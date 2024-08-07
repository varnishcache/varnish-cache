/*-
 * Copyright (c) 2008-2010 Varnish Software AS
 * All rights reserved.
 *
 * Author: Frédéric Lécaille <flecaille@haproxy.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"

#include <sys/types.h>
#include <sys/socket.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vtc.h"

#include "vsa.h"
#include "vss.h"
#include "vtcp.h"
#include "vre.h"

struct syslog_srv {
	unsigned			magic;
#define SYSLOG_SRV_MAGIC		0xbf28a692
	char				*name;
	struct vtclog			*vl;
	VTAILQ_ENTRY(syslog_srv)	list;
	char				run;

	int				repeat;
	char				*spec;

	int				sock;
	char				bind[256];
	int				lvl;

	pthread_t			tp;
	ssize_t				rxbuf_left;
	size_t				rxbuf_sz;
	char				*rxbuf;
	vtim_dur			timeout;
};

static pthread_mutex_t			syslog_mtx;

static VTAILQ_HEAD(, syslog_srv)	syslogs =
    VTAILQ_HEAD_INITIALIZER(syslogs);

#define SYSLOGCMDS \
	CMD_SYSLOG(expect) \
	CMD_SYSLOG(recv)

#define CMD_SYSLOG(nm) static cmd_f cmd_syslog_##nm;
SYSLOGCMDS
#undef CMD_SYSLOG

static const struct cmds syslog_cmds[] = {
#define CMD_SYSLOG(n) { #n, cmd_syslog_##n },
SYSLOGCMDS
#undef CMD_SYSLOG
	{ NULL, NULL }
};

static const char * const syslog_levels[] = {
	"emerg",
	"alert",
	"crit",
	"err",
	"warning",
	"notice",
	"info",
	"debug",
	NULL,
};

static int
get_syslog_level(struct vtclog *vl, const char *lvl)
{
	int i;

	for (i = 0; syslog_levels[i]; i++)
		if (!strcmp(lvl, syslog_levels[i]))
			return (i);
	vtc_fatal(vl, "wrong syslog level '%s'\n", lvl);
}

/*--------------------------------------------------------------------
 * Check if a UDP syscall return value is fatal
 * XXX: Largely copied from VTCP, not sure if really applicable
 */

static int
VUDP_Check(int a)
{
	if (a == 0)
		return (1);
	if (errno == ECONNRESET)
		return (1);
#if (defined (__SVR4) && defined (__sun)) || defined (__NetBSD__)
	/*
	 * Solaris returns EINVAL if the other end unexpectedly reset the
	 * connection.
	 * This is a bug in Solaris and documented behaviour on NetBSD.
	 */
	if (errno == EINVAL || errno == ETIMEDOUT || errno == EPIPE)
		return (1);
#elif defined (__APPLE__)
	/*
	 * MacOS returns EINVAL if the other end unexpectedly reset
	 * the connection.
	 */
	if (errno == EINVAL)
		return (1);
#endif
	return (0);
}

/*--------------------------------------------------------------------
 * When closing a UDP connection, a couple of errno's are legit, we
 * can't be held responsible for the other end wanting to talk to us.
 */

static void
VUDP_close(int *s)
{
	int i;

	i = close(*s);

	assert(VUDP_Check(i));
	*s = -1;
}

/*--------------------------------------------------------------------
 * Given a struct suckaddr, open a socket of the appropriate type, and bind
 * it to the requested address.
 *
 * If the address is an IPv6 address, the IPV6_V6ONLY option is set to
 * avoid conflicts between INADDR_ANY and IN6ADDR_ANY.
 */

static int
VUDP_bind(const struct suckaddr *sa, const char **errp)
{
#ifdef IPV6_V6ONLY
	int val;
#endif
	int sd, e;
	socklen_t sl;
	const struct sockaddr *so;
	int proto;

	if (errp != NULL)
		*errp = NULL;

	proto = VSA_Get_Proto(sa);
	sd = socket(proto, SOCK_DGRAM, 0);
	if (sd < 0) {
		if (errp != NULL)
			*errp = "socket(2)";
		return (-1);
	}

#ifdef IPV6_V6ONLY
	/* forcibly use separate sockets for IPv4 and IPv6 */
	val = 1;
	if (proto == AF_INET6 &&
	    setsockopt(sd, IPPROTO_IPV6, IPV6_V6ONLY, &val, sizeof val) != 0) {
		if (errp != NULL)
			*errp = "setsockopt(IPV6_V6ONLY, 1)";
		e = errno;
		closefd(&sd);
		errno = e;
		return (-1);
	}
#endif
	so = VSA_Get_Sockaddr(sa, &sl);
	if (bind(sd, so, sl) != 0) {
		if (errp != NULL)
			*errp = "bind(2)";
		e = errno;
		closefd(&sd);
		errno = e;
		return (-1);
	}
	return (sd);
}

/*--------------------------------------------------------------------*/

struct udp_helper {
	const char	**errp;
};

static int v_matchproto_(vss_resolved_f)
vudp_lo_cb(void *priv, const struct suckaddr *sa)
{
	int sock;
	struct udp_helper *hp = priv;

	sock = VUDP_bind(sa, hp->errp);
	if (sock > 0) {
		*hp->errp = NULL;
		return (sock);
	}
	AN(*hp->errp);
	return (0);
}

static int
VUDP_bind_on(const char *addr, const char *def_port, const char **errp)
{
	struct udp_helper h;
	int sock;

	h.errp = errp;

	sock = VSS_resolver_socktype(
	    addr, def_port, vudp_lo_cb, &h, errp, SOCK_DGRAM);
	if (*errp != NULL)
		return (-1);
	return (sock);
}

/**********************************************************************
 * Allocate and initialize a syslog
 */

static struct syslog_srv *
syslog_new(const char *name, struct vtclog *vl)
{
	struct syslog_srv *s;

	VTC_CHECK_NAME(vl, name, "Syslog", 'S');
	ALLOC_OBJ(s, SYSLOG_SRV_MAGIC);
	AN(s);
	REPLACE(s->name, name);
	s->vl = vtc_logopen("%s", s->name);
	AN(s->vl);
	vtc_log_set_cmd(s->vl, syslog_cmds);

	bprintf(s->bind, "%s", default_listen_addr);
	s->repeat = 1;
	s->sock = -1;
	s->lvl = -1;
	s->timeout = vtc_maxdur * .5;		// XXX

	vl = vtc_logopen("%s", s->name);
	AN(vl);

	s->rxbuf_sz = s->rxbuf_left = 2048*1024;
	s->rxbuf = malloc(s->rxbuf_sz);		/* XXX */
	AN(s->rxbuf);

	PTOK(pthread_mutex_lock(&syslog_mtx));
	VTAILQ_INSERT_TAIL(&syslogs, s, list);
	PTOK(pthread_mutex_unlock(&syslog_mtx));
	return (s);
}

/**********************************************************************
 * Clean up a syslog
 */

static void
syslog_delete(struct syslog_srv *s)
{

	CHECK_OBJ_NOTNULL(s, SYSLOG_SRV_MAGIC);
	macro_undef(s->vl, s->name, "addr");
	macro_undef(s->vl, s->name, "port");
	macro_undef(s->vl, s->name, "sock");
	vtc_logclose(s->vl);
	free(s->name);
	free(s->rxbuf);
	/* XXX: MEMLEAK (?) (VSS ??) */
	FREE_OBJ(s);
}

static void
syslog_rx(const struct syslog_srv *s, int lvl)
{
	ssize_t ret;

	while (!vtc_error) {
		/* Pointers to syslog priority value (see <PRIVAL>, rfc5424). */
		char *prib, *prie, *end;
		unsigned int prival;

		VTCP_set_read_timeout(s->sock, s->timeout);

		ret = recv(s->sock, s->rxbuf, s->rxbuf_sz - 1, 0);
		if (ret < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;

			vtc_fatal(s->vl,
			    "%s: recv failed (fd: %d read: %s", __func__,
			    s->sock, strerror(errno));
		}
		if (ret == 0)
			vtc_fatal(s->vl,
			    "syslog rx timeout (fd: %d %.3fs ret: %zd)",
			    s->sock, s->timeout, ret);

		s->rxbuf[ret] = '\0';
		vtc_dump(s->vl, 4, "syslog", s->rxbuf, ret);

		prib = s->rxbuf;
		if (*prib++ != '<')
			vtc_fatal(s->vl, "syslog PRI, no '<'");
		prie = strchr(prib, '>');
		if (prie == NULL)
			vtc_fatal(s->vl, "syslog PRI, no '>'");

		prival = strtoul(prib, &end, 10);
		if (end != prie)
			vtc_fatal(s->vl, "syslog PRI, bad number");

		if (lvl >= 0 && lvl == (prival & 0x7))
			return;
	}
}

/**********************************************************************
 * Syslog server bind
 */

static void
syslog_bind(struct syslog_srv *s)
{
	const char *err;
	char aaddr[VTCP_ADDRBUFSIZE];
	char aport[VTCP_PORTBUFSIZE];
	char buf[vsa_suckaddr_len];
	const struct suckaddr *sua;

	CHECK_OBJ_NOTNULL(s, SYSLOG_SRV_MAGIC);

	if (s->sock >= 0)
		VUDP_close(&s->sock);
	s->sock = VUDP_bind_on(s->bind, "0", &err);
	if (err != NULL)
		vtc_fatal(s->vl,
		    "Syslog server bind address (%s) cannot be resolved: %s",
		    s->bind, err);
	assert(s->sock > 0);
	sua = VSA_getsockname(s->sock, buf, sizeof buf);
	AN(sua);
	VTCP_name(sua, aaddr, sizeof aaddr, aport, sizeof aport);
	macro_def(s->vl, s->name, "addr", "%s", aaddr);
	macro_def(s->vl, s->name, "port", "%s", aport);
	if (VSA_Get_Proto(sua) == AF_INET)
		macro_def(s->vl, s->name, "sock", "%s:%s", aaddr, aport);
	else
		macro_def(s->vl, s->name, "sock", "[%s]:%s", aaddr, aport);
	/* Record the actual port, and reuse it on subsequent starts */
	bprintf(s->bind, "%s %s", aaddr, aport);
}

static void v_matchproto_(cmd_f)
cmd_syslog_expect(CMD_ARGS)
{
	struct syslog_srv *s;
	struct vsb vsb[1];
	vre_t *vre;
	int error, erroroffset, i, ret;
	char *cmp, *spec, errbuf[VRE_ERROR_LEN];

	(void)vl;
	CAST_OBJ_NOTNULL(s, priv, SYSLOG_SRV_MAGIC);
	AZ(strcmp(av[0], "expect"));
	av++;

	cmp = av[0];
	spec = av[1];
	AN(cmp);
	AN(spec);
	AZ(av[2]);

	assert(!strcmp(cmp, "~") || !strcmp(cmp, "!~"));

	vre = VRE_compile(spec, 0, &error, &erroroffset, 1);
	if (vre == NULL) {
		AN(VSB_init(vsb, errbuf, sizeof errbuf));
		AZ(VRE_error(vsb, error));
		AZ(VSB_finish(vsb));
		VSB_fini(vsb);
		vtc_fatal(s->vl, "REGEXP error: '%s' (@%d) (%s)",
		    errbuf, erroroffset, spec);
	}

	i = VRE_match(vre, s->rxbuf, 0, 0, NULL);

	VRE_free(&vre);

	ret = (i >= 0 && *cmp == '~') || (i < 0 && *cmp == '!');
	if (!ret)
		vtc_fatal(s->vl, "EXPECT FAILED %s \"%s\"", cmp, spec);
	else
		vtc_log(s->vl, 4, "EXPECT MATCH %s \"%s\"", cmp, spec);
}

static void v_matchproto_(cmd_f)
cmd_syslog_recv(CMD_ARGS)
{
	int lvl;
	struct syslog_srv *s;

	CAST_OBJ_NOTNULL(s, priv, SYSLOG_SRV_MAGIC);
	(void)vl;
	AZ(strcmp(av[0], "recv"));
	av++;
	if (av[0] == NULL)
		lvl = s->lvl;
	else
		lvl = get_syslog_level(vl, av[0]);

	syslog_rx(s, lvl);
}

/**********************************************************************
 * Syslog server thread
 */

static void *
syslog_thread(void *priv)
{
	struct syslog_srv *s;
	int i;

	CAST_OBJ_NOTNULL(s, priv, SYSLOG_SRV_MAGIC);
	assert(s->sock >= 0);

	vtc_log(s->vl, 2, "Started on %s (level: %d)", s->bind, s->lvl);
	for (i = 0; i < s->repeat; i++) {
		if (s->repeat > 1)
			vtc_log(s->vl, 3, "Iteration %d", i);
		parse_string(s->vl, s, s->spec);
		vtc_log(s->vl, 3, "shutting fd %d", s->sock);
	}
	VUDP_close(&s->sock);
	vtc_log(s->vl, 2, "Ending");
	return (NULL);
}

/**********************************************************************
 * Start the syslog thread
 */

static void
syslog_start(struct syslog_srv *s)
{
	CHECK_OBJ_NOTNULL(s, SYSLOG_SRV_MAGIC);
	vtc_log(s->vl, 2, "Starting syslog server");
	if (s->sock == -1)
		syslog_bind(s);
	vtc_log(s->vl, 1, "Bound on %s", s->bind);
	s->run = 1;
	PTOK(pthread_create(&s->tp, NULL, syslog_thread, s));
}

/**********************************************************************
 * Force stop the syslog thread
 */

static void
syslog_stop(struct syslog_srv *s)
{
	void *res;

	CHECK_OBJ_NOTNULL(s, SYSLOG_SRV_MAGIC);
	vtc_log(s->vl, 2, "Stopping for syslog server");
	(void)pthread_cancel(s->tp);
	PTOK(pthread_join(s->tp, &res));
	s->tp = 0;
	s->run = 0;
}

/**********************************************************************
 * Wait for syslog thread to stop
 */

static void
syslog_wait(struct syslog_srv *s)
{
	void *res;

	CHECK_OBJ_NOTNULL(s, SYSLOG_SRV_MAGIC);
	vtc_log(s->vl, 2, "Waiting for syslog server (%d)", s->sock);
	PTOK(pthread_join(s->tp, &res));
	if (res != NULL && !vtc_stop)
		vtc_fatal(s->vl, "Syslog server returned \"%p\"",
		    (char *)res);
	s->tp = 0;
	s->run = 0;
}

/* SECTION: syslog syslog
 *
 * Define and interact with syslog instances (for use with haproxy)
 *
 * To define a syslog server, you'll use this syntax::
 *
 *     syslog SNAME
 *
 * Arguments:
 *
 * SNAME
 *     Identify the syslog server with a string which must start with 'S'.
 *
 * \-level STRING
 *         Set the default syslog priority level used by any subsequent "recv"
 *         command.
 *         Any syslog dgram with a different level will be skipped by
 *         "recv" command. This default level value may be superseded
 *         by "recv" command if supplied as first argument: "recv <level>".
 *
 * \-start
 *         Start the syslog server thread in the background.
 *
 * \-repeat
 *         Instead of processing the specification only once, do it
 *	   NUMBER times.
 *
 * \-bind
 *         Bind the syslog socket to a local address.
 *
 * \-wait
 *         Wait for that thread to terminate.
 *
 * \-stop
 *         Stop the syslog server thread.
 */

void v_matchproto_(cmd_f)
cmd_syslog(CMD_ARGS)
{
	struct syslog_srv *s;

	(void)priv;

	if (av == NULL) {
		/* Reset and free */
		do {
			PTOK(pthread_mutex_lock(&syslog_mtx));
			s = VTAILQ_FIRST(&syslogs);
			CHECK_OBJ_ORNULL(s, SYSLOG_SRV_MAGIC);
			if (s != NULL)
				VTAILQ_REMOVE(&syslogs, s, list);
			PTOK(pthread_mutex_unlock(&syslog_mtx));
			if (s != NULL) {
				if (s->run) {
					(void)pthread_cancel(s->tp);
					syslog_wait(s);
				}
				if (s->sock >= 0)
					VUDP_close(&s->sock);
				syslog_delete(s);
			}
		} while (s != NULL);
		return;
	}

	AZ(strcmp(av[0], "syslog"));
	av++;

	PTOK(pthread_mutex_lock(&syslog_mtx));
	VTAILQ_FOREACH(s, &syslogs, list)
		if (!strcmp(s->name, av[0]))
			break;
	PTOK(pthread_mutex_unlock(&syslog_mtx));
	if (s == NULL)
		s = syslog_new(av[0], vl);
	CHECK_OBJ_NOTNULL(s, SYSLOG_SRV_MAGIC);
	av++;

	for (; *av != NULL; av++) {
		if (vtc_error)
			break;
		if (!strcmp(*av, "-wait")) {
			if (!s->run)
				vtc_fatal(s->vl, "Syslog server not -started");
			syslog_wait(s);
			continue;
		}

		if (!strcmp(*av, "-stop")) {
			syslog_stop(s);
			continue;
		}

		/*
		 * We do an implicit -wait if people muck about with a
		 * running syslog.
		 * This only works if the previous ->spec has completed
		 */
		if (s->run)
			syslog_wait(s);

		AZ(s->run);
		if (!strcmp(*av, "-repeat")) {
			AN(av[1]);
			s->repeat = atoi(av[1]);
			av++;
			continue;
		}
		if (!strcmp(*av, "-bind")) {
			AN(av[1]);
			bprintf(s->bind, "%s", av[1]);
			av++;
			syslog_bind(s);
			continue;
		}
		if (!strcmp(*av, "-level")) {
			AN(av[1]);
			s->lvl = get_syslog_level(vl, av[1]);
			av++;
			continue;
		}
		if (!strcmp(*av, "-start")) {
			syslog_start(s);
			continue;
		}
		if (**av == '-')
			vtc_fatal(s->vl, "Unknown syslog argument: %s", *av);
		s->spec = *av;
	}
}

void
init_syslog(void)
{
	PTOK(pthread_mutex_init(&syslog_mtx, NULL));
}
