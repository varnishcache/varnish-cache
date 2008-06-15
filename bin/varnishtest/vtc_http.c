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
};

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
		fprintf(stderr, "Unknown http spec: %s\n", *av);
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
	{ "rxreq",	cmd_dump },
	{ "txresponse",	cmd_dump },
	{ "rxresponse",	cmd_dump },
	{ "expect",	cmd_dump },
	{ NULL,		NULL }
};

void
http_process(const char *spec, int sock, int client)
{
	struct http *hp;
	char *s, *q;

	ALLOC_OBJ(hp, HTTP_MAGIC);
	hp->fd = sock;
	hp->client = client;

	s = strdup(spec + 1);
	q = strchr(s, '\0');
	assert(q > s);
	q--;
	assert(*q == '}');
	*q = '\0';
	AN(s);
	parse_string(s, http_cmds, hp);
}
