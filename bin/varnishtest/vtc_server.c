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

struct server {
	unsigned		magic;
#define SERVER_MAGIC		0x55286619
	char			*name;
	VTAILQ_ENTRY(server)	list;

	unsigned		repeat;
	char			*spec;
	
	int			depth;
	int			sock;
	const char		*listen;
	struct vss_addr		**vss_addr;
	char			*addr;
	char			*port;

	pthread_t		tp;
};

static VTAILQ_HEAD(, server)	servers =
    VTAILQ_HEAD_INITIALIZER(servers);

/**********************************************************************
 * Server thread
 */

static void *
server_thread(void *priv)
{
	struct server *s;
	int i, fd;
	struct sockaddr_storage addr_s;
	struct sockaddr *addr;
	socklen_t l;


	CAST_OBJ_NOTNULL(s, priv, SERVER_MAGIC);
	assert(s->sock >= 0);

	printf("### Server %s started\n", s->name);
	for (i = 0; i < s->repeat; i++) {
		if (s->repeat > 1)
			printf("#### Server %s iteration %d\n", s->name, i);
		addr = (void*)&addr_s;
		l = sizeof addr_s;
		fd = accept(s->sock, addr, &l);
		printf("#### Accepted socket %d\n", fd);
		http_process(s->spec, fd, 0);
		close(fd);
	}
	printf("### Server %s ending\n", s->name);

	return (NULL);
}

/**********************************************************************
 * Allocate and initialize a server
 */

static struct server *
server_new(char *name)
{
	struct server *s;

	ALLOC_OBJ(s, SERVER_MAGIC);
	s->name = name;
	s->listen = ":9080";
	s->repeat = 1;
	s->depth = 1;
	s->sock = -1;
	VTAILQ_INSERT_TAIL(&servers, s, list);
	return (s);
}

/**********************************************************************
 * Start the server thread
 */

static void
server_start(struct server *s)
{
	int naddr;

	CHECK_OBJ_NOTNULL(s, SERVER_MAGIC);
	printf("Starting server %s\n", s->name);
	if (s->sock < 0) {
		AZ(VSS_parse(s->listen, &s->addr, &s->port));
		naddr = VSS_resolve(s->addr, s->port, &s->vss_addr);
		if (naddr != 1) {
			fprintf(stderr,
			    "Server %s listen address not unique\n"
			    "   \"%s\" resolves to (%d) sockets\n",
			    s->name, s->listen, naddr);
			exit (1);
		}
		s->sock = VSS_listen(s->vss_addr[0], s->depth);
		assert(s->sock >= 0);
	}
	printf("#### Server %s listen on %s (fd %d)\n",
	    s->name, s->listen, s->sock);
	AZ(pthread_create(&s->tp, NULL, server_thread, s));
}

/**********************************************************************
 * Wait for server thread to stop
 */

static void
server_wait(struct server *s)
{
	void *res;

	CHECK_OBJ_NOTNULL(s, SERVER_MAGIC);
	printf("Waiting for server %s\n", s->name);
	AZ(pthread_join(s->tp, &res));
	if (res != NULL) {
		fprintf(stderr, "Server %s returned \"%s\"\n",
		    s->name, (char *)res);
		exit (1);
	}
	s->tp = NULL;
}

/**********************************************************************
 * Server command dispatch
 */

void
cmd_server(char **av, void *priv)
{
	struct server *s;

	(void)priv;
	assert(!strcmp(av[0], "server"));
	av++;

	VTAILQ_FOREACH(s, &servers, list)
		if (!strcmp(s->name, av[0]))
			break;
	if (s == NULL) 
		s = server_new(av[0]);
	av++;

	for (; *av != NULL; av++) {
		if (!strcmp(*av, "-repeat")) {
			s->repeat = atoi(av[1]);
			av++;
			continue;
		}
		if (!strcmp(*av, "-listen")) {
			s->listen = av[1];
			av++;
			continue;
		}
		if (!strcmp(*av, "-start")) {
			server_start(s);
			continue;
		}
		if (!strcmp(*av, "-wait")) {
			server_wait(s);
			continue;
		}
		if (**av == '{') {
			s->spec = *av;
			continue;
		}
		fprintf(stderr, "Unknown server argument: %s\n", *av);
		exit (1);
	}
}
