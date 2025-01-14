/*-
 * Copyright (c) 2008-2010 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vsa.h"
#include "vtc.h"

#include "vtcp.h"
#include "vus.h"

struct server {
	unsigned		magic;
#define SERVER_MAGIC		0x55286619
	char			*name;
	struct vtclog		*vl;
	VTAILQ_ENTRY(server)	list;
	struct vtc_sess		*vsp;
	char			run;

	char			*spec;

	int			depth;
	int			sock;
	int			fd;
	unsigned		is_dispatch;
	char			listen[256];
	char			aaddr[VTCP_ADDRBUFSIZE];
	char			aport[VTCP_PORTBUFSIZE];

	pthread_t		tp;
};

static pthread_mutex_t		server_mtx;

static VTAILQ_HEAD(, server)	servers =
    VTAILQ_HEAD_INITIALIZER(servers);

/**********************************************************************
 * Allocate and initialize a server
 */

static struct server *
server_new(const char *name, struct vtclog *vl)
{
	struct server *s;

	VTC_CHECK_NAME(vl, name, "Server", 's');
	ALLOC_OBJ(s, SERVER_MAGIC);
	AN(s);
	REPLACE(s->name, name);
	s->vl = vtc_logopen("%s", s->name);
	AN(s->vl);
	s->vsp = Sess_New(s->vl, name);
	AN(s->vsp);

	bprintf(s->listen, "%s", default_listen_addr);
	s->depth = 10;
	s->sock = -1;
	s->fd = -1;
	PTOK(pthread_mutex_lock(&server_mtx));
	VTAILQ_INSERT_TAIL(&servers, s, list);
	PTOK(pthread_mutex_unlock(&server_mtx));
	return (s);
}

/**********************************************************************
 * Clean up a server
 */

static void
server_delete(struct server *s)
{

	CHECK_OBJ_NOTNULL(s, SERVER_MAGIC);
	Sess_Destroy(&s->vsp);
	macro_undef(s->vl, s->name, "addr");
	macro_undef(s->vl, s->name, "port");
	macro_undef(s->vl, s->name, "sock");
	vtc_logclose(s->vl);
	free(s->name);
	/* XXX: MEMLEAK (?) (VSS ??) */
	FREE_OBJ(s);
}

/**********************************************************************
 * Server listen
 */

struct helper {
	int		depth;
	const char	**errp;
};

/* cf. VTCP_listen_on() */
static int v_matchproto_(vus_resolved_f)
uds_listen(void *priv, const struct sockaddr_un *uds)
{
	int sock, e;
	struct helper *hp = priv;

	sock = VUS_bind(uds, hp->errp);
	if (sock >= 0)   {
		if (listen(sock, hp->depth) != 0) {
			e = errno;
			closefd(&sock);
			errno = e;
			if (hp->errp != NULL)
				*hp->errp = "listen(2)";
			return (-1);
		}
	}
	if (sock > 0) {
		*hp->errp = NULL;
		return (sock);
	}
	AN(*hp->errp);
	return (0);
}

static void
server_listen_uds(struct server *s, const char **errp)
{
	mode_t m;
	struct helper h;

	h.depth = s->depth;
	h.errp = errp;

	errno = 0;
	if (unlink(s->listen) != 0 && errno != ENOENT)
		vtc_fatal(s->vl, "Could not unlink %s before bind: %s",
		    s->listen, strerror(errno));
	/*
	 * Temporarily set the umask to 0 to avoid issues with
	 * permissions.
	 */
	m = umask(0);
	s->sock = VUS_resolver(s->listen, uds_listen, &h, errp);
	(void)umask(m);
	if (*errp != NULL)
		return;
	assert(s->sock > 0);
	macro_def(s->vl, s->name, "addr", "0.0.0.0");
	macro_def(s->vl, s->name, "port", "0");
	macro_def(s->vl, s->name, "sock", "%s", s->listen);
}

static void
server_listen_tcp(struct server *s, const char **errp)
{
	char buf[vsa_suckaddr_len];
	const struct suckaddr *sua;

	s->sock = VTCP_listen_on(s->listen, "0", s->depth, errp);
	if (*errp != NULL)
		return;
	assert(s->sock > 0);
	sua = VSA_getsockname(s->sock, buf, sizeof buf);
	AN(sua);
	VTCP_name(sua, s->aaddr, sizeof s->aaddr,
	    s->aport, sizeof s->aport);

	/* Record the actual port, and reuse it on subsequent starts */
	if (VSA_Get_Proto(sua) == AF_INET)
		bprintf(s->listen, "%s:%s", s->aaddr, s->aport);
	else
		bprintf(s->listen, "[%s]:%s", s->aaddr, s->aport);

	macro_def(s->vl, s->name, "addr", "%s", s->aaddr);
	macro_def(s->vl, s->name, "port", "%s", s->aport);
	macro_def(s->vl, s->name, "sock", "%s", s->listen);
}

static void
server_listen(struct server *s)
{
	const char *err;

	CHECK_OBJ_NOTNULL(s, SERVER_MAGIC);

	if (s->sock >= 0)
		VTCP_close(&s->sock);
	if (VUS_is(s->listen))
		server_listen_uds(s, &err);
	else
		server_listen_tcp(s, &err);
	if (err != NULL)
		vtc_fatal(s->vl,
		    "Server listen address (%s) cannot be resolved: %s",
		    s->listen, err);
}

/**********************************************************************
 * Server thread
 */

static int
server_conn(void *priv, struct vtclog *vl)
{
	struct server *s;
	struct sockaddr_storage addr_s;
	struct sockaddr *addr;
	char abuf[VTCP_ADDRBUFSIZE];
	char pbuf[VTCP_PORTBUFSIZE];
	socklen_t l;
	int fd;

	CAST_OBJ_NOTNULL(s, priv, SERVER_MAGIC);

	addr = (void*)&addr_s;
	l = sizeof addr_s;
	fd = accept(s->sock, addr, &l);
	if (fd < 0)
		vtc_fatal(vl, "Accept failed: %s", strerror(errno));
	if (VUS_is(s->listen))
		vtc_log(vl, 3, "accepted fd %d 0.0.0.0 0", fd);
	else {
		VTCP_hisname(fd, abuf, sizeof abuf, pbuf, sizeof pbuf);
		vtc_log(vl, 3, "accepted fd %d %s %s", fd, abuf, pbuf);
	}
	return (fd);
}

static void
server_disc(void *priv, struct vtclog *vl, int *fdp)
{
	int j;
	struct server *s;

	CAST_OBJ_NOTNULL(s, priv, SERVER_MAGIC);
	vtc_log(vl, 3, "shutting fd %d (server run)", *fdp);
	j = shutdown(*fdp, SHUT_WR);
	if (!vtc_stop && !VTCP_Check(j))
		vtc_fatal(vl, "Shutdown failed: %s", strerror(errno));
	VTCP_close(fdp);
}

static void
server_start_thread(struct server *s)
{

	s->run = 1;
	s->tp = Sess_Start_Thread(
	    s,
	    s->vsp,
	    server_conn,
	    server_disc,
	    s->listen,
	    &s->sock,
	    s->spec
	);
}

/**********************************************************************
 * Start the server thread
 */

static void
server_start(struct server *s)
{
	CHECK_OBJ_NOTNULL(s, SERVER_MAGIC);
	vtc_log(s->vl, 2, "Starting server");
	server_listen(s);
	vtc_log(s->vl, 1, "Listen on %s", s->listen);
	server_start_thread(s);
}

/**********************************************************************
 */

static void *
server_dispatch_wrk(void *priv)
{
	struct server *s;
	struct vtclog *vl;
	int j, fd;

	CAST_OBJ_NOTNULL(s, priv, SERVER_MAGIC);
	assert(s->sock < 0);

	vl = vtc_logopen("%s", s->name);
	pthread_cleanup_push(vtc_logclose, vl);

	fd = s->fd;

	vtc_log(vl, 3, "start with fd %d", fd);
	fd = sess_process(vl, s->vsp, s->spec, fd, &s->sock, s->listen);
	vtc_log(vl, 3, "shutting fd %d (server dispatch)", fd);
	j = shutdown(fd, SHUT_WR);
	if (!VTCP_Check(j))
		vtc_fatal(vl, "Shutdown failed: %s", strerror(errno));
	VTCP_close(&s->fd);
	vtc_log(vl, 2, "Ending");
	pthread_cleanup_pop(0);
	vtc_logclose(vl);
	return (NULL);
}

static void *
server_dispatch_thread(void *priv)
{
	struct server *s, *s2;
	static int sn = 1;
	int fd;
	char snbuf[8];
	struct vtclog *vl;
	struct sockaddr_storage addr_s;
	struct sockaddr *addr;
	socklen_t l;

	CAST_OBJ_NOTNULL(s, priv, SERVER_MAGIC);
	assert(s->sock >= 0);

	vl = vtc_logopen("%s", s->name);
	pthread_cleanup_push(vtc_logclose, vl);

	vtc_log(vl, 2, "Dispatch started on %s", s->listen);

	while (!vtc_stop) {
		addr = (void*)&addr_s;
		l = sizeof addr_s;
		fd = accept(s->sock, addr, &l);
		if (fd < 0)
			vtc_fatal(vl, "Accepted failed: %s", strerror(errno));
		bprintf(snbuf, "s%d", sn++);
		vtc_log(vl, 3, "dispatch fd %d -> %s", fd, snbuf);
		s2 = server_new(snbuf, vl);
		s2->is_dispatch = 1;
		s2->spec = s->spec;
		bstrcpy(s2->listen, s->listen);
		s2->fd = fd;
		s2->run = 1;
		PTOK(pthread_create(&s2->tp, NULL, server_dispatch_wrk, s2));
	}
	pthread_cleanup_pop(0);
	vtc_logclose(vl);
	NEEDLESS(return (NULL));
}

static void
server_dispatch(struct server *s)
{
	CHECK_OBJ_NOTNULL(s, SERVER_MAGIC);
	server_listen(s);
	vtc_log(s->vl, 2, "Starting dispatch server");
	s->run = 1;
	PTOK(pthread_create(&s->tp, NULL, server_dispatch_thread, s));
}

/**********************************************************************
 * Force stop the server thread
 */

static void
server_break(struct server *s)
{
	void *res;

	CHECK_OBJ_NOTNULL(s, SERVER_MAGIC);
	vtc_log(s->vl, 2, "Breaking for server");
	(void)pthread_cancel(s->tp);
	PTOK(pthread_join(s->tp, &res));
	VTCP_close(&s->sock);
	s->tp = 0;
	s->run = 0;
}

/**********************************************************************
 * Wait for server thread to stop
 */

static void
server_wait(struct server *s)
{
	void *res;

	CHECK_OBJ_NOTNULL(s, SERVER_MAGIC);
	vtc_log(s->vl, 2, "Waiting for server (%d/%d)", s->sock, s->fd);
	PTOK(pthread_join(s->tp, &res));
	if (res != NULL && !vtc_stop)
		vtc_fatal(s->vl, "Server returned \"%p\"",
		    (char *)res);
	s->tp = 0;
	s->run = 0;
}

/**********************************************************************
 * Generate VCL backend decls for our servers
 */

void
cmd_server_gen_vcl(struct vsb *vsb)
{
	struct server *s;

	PTOK(pthread_mutex_lock(&server_mtx));
	VTAILQ_FOREACH(s, &servers, list) {
		if (s->is_dispatch)
			continue;

		if (VUS_is(s->listen))
			VSB_printf(vsb,
			   "backend %s { .path = \"%s\"; }\n",
			   s->name, s->listen);
		else
			VSB_printf(vsb,
			   "backend %s { .host = \"%s\"; .port = \"%s\"; }\n",
			   s->name, s->aaddr, s->aport);
	}
	PTOK(pthread_mutex_unlock(&server_mtx));
}


/**********************************************************************
 * Generate VCL backend decls for our servers
 */

void
cmd_server_gen_haproxy_conf(struct vsb *vsb)
{
	struct server *s;

	PTOK(pthread_mutex_lock(&server_mtx));
	VTAILQ_FOREACH(s, &servers, list) {
		if (! VUS_is(s->listen))
			VSB_printf(vsb,
			   "\n    backend be%s\n"
			   "\tserver srv%s %s:%s\n",
			   s->name + 1, s->name + 1, s->aaddr, s->aport);
		else
			INCOMPL();
	}
	VTAILQ_FOREACH(s, &servers, list) {
		if (! VUS_is(s->listen))
			VSB_printf(vsb,
			   "\n    frontend http%s\n"
			   "\tuse_backend be%s\n"
			   "\tbind \"fd@${fe%s}\"\n",
			   s->name + 1, s->name + 1, s->name + 1);
		else
			INCOMPL();
	}
	PTOK(pthread_mutex_unlock(&server_mtx));
}


/**********************************************************************
 * Server command dispatch
 */

void
cmd_server(CMD_ARGS)
{
	struct server *s;

	(void)priv;

	if (av == NULL) {
		/* Reset and free */
		while (1) {
			PTOK(pthread_mutex_lock(&server_mtx));
			s = VTAILQ_FIRST(&servers);
			CHECK_OBJ_ORNULL(s, SERVER_MAGIC);
			if (s != NULL)
				VTAILQ_REMOVE(&servers, s, list);
			PTOK(pthread_mutex_unlock(&server_mtx));
			if (s == NULL)
				break;
			if (s->run) {
				(void)pthread_cancel(s->tp);
				server_wait(s);
			}
			if (s->sock >= 0)
				VTCP_close(&s->sock);
			server_delete(s);
		}
		return;
	}

	AZ(strcmp(av[0], "server"));
	av++;

	PTOK(pthread_mutex_lock(&server_mtx));
	VTAILQ_FOREACH(s, &servers, list)
		if (!strcmp(s->name, av[0]))
			break;
	PTOK(pthread_mutex_unlock(&server_mtx));
	if (s == NULL)
		s = server_new(av[0], vl);
	CHECK_OBJ_NOTNULL(s, SERVER_MAGIC);
	av++;

	for (; *av != NULL; av++) {
		if (vtc_error)
			break;
		if (!strcmp(*av, "-wait")) {
			if (!s->run)
				vtc_fatal(s->vl, "Server not -started");
			server_wait(s);
			continue;
		}

		if (!strcmp(*av, "-break")) {
			server_break(s);
			continue;
		}

		/*
		 * We do an implicit -wait if people muck about with a
		 * running server.
		 */
		if (s->run)
			server_wait(s);

		AZ(s->run);

		if (Sess_GetOpt(s->vsp, &av))
			continue;

		if (!strcmp(*av, "-listen")) {
			if (s->sock >= 0)
				VTCP_close(&s->sock);
			bprintf(s->listen, "%s", av[1]);
			av++;
			continue;
		}
		if (!strcmp(*av, "-start")) {
			server_start(s);
			continue;
		}
		if (!strcmp(*av, "-dispatch")) {
			if (strcmp(s->name, "s0"))
				vtc_fatal(s->vl,
				    "server -dispatch only works on s0");
			server_dispatch(s);
			continue;
		}
		if (**av == '-')
			vtc_fatal(s->vl, "Unknown server argument: %s", *av);
		s->spec = *av;
	}
}

void
init_server(void)
{
	PTOK(pthread_mutex_init(&server_mtx, NULL));
}
