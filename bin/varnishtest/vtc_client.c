/*-
 * Copyright (c) 2008-2011 Varnish Software AS
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
#include <sys/un.h>

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <unistd.h>

#include "vtc.h"

#include "vsa.h"
#include "vss.h"
#include "vtcp.h"
#include "vus.h"

struct client {
	unsigned		magic;
#define CLIENT_MAGIC		0x6242397c
	char			*name;
	struct vtclog		*vl;
	VTAILQ_ENTRY(client)	list;
	struct vtc_sess		*vsp;

	char			*spec;

	char			connect[256];
	char			*addr;

	char			*proxy_spec;
	int			proxy_version;

	unsigned		running;
	pthread_t		tp;
};

static VTAILQ_HEAD(, client)	clients = VTAILQ_HEAD_INITIALIZER(clients);

/**********************************************************************
 * Send the proxy header
 */

static void
client_proxy(struct vtclog *vl, int fd, int version, const char *spec)
{
	const struct suckaddr *sac, *sas;
	char *p, *p2;

	p = strdup(spec);
	AN(p);
	p2 = strchr(p, ' ');
	AN(p2);
	*p2++ = '\0';

	sac = VSS_ResolveOne(NULL, p, NULL, 0, SOCK_STREAM, AI_PASSIVE);
	if (sac == NULL)
		vtc_fatal(vl, "Could not resolve client address");
	sas = VSS_ResolveOne(NULL, p2, NULL, 0, SOCK_STREAM, AI_PASSIVE);
	if (sas == NULL)
		vtc_fatal(vl, "Could not resolve server address");
	if (vtc_send_proxy(fd, version, sac, sas))
		vtc_fatal(vl, "Write failed: %s", strerror(errno));
	free(p);
	VSA_free(&sac);
	VSA_free(&sas);
}

/**********************************************************************
 * Socket connect.
 */

static int
client_tcp_connect(struct vtclog *vl, const char *addr, double tmo,
		   const char **errp)
{
	int fd;
	char mabuf[VTCP_ADDRBUFSIZE], mpbuf[VTCP_PORTBUFSIZE];

	AN(addr);
	AN(errp);
	fd = VTCP_open(addr, NULL, tmo, errp);
	if (fd < 0)
		return (fd);
	VTCP_myname(fd, mabuf, sizeof mabuf, mpbuf, sizeof mpbuf);
	vtc_log(vl, 3, "connected fd %d from %s %s to %s", fd, mabuf, mpbuf,
		addr);
	return (fd);
}

/* cf. VTCP_Open() */
static int v_matchproto_(vus_resolved_f)
uds_open(void *priv, const struct sockaddr_un *uds)
{
	double *p;
	int s, i, tmo;
	struct pollfd fds[1];
	socklen_t sl;

	sl = VUS_socklen(uds);

	AN(priv);
	AN(uds);
	p = priv;
	assert(*p > 0.);
	tmo = (int)(*p * 1e3);

	s = socket(uds->sun_family, SOCK_STREAM, 0);
	if (s < 0)
		return (s);

	VTCP_nonblocking(s);
	i = connect(s, (const void*)uds, sl);
	if (i == 0)
		return (s);
	if (errno != EINPROGRESS) {
		closefd(&s);
		return (-1);
	}

	fds[0].fd = s;
	fds[0].events = POLLWRNORM;
	fds[0].revents = 0;
	i = poll(fds, 1, tmo);

	if (i == 0) {
		closefd(&s);
		errno = ETIMEDOUT;
		return (-1);
	}

	return (VTCP_connected(s));
}

static int
client_uds_connect(struct vtclog *vl, const char *path, double tmo,
		   const char **errp)
{
	int fd;

	assert(tmo >= 0);

	errno = 0;
	fd = VUS_resolver(path, uds_open, &tmo, errp);
	if (fd < 0) {
		*errp = strerror(errno);
		return (fd);
	}
	vtc_log(vl, 3, "connected fd %d to %s", fd, path);
	return (fd);
}

static int
client_connect(struct vtclog *vl, struct client *c)
{
	const char *err;
	int fd;

	/* The connect timeout is 3s, to make up for the slow_acceptor
	 * debug flag that adds 2s per connection.
	 */
	vtc_log(vl, 3, "Connect to %s", c->addr);
	if (VUS_is(c->addr))
		fd = client_uds_connect(vl, c->addr, 3., &err);
	else
		fd = client_tcp_connect(vl, c->addr, 3., &err);
	if (fd < 0)
		vtc_fatal(c->vl, "Failed to open %s: %s",
		    c->addr, err);
	/* VTCP_blocking does its own checks, trust it */
	VTCP_blocking(fd);
	if (c->proxy_spec != NULL)
		client_proxy(vl, fd, c->proxy_version, c->proxy_spec);
	return (fd);
}

/**********************************************************************
 * Client thread
 */

static int
client_conn(void *priv, struct vtclog *vl)
{
	struct client *c;

	CAST_OBJ_NOTNULL(c, priv, CLIENT_MAGIC);
	return (client_connect(vl, c));
}

static void
client_disc(void *priv, struct vtclog *vl, int *fdp)
{
	(void)priv;
	vtc_log(vl, 3, "closing fd %d", *fdp);
	VTCP_close(fdp);
}

/**********************************************************************
 * Allocate and initialize a client
 */

static struct client *
client_new(const char *name)
{
	struct client *c;

	ALLOC_OBJ(c, CLIENT_MAGIC);
	AN(c);
	REPLACE(c->name, name);
	c->vl = vtc_logopen("%s", name);
	AN(c->vl);
	c->vsp = Sess_New(c->vl, name);
	AN(c->vsp);

	bprintf(c->connect, "%s", "${v1_sock}");
	VTAILQ_INSERT_TAIL(&clients, c, list);
	return (c);
}

/**********************************************************************
 * Clean up client
 */

static void
client_delete(struct client *c)
{

	CHECK_OBJ_NOTNULL(c, CLIENT_MAGIC);
	Sess_Destroy(&c->vsp);
	vtc_logclose(c->vl);
	free(c->spec);
	free(c->name);
	free(c->addr);
	free(c->proxy_spec);
	/* XXX: MEMLEAK (?)*/
	FREE_OBJ(c);
}

/**********************************************************************
 * Start the client thread
 */

static void
client_start(struct client *c)
{
	struct vsb *vsb;

	CHECK_OBJ_NOTNULL(c, CLIENT_MAGIC);
	vtc_log(c->vl, 2, "Starting client");
	c->running = 1;
	vsb = macro_expand(c->vl, c->connect);
	AN(vsb);
	REPLACE(c->addr, VSB_data(vsb));
	VSB_destroy(&vsb);
	c->tp = Sess_Start_Thread(
	    c,
	    c->vsp,
	    client_conn,
	    client_disc,
	    c->addr,
	    NULL,
	    c->spec
	);
}

/**********************************************************************
 * Wait for client thread to stop
 */

static void
client_wait(struct client *c)
{
	void *res;

	CHECK_OBJ_NOTNULL(c, CLIENT_MAGIC);
	vtc_log(c->vl, 2, "Waiting for client");
	PTOK(pthread_join(c->tp, &res));
	if (res != NULL)
		vtc_fatal(c->vl, "Client returned \"%s\"", (char *)res);
	c->tp = 0;
	c->running = 0;
}

/**********************************************************************
 * Run the client thread
 */

static void
client_run(struct client *c)
{

	client_start(c);
	client_wait(c);
}


/**********************************************************************
 * Client command dispatch
 */

void
cmd_client(CMD_ARGS)
{
	struct client *c, *c2;

	(void)priv;

	if (av == NULL) {
		/* Reset and free */
		VTAILQ_FOREACH_SAFE(c, &clients, list, c2) {
			VTAILQ_REMOVE(&clients, c, list);
			if (c->tp != 0)
				client_wait(c);
			client_delete(c);
		}
		return;
	}

	AZ(strcmp(av[0], "client"));
	av++;

	VTC_CHECK_NAME(vl, av[0], "Client", 'c');
	VTAILQ_FOREACH(c, &clients, list)
		if (!strcmp(c->name, av[0]))
			break;
	if (c == NULL)
		c = client_new(av[0]);
	av++;

	for (; *av != NULL; av++) {
		if (vtc_error)
			break;

		if (!strcmp(*av, "-wait")) {
			client_wait(c);
			continue;
		}

		/* Don't muck about with a running client */
		if (c->running)
			client_wait(c);

		AZ(c->running);
		if (Sess_GetOpt(c->vsp, &av))
			continue;

		if (!strcmp(*av, "-connect")) {
			bprintf(c->connect, "%s", av[1]);
			av++;
			continue;
		}
		if (!strcmp(*av, "-proxy1")) {
			REPLACE(c->proxy_spec, av[1]);
			c->proxy_version = 1;
			av++;
			continue;
		}
		if (!strcmp(*av, "-proxy2")) {
			REPLACE(c->proxy_spec, av[1]);
			c->proxy_version = 2;
			av++;
			continue;
		}
		if (!strcmp(*av, "-start")) {
			client_start(c);
			continue;
		}
		if (!strcmp(*av, "-run")) {
			client_run(c);
			continue;
		}
		if (**av == '-')
			vtc_fatal(c->vl, "Unknown client argument: %s", *av);
		REPLACE(c->spec, *av);
	}
}
