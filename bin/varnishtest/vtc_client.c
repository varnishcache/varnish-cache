/*
 * Copyright (c) 2006-2008 Linpro AS
 * All rights reserved.
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
 *
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/socket.h>

#include "vtc.h"

#include "vqueue.h"
#include "miniobj.h"
#include "vss.h"
#include "libvarnish.h"

struct client {
	unsigned		magic;
#define CLIENT_MAGIC		0x6242397c
	char			*name;
	VTAILQ_ENTRY(client)	list;

	char			*spec;
	
	const char		*connect;
	int			naddr;
	struct vss_addr		**vss_addr;
	char			*addr;
	char			*port;

	pthread_t		tp;
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
	int i;
	int fd;

	CAST_OBJ_NOTNULL(c, priv, CLIENT_MAGIC);
	assert(c->naddr > 0);

	printf("### Client %s started\n", c->name);
	printf("#### Client %s connect to %s\n", c->name, c->connect);
	for (i = 0; i < c->naddr; i++) {
		fd = VSS_connect(c->vss_addr[i]);
		if (fd >= 0)
			break;
	}
	assert(fd >= 0);
	printf("#### Client %s connected to %s fd is %d\n",
	    c->name, c->connect, fd);
	http_process(c->spec, fd, 1);
	close(fd);
	printf("### Client %s ending\n", c->name);

	return (NULL);
}

/**********************************************************************
 * Allocate and initialize a client
 */

static struct client *
client_new(char *name)
{
	struct client *c;

	ALLOC_OBJ(c, CLIENT_MAGIC);
	c->name = name;
	c->connect = ":8080";
	VTAILQ_INSERT_TAIL(&clients, c, list);
	return (c);
}

/**********************************************************************
 * Start the client thread
 */

static void
client_start(struct client *c)
{

	CHECK_OBJ_NOTNULL(c, CLIENT_MAGIC);
	printf("Starting client %s\n", c->name);
	AZ(pthread_create(&c->tp, NULL, client_thread, c));
}

/**********************************************************************
 * Wait for client thread to stop
 */

static void
client_wait(struct client *c)
{
	void *res;

	CHECK_OBJ_NOTNULL(c, CLIENT_MAGIC);
	printf("Waiting for client %s\n", c->name);
	AZ(pthread_join(c->tp, &res));
	if (res != NULL) {
		fprintf(stderr, "Server %s returned \"%s\"\n",
		    c->name, (char *)res);
		exit (1);
	}
	c->tp = NULL;
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
cmd_client(char **av, void *priv)
{
	struct client *c;

	(void)priv;
	assert(!strcmp(av[0], "client"));
	av++;

	VTAILQ_FOREACH(c, &clients, list)
		if (!strcmp(c->name, av[0]))
			break;
	if (c == NULL) 
		c = client_new(av[0]);
	av++;

	for (; *av != NULL; av++) {
		if (!strcmp(*av, "-connect")) {
			c->connect = av[1];
			av++;
			AZ(VSS_parse(c->connect, &c->addr, &c->port));
			c->naddr = VSS_resolve(c->addr, c->port, &c->vss_addr);
			assert(c->naddr > 0);
			continue;
		}
		if (!strcmp(*av, "-run")) {
			client_run(c);
			continue;
		}
		if (**av == '{') {
			c->spec = *av;
			continue;
		}
		fprintf(stderr, "Unknown client argument: %s\n", *av);
		exit (1);
	}
}
