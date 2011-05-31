/*
 * Copyright (c) 2008-2010 Redpill Linpro AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>

#include "vtc.h"

#include "vsb.h"
#include "vqueue.h"
#include "miniobj.h"
#include "vss.h"
#include "libvarnish.h"

struct client {
	unsigned		magic;
#define CLIENT_MAGIC		0x6242397c
	char			*name;
	struct vtclog		*vl;
	VTAILQ_ENTRY(client)	list;

	char			*spec;

	char			connect[256];

	unsigned		repeat;

	pthread_t		tp;
	unsigned		running;
};

static VTAILQ_HEAD(, client)	clients =
    VTAILQ_HEAD_INITIALIZER(clients);

/**********************************************************************
 * Server thread
 */

static void *
client_thread(void *priv)
{
	struct client *c;
	struct vtclog *vl;
	int fd;
	unsigned u;
	struct vsb *vsb;
	char *p;
	char mabuf[32], mpbuf[32];

	CAST_OBJ_NOTNULL(c, priv, CLIENT_MAGIC);
	AN(*c->connect);

	vl = vtc_logopen(c->name);

	p = strdup(c->connect);
	AN(p);
	vsb = macro_expand(vl, p);
	AN(vsb);

	if (c->repeat == 0)
		c->repeat = 1;
	if (c->repeat != 1)
		vtc_log(vl, 2, "Started (%u iterations)", c->repeat);
	for (u = 0; u < c->repeat; u++) {
		vtc_log(vl, 3, "Connect to %s", VSB_data(vsb));
		fd = VSS_open(VSB_data(vsb), 10.);
		if (fd < 0)
			vtc_log(c->vl, 0, "Failed to open %s", VSB_data(vsb));
		assert(fd >= 0);
		TCP_blocking(fd);
		TCP_myname(fd, mabuf, sizeof mabuf, mpbuf, sizeof mpbuf);
		vtc_log(vl, 3, "connected fd %d from %s %s to %s",
		    fd, mabuf, mpbuf, VSB_data(vsb));
		http_process(vl, c->spec, fd, -1);
		vtc_log(vl, 3, "closing fd %d", fd);
		TCP_close(&fd);
	}
	vtc_log(vl, 2, "Ending");
	VSB_delete(vsb);
	free(p);
	return (NULL);
}

/**********************************************************************
 * Allocate and initialize a client
 */

static struct client *
client_new(const char *name)
{
	struct client *c;

	AN(name);
	ALLOC_OBJ(c, CLIENT_MAGIC);
	AN(c);
	REPLACE(c->name, name);
	c->vl = vtc_logopen(name);
	AN(c->vl);
	if (*c->name != 'c')
		vtc_log(c->vl, 0, "Client name must start with 'c'");

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
	vtc_logclose(c->vl);
	free(c->spec);
	free(c->name);
	/* XXX: MEMLEAK (?)*/
	FREE_OBJ(c);
}

/**********************************************************************
 * Start the client thread
 */

static void
client_start(struct client *c)
{

	CHECK_OBJ_NOTNULL(c, CLIENT_MAGIC);
	vtc_log(c->vl, 2, "Starting client");
	AZ(pthread_create(&c->tp, NULL, client_thread, c));
	c->running = 1;
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
	AZ(pthread_join(c->tp, &res));
	if (res != NULL)
		vtc_log(c->vl, 0, "Client returned \"%s\"", (char *)res);
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
 * Server command dispatch
 */

void
cmd_client(CMD_ARGS)
{
	struct client *c, *c2;

	(void)priv;
	(void)cmd;
	(void)vl;

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

	assert(!strcmp(av[0], "client"));
	av++;

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

		if (!strcmp(*av, "-connect")) {
			bprintf(c->connect, "%s", av[1]);
			av++;
			continue;
		}
		if (!strcmp(*av, "-repeat")) {
			c->repeat = atoi(av[1]);
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
			vtc_log(c->vl, 0, "Unknown client argument: %s", *av);
		REPLACE(c->spec, *av);
	}
}
