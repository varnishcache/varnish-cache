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


#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "libvarnish.h"
#include "miniobj.h"
#include "vsb.h"

#include "vtc.h"


struct http {
	unsigned		magic;
#define HTTP_MAGIC		0x2f02169c
	int			fd;
	int			client;
	int			timeout;

	int			nrxbuf;
	char			*rxbuf;

	char			*req;
	char			*resp;
};

/**********************************************************************
 * Receive a HTTP protocol header
 */

static void
http_rxhdr(struct http *hp)
{
	int l, n, i;
	struct pollfd pfd[1];
	char *p;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	hp->rxbuf = malloc(hp->nrxbuf);		/* XXX */
	AN(hp->rxbuf);
	l = 0;
	while (1) {
		pfd[0].fd = hp->fd;
		pfd[0].events = POLLRDNORM;
		pfd[0].revents = 0;
		i = poll(pfd, 1, hp->timeout);
		assert(i > 0);
		assert(l < hp->nrxbuf);
		n = read(hp->fd, hp->rxbuf + l, 1);
		assert(n == 1);
		l += n;
		hp->rxbuf[l] = '\0';
		assert(n > 0);
		p = hp->rxbuf + l - 1;
		i = 0;
		for (i = 0; p > hp->rxbuf; p--) {
			if (*p != '\n') 
				break;
			if (p - 1 > hp->rxbuf && p[-1] == '\r')
				p--;
			if (++i == 2)
				break;
		}
		if (i == 2)
			break;
	}
printf("<<<%s>>>\n", hp->rxbuf);
}


/**********************************************************************
 * Receive a response
 */

static void
cmd_http_rxresp(char **av, void *priv)
{
	struct http *hp;

	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	AN(hp->client);
	assert(!strcmp(av[0], "rxresp"));
	av++;

	for(; *av != NULL; av++) {
		fprintf(stderr, "Unknown http rxresp spec: %s\n", *av);
		exit (1);
	}
	http_rxhdr(hp);
	hp->resp = hp->rxbuf;
}

/**********************************************************************
 * Transmit a response
 */

static void
cmd_http_txresp(char **av, void *priv)
{
	struct http *hp;
	struct vsb *vsb;
	const char *proto = "HTTP/1.1";
	const char *status = "200";
	const char *msg = "Ok";
	const char *body = NULL;
	int dohdr = 0;
	const char *nl = "\r\n";
	int l;

	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	AZ(hp->client);
	assert(!strcmp(av[0], "txresp"));
	av++;

	vsb = vsb_new(NULL, NULL, 0, VSB_AUTOEXTEND);

	for(; *av != NULL; av++) {
		if (!strcmp(*av, "-proto")) {
			AZ(dohdr);
			proto = av[1];
			av++;
			continue;
		}
		if (!strcmp(*av, "-status")) {
			AZ(dohdr);
			status = av[1];
			av++;
			continue;
		}
		if (!strcmp(*av, "-msg")) {
			AZ(dohdr);
			msg = av[1];
			av++;
			continue;
		}
		if (!strcmp(*av, "-body")) {
			body = av[1];
			av++;
			continue;
		}
		if (!strcmp(*av, "-hdr")) {
			if (dohdr == 0) {
				vsb_printf(vsb, "%s %s %s%s", 
				    proto, status, msg, nl);
				dohdr = 1;
			}
			vsb_printf(vsb, "%s%s", av[1], nl);
			av++;
			continue;
		}
		fprintf(stderr, "Unknown http txreq spec: %s\n", *av);
		exit (1);
	}
	if (dohdr == 0) {
		vsb_printf(vsb, "%s %s %s%s", 
		    proto, status, msg, nl);
		dohdr = 1;
	}
	vsb_cat(vsb, nl);
	if (body != NULL) {
		vsb_cat(vsb, body);
		vsb_cat(vsb, nl);
	}
	vsb_finish(vsb);
	AZ(vsb_overflowed(vsb));
	l = write(hp->fd, vsb_data(vsb), vsb_len(vsb));
	assert(l == vsb_len(vsb));
	vsb_delete(vsb);
}

/**********************************************************************
 * Receive a request
 */

static void
cmd_http_rxreq(char **av, void *priv)
{
	struct http *hp;

	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	AZ(hp->client);
	assert(!strcmp(av[0], "rxreq"));
	av++;

	for(; *av != NULL; av++) {
		fprintf(stderr, "Unknown http rxreq spec: %s\n", *av);
		exit (1);
	}
	http_rxhdr(hp);
	hp->req = hp->rxbuf;
}

/**********************************************************************
 * Transmit a request
 */

static void
cmd_http_txreq(char **av, void *priv)
{
	struct http *hp;
	struct vsb *vsb;
	const char *req = "GET";
	const char *url = "/";
	const char *proto = "HTTP/1.1";
	int dohdr = 0;
	const char *nl = "\r\n";
	int l;

	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	AN(hp->client);
	assert(!strcmp(av[0], "txreq"));
	av++;

	vsb = vsb_new(NULL, NULL, 0, VSB_AUTOEXTEND);

	for(; *av != NULL; av++) {
		if (!strcmp(*av, "-url")) {
			AZ(dohdr);
			url = av[1];
			av++;
			continue;
		}
		if (!strcmp(*av, "-proto")) {
			AZ(dohdr);
			proto = av[1];
			av++;
			continue;
		}
		if (!strcmp(*av, "-req")) {
			AZ(dohdr);
			req = av[1];
			av++;
			continue;
		}
		if (!strcmp(*av, "-hdr")) {
			if (dohdr == 0) {
				vsb_printf(vsb, "%s %s %s%s", 
				    req, url, proto, nl);
				dohdr = 1;
			}
			vsb_printf(vsb, "%s%s", av[1], nl);
			av++;
			continue;
		}
		fprintf(stderr, "Unknown http txreq spec: %s\n", *av);
		exit (1);
	}
	if (dohdr == 0) {
		vsb_printf(vsb, "%s %s %s%s", 
		    req, url, proto, nl);
		dohdr = 1;
	}
	vsb_cat(vsb, nl);
	vsb_finish(vsb);
	AZ(vsb_overflowed(vsb));
	l = write(hp->fd, vsb_data(vsb), vsb_len(vsb));
	assert(l == vsb_len(vsb));
	vsb_delete(vsb);
}

/**********************************************************************
 * Execute HTTP specifications
 */

static struct cmds http_cmds[] = {
	{ "txreq",	cmd_http_txreq },
	{ "rxreq",	cmd_http_rxreq },
	{ "txresp",	cmd_http_txresp },
	{ "rxresp",	cmd_http_rxresp },
	{ "expect",	cmd_dump },
	{ NULL,		NULL }
};

void
http_process(const char *spec, int sock, int client)
{
	struct http *hp;
	char *s, *q;

	ALLOC_OBJ(hp, HTTP_MAGIC);
	AN(hp);
	hp->fd = sock;
	hp->client = client;
	hp->timeout = 1000;
	hp->nrxbuf = 8192;

	s = strdup(spec + 1);
	q = strchr(s, '\0');
	assert(q > s);
	q--;
	assert(*q == '}');
	*q = '\0';
	AN(s);
	parse_string(s, http_cmds, hp);
	free(hp);
}
