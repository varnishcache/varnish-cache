/*
 * Copyright (c) 2008-2009 Linpro AS
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
 *
 * $Id$
 */

#include "config.h"

#include <poll.h>
#include <stdio.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

#include "libvarnish.h"
#include "vct.h"
#include "miniobj.h"
#include "vsb.h"

#include "vtc.h"

#define MAX_HDR		50

struct http {
	unsigned		magic;
#define HTTP_MAGIC		0x2f02169c
	int			fd;
	int			client;
	int			timeout;
	struct vtclog		*vl;

	struct vsb		*vsb;

	int			nrxbuf;
	char			*rxbuf;
	int			prxbuf;
	char			*body;
	char			bodylen[20];

	char			*req[MAX_HDR];
	char			*resp[MAX_HDR];
};

/* XXX: we may want to vary this */
static const char * const nl = "\r\n";

/**********************************************************************
 * Generate a synthetic body
 */

static char *
synth_body(const char *len)
{
	int i, j, k, l;
	char *b;


	AN(len);
	i = strtoul(len, NULL, 0);
	assert(i > 0);
	b = malloc(i + 1);
	AN(b);
	l = k = '!';
	for (j = 0; j < i; j++) {
		if ((j % 64) == 63) {
			b[j] = '\n';
			k++;
			if (k == '~')
				k = '!';
			l = k;
		} else {
			b[j] = l++;
			if (l == '~')
				l = '!';
		}
	}
	b[i - 1] = '\n';
	b[i] = '\0';
	return (b);
}

/**********************************************************************
 * Finish and write the vsb to the fd
 */

static void
http_write(const struct http *hp, int lvl, const char *pfx)
{
	int l;

	vsb_finish(hp->vsb);
	AZ(vsb_overflowed(hp->vsb));
	vtc_dump(hp->vl, lvl, pfx, vsb_data(hp->vsb));
	l = write(hp->fd, vsb_data(hp->vsb), vsb_len(hp->vsb));
	assert(l == vsb_len(hp->vsb));
}

/**********************************************************************
 * find header
 */

static char *
http_find_header(char * const *hh, const char *hdr)
{
	int n, l;
	char *r;

	l = strlen(hdr);

	for (n = 3; hh[n] != NULL; n++) {
		if (strncasecmp(hdr, hh[n], l) || hh[n][l] != ':')
			continue;
		for (r = hh[n] + l + 1; vct_issp(*r); r++)
			continue;
		return (r);
	}
	return (NULL);
}

/**********************************************************************
 * Expect
 */

static char *
cmd_var_resolve(struct http *hp, char *spec)
{
	char **hh, *hdr;

	if (!strcmp(spec, "req.request"))
		return(hp->req[0]);
	if (!strcmp(spec, "req.url"))
		return(hp->req[1]);
	if (!strcmp(spec, "req.proto"))
		return(hp->req[2]);
	if (!strcmp(spec, "resp.proto"))
		return(hp->resp[0]);
	if (!strcmp(spec, "resp.status"))
		return(hp->resp[1]);
	if (!strcmp(spec, "resp.msg"))
		return(hp->resp[2]);
	if (!strcmp(spec, "resp.bodylen"))
		return(hp->bodylen);
	if (!memcmp(spec, "req.http.", 9)) {
		hh = hp->req;
		hdr = spec + 9;
	} else if (!memcmp(spec, "resp.http.", 10)) {
		hh = hp->resp;
		hdr = spec + 10;
	} else
		return (spec);
	hdr = http_find_header(hh, hdr);
	if (hdr != NULL)
		return (hdr);
	return (spec);
}

static void
cmd_http_expect(CMD_ARGS)
{
	struct http *hp;
	char *lhs;
	char *cmp;
	char *rhs;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	assert(!strcmp(av[0], "expect"));
	av++;

	AN(av[0]);
	AN(av[1]);
	AN(av[2]);
	AZ(av[3]);
	lhs = cmd_var_resolve(hp, av[0]);
	cmp = av[1];
	rhs = cmd_var_resolve(hp, av[2]);
	if (!strcmp(cmp, "==")) {
		if (strcmp(lhs, rhs))
			vtc_log(hp->vl, 0, "EXPECT %s (%s) %s %s (%s) failed",
			    av[0], lhs, av[1], av[2], rhs);
		else
			vtc_log(hp->vl, 4, "EXPECT %s (%s) %s %s (%s) match",
			    av[0], lhs, av[1], av[2], rhs);
	} else if (!strcmp(cmp, "!=")) {
		if (!strcmp(lhs, rhs))
			vtc_log(hp->vl, 0, "EXPECT %s (%s) %s %s (%s) failed",
			    av[0], lhs, av[1], av[2], rhs);
		else
			vtc_log(hp->vl, 4, "EXPECT %s (%s) %s %s (%s) match",
			    av[0], lhs, av[1], av[2], rhs);
	} else {
		vtc_log(hp->vl, 0, "EXPECT %s (%s) %s %s (%s) not implemented",
		    av[0], lhs, av[1], av[2], rhs);
	}
}

/**********************************************************************
 * Split a HTTP protocol header
 */

static void
http_splitheader(struct http *hp, int req)
{
	char *p, *q, **hh;
	int n;
	char buf[20];

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	if (req) {
		memset(hp->req, 0, sizeof hp->req);
		hh = hp->req;
	} else {
		memset(hp->resp, 0, sizeof hp->resp);
		hh = hp->resp;
	}

	n = 0;
	p = hp->rxbuf;

	/* REQ/PROTO */
	while (vct_islws(*p))
		p++;
	hh[n++] = p;
	while (!vct_islws(*p))
		p++;
	assert(!vct_iscrlf(*p));
	*p++ = '\0';

	/* URL/STATUS */
	while (vct_issp(*p))		/* XXX: H space only */
		p++;
	assert(!vct_iscrlf(*p));
	hh[n++] = p;
	while (!vct_islws(*p))
		p++;
	if (vct_iscrlf(*p)) {
		hh[n++] = NULL;
		q = p;
		p += vct_skipcrlf(p);
		*q = '\0';
	} else {
		*p++ = '\0';
		/* PROTO/MSG */
		while (vct_issp(*p))		/* XXX: H space only */
			p++;
		hh[n++] = p;
		while (!vct_iscrlf(*p))
			p++;
		q = p;
		p += vct_skipcrlf(p);
		*q = '\0';
	}
	assert(n == 3);

	while (*p != '\0') {
		assert(n < MAX_HDR);
		if (vct_iscrlf(*p))
			break;
		hh[n++] = p++;
		while (*p != '\0' && !vct_iscrlf(*p))
			p++;
		q = p;
		p += vct_skipcrlf(p);
		*q = '\0';
	}
	p += vct_skipcrlf(p);
	assert(*p == '\0');

	for (n = 0; n < 3 || hh[n] != NULL; n++) {
		sprintf(buf, "http[%2d] ", n);
		vtc_dump(hp->vl, 4, buf, hh[n]);
	}
}


/**********************************************************************
 * Receive another character
 */

static int
http_rxchar_eof(struct http *hp, int n)
{
	int i;
	struct pollfd pfd[1];

	while (n > 0) {
		pfd[0].fd = hp->fd;
		pfd[0].events = POLLIN;
		pfd[0].revents = 0;
		i = poll(pfd, 1, hp->timeout);
		if (i <= 0)
			vtc_log(hp->vl, 0, "HTTP rx failed (%s)",
			    strerror(errno));
		assert(i > 0);
		assert(hp->prxbuf + n < hp->nrxbuf);
		i = read(hp->fd, hp->rxbuf + hp->prxbuf, n);
		if (i == 0)
			return (i);
		assert(i > 0);
		hp->prxbuf += i;
		hp->rxbuf[hp->prxbuf] = '\0';
		n -= i;
	}
	return (1);
}

static void
http_rxchar(struct http *hp, int n)
{
	int i;

	i = http_rxchar_eof(hp, n);
	if (i <= 0)
		vtc_log(hp->vl, 0, "HTTP rx failed (%s)", strerror(errno));
	assert(i > 0);
}

/**********************************************************************
 * Swallow a HTTP message body
 */

static void
http_swallow_body(struct http *hp, char * const *hh, int body)
{
	char *p, *q;
	int i, l, ll;


	ll = 0;
	p = http_find_header(hh, "content-length");
	if (p != NULL) {
		l = strtoul(p, NULL, 0);
		hp->body = hp->rxbuf + hp->prxbuf;
		http_rxchar(hp, l);
		vtc_dump(hp->vl, 4, "body", hp->body);
		sprintf(hp->bodylen, "%d", l);
		return;
	}
	p = http_find_header(hh, "transfer-encoding");
	if (p != NULL && !strcmp(p, "chunked")) {
		hp->body = hp->rxbuf + hp->prxbuf;
		while (1) {
			l = hp->prxbuf;
			do
				http_rxchar(hp, 1);
			while (hp->rxbuf[hp->prxbuf - 1] != '\n');
			vtc_dump(hp->vl, 4, "len", hp->rxbuf + l);
			i = strtoul(hp->rxbuf + l, &q, 16);
			assert(q != hp->rxbuf + l);
			assert(*q == '\0' || vct_islws(*q));
			hp->prxbuf = l;
			if (i > 0) {
				ll += i;
				http_rxchar(hp, i);
				vtc_dump(hp->vl, 4, "chunk", hp->rxbuf + l);
			}
			l = hp->prxbuf;
			http_rxchar(hp, 2);
			assert(vct_iscrlf(hp->rxbuf[l]));
			assert(vct_iscrlf(hp->rxbuf[l + 1]));
			hp->prxbuf = l;
			hp->rxbuf[l] = '\0';
			if (i == 0)
				break;
		}
		vtc_dump(hp->vl, 4, "body", hp->body);
		sprintf(hp->bodylen, "%d", ll);
		return;
	}
	if (body) {
		hp->body = hp->rxbuf + hp->prxbuf;
		do  {
			i = http_rxchar_eof(hp, 1);
			ll += i;
		} while (i > 0);
		vtc_dump(hp->vl, 4, "rxeof", hp->body);
	}
	sprintf(hp->bodylen, "%d", ll);
}

/**********************************************************************
 * Receive a HTTP protocol header
 */

static void
http_rxhdr(struct http *hp)
{
	int i;
	char *p;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	hp->prxbuf = 0;
	hp->body = NULL;
	while (1) {
		http_rxchar(hp, 1);
		p = hp->rxbuf + hp->prxbuf - 1;
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
	vtc_dump(hp->vl, 4, "rxhdr", hp->rxbuf);
}


/**********************************************************************
 * Receive a response
 */

static void
cmd_http_rxresp(CMD_ARGS)
{
	struct http *hp;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	AN(hp->client);
	assert(!strcmp(av[0], "rxresp"));
	av++;

	for(; *av != NULL; av++)
		vtc_log(hp->vl, 0, "Unknown http rxresp spec: %s\n", *av);
	vtc_log(hp->vl, 3, "rxresp");
	http_rxhdr(hp);
	http_splitheader(hp, 0);
	if (!strcmp(hp->resp[1], "200"))
		http_swallow_body(hp, hp->resp, 1);
	else
		http_swallow_body(hp, hp->resp, 0);
	vtc_log(hp->vl, 4, "bodylen = %s", hp->bodylen);
}

/**********************************************************************
 * Transmit a response
 */

static void
cmd_http_txresp(CMD_ARGS)
{
	struct http *hp;
	const char *proto = "HTTP/1.1";
	const char *status = "200";
	const char *msg = "Ok";
	int bodylen;
	char *b, *c;
	char *body = NULL;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	AZ(hp->client);
	assert(!strcmp(av[0], "txresp"));
	av++;

	vsb_clear(hp->vsb);

	for(; *av != NULL; av++) {
		if (!strcmp(*av, "-proto")) {
			proto = av[1];
			av++;
		} else if (!strcmp(*av, "-status")) {
			status = av[1];
			av++;
		} else if (!strcmp(*av, "-msg")) {
			msg = av[1];
			av++;
			continue;
		} else
			break;
	}

	vsb_printf(hp->vsb, "%s %s %s%s", proto, status, msg, nl);

	for(; *av != NULL; av++) {
		if (!strcmp(*av, "-hdr")) {
			vsb_printf(hp->vsb, "%s%s", av[1], nl);
			av++;
		} else
			break;
	}
	for(; *av != NULL; av++) {
		if (!strcmp(*av, "-body")) {
			AZ(body);
			REPLACE(body, av[1]);
			av++;
			bodylen = strlen(body);
			for (b = body; *b != '\0'; b++) {
				if(*b == '\\' && b[1] == '0') {
					*b = '\0';
					for(c = b+1; *c != '\0'; c++) {
						*c = c[1];
					}
					b++;
					bodylen--;
				}
			}
		} else if (!strcmp(*av, "-bodylen")) {
			AZ(body);
			body = synth_body(av[1]);
			bodylen = strlen(body);
			av++;
		} else
			break;
	}
	if (*av != NULL)
		vtc_log(hp->vl, 0, "Unknown http txresp spec: %s\n", *av);
	if (body != NULL)
		vsb_printf(hp->vsb, "Content-Length: %d%s", bodylen, nl);
	vsb_cat(hp->vsb, nl);
	if (body != NULL)
		vsb_bcat(hp->vsb, body, bodylen);
	http_write(hp, 4, "txresp");
}

/**********************************************************************
 * Receive a request
 */

static void
cmd_http_rxreq(CMD_ARGS)
{
	struct http *hp;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	AZ(hp->client);
	assert(!strcmp(av[0], "rxreq"));
	av++;

	for(; *av != NULL; av++)
		vtc_log(hp->vl, 0, "Unknown http rxreq spec: %s\n", *av);
	vtc_log(hp->vl, 3, "rxreq");
	http_rxhdr(hp);
	http_splitheader(hp, 1);
	http_swallow_body(hp, hp->req, 0);
	vtc_log(hp->vl, 4, "bodylen = %s", hp->bodylen);
}

static void
cmd_http_rxhdrs(CMD_ARGS)
{
	struct http *hp;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	AZ(hp->client);
	assert(!strcmp(av[0], "rxhdrs"));
	av++;

	for(; *av != NULL; av++)
		vtc_log(hp->vl, 0, "Unknown http rxreq spec: %s\n", *av);
	vtc_log(hp->vl, 3, "rxhdrs");
	http_rxhdr(hp);
	http_splitheader(hp, 1);
}

static void
cmd_http_rxbody(CMD_ARGS)
{
	struct http *hp;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	AZ(hp->client);
	assert(!strcmp(av[0], "rxbody"));
	av++;

	for(; *av != NULL; av++)
		vtc_log(hp->vl, 0, "Unknown http rxreq spec: %s\n", *av);
	vtc_log(hp->vl, 3, "rxbody");
	http_swallow_body(hp, hp->req, 0);
	vtc_log(hp->vl, 4, "bodylen = %s", hp->bodylen);
}

/**********************************************************************
 * Transmit a request
 */

static void
cmd_http_txreq(CMD_ARGS)
{
	struct http *hp;
	const char *req = "GET";
	const char *url = "/";
	const char *proto = "HTTP/1.1";
	const char *body = NULL;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	AN(hp->client);
	assert(!strcmp(av[0], "txreq"));
	av++;

	vsb_clear(hp->vsb);

	for(; *av != NULL; av++) {
		if (!strcmp(*av, "-url")) {
			url = av[1];
			av++;
		} else if (!strcmp(*av, "-proto")) {
			proto = av[1];
			av++;
		} else if (!strcmp(*av, "-req")) {
			req = av[1];
			av++;
		} else
			break;
	}
	vsb_printf(hp->vsb, "%s %s %s%s", req, url, proto, nl);
	for(; *av != NULL; av++) {
		if (!strcmp(*av, "-hdr")) {
			vsb_printf(hp->vsb, "%s%s", av[1], nl);
			av++;
		} else
			break;
	}
	for(; *av != NULL; av++) {
		if (!strcmp(*av, "-body")) {
			AZ(body);
			body = av[1];
			av++;
		} else if (!strcmp(*av, "-bodylen")) {
			AZ(body);
			body = synth_body(av[1]);
			av++;
		} else
			break;
	}
	if (*av != NULL)
		vtc_log(hp->vl, 0, "Unknown http txreq spec: %s\n", *av);
	if (body != NULL)
		vsb_printf(hp->vsb, "Content-Length: %d%s", strlen(body), nl);
	vsb_cat(hp->vsb, nl);
	if (body != NULL) {
		vsb_cat(hp->vsb, body);
		vsb_cat(hp->vsb, nl);
	}
	http_write(hp, 4, "txreq");
}

/**********************************************************************
 * Send a string
 */

static void
cmd_http_send(CMD_ARGS)
{
	struct http *hp;
	int i;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	AN(av[1]);
	AZ(av[2]);
	vtc_dump(hp->vl, 4, "send", av[1]);
	i = write(hp->fd, av[1], strlen(av[1]));
	assert(i == strlen(av[1]));

}

/**********************************************************************
 * Send a string as chunked encoding
 */

static void
cmd_http_chunked(CMD_ARGS)
{
	struct http *hp;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	AN(av[1]);
	AZ(av[2]);
	vsb_clear(hp->vsb);
	vsb_printf(hp->vsb, "%x%s%s%s", strlen(av[1]), nl, av[1], nl);
	http_write(hp, 4, "chunked");
}

/**********************************************************************
 * set the timeout
 */

static void
cmd_http_timeout(CMD_ARGS)
{
	struct http *hp;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	AN(av[1]);
	AZ(av[2]);
	hp->timeout = (int)(strtod(av[1], NULL) * 1000.0);
}

/**********************************************************************
 * Execute HTTP specifications
 */

static const struct cmds http_cmds[] = {
	{ "timeout",	cmd_http_timeout },
	{ "txreq",	cmd_http_txreq },

	{ "rxreq",	cmd_http_rxreq },
	{ "rxhdrs",	cmd_http_rxhdrs },
	{ "rxbody",	cmd_http_rxbody },

	{ "txresp",	cmd_http_txresp },
	{ "rxresp",	cmd_http_rxresp },
	{ "expect",	cmd_http_expect },
	{ "send",	cmd_http_send },
	{ "chunked",	cmd_http_chunked },
	{ "delay",	cmd_delay },
	{ "sema",	cmd_sema },
	{ NULL,		NULL }
};

void
http_process(struct vtclog *vl, const char *spec, int sock, int client)
{
	struct http *hp;
	char *s, *q;

	ALLOC_OBJ(hp, HTTP_MAGIC);
	AN(hp);
	hp->fd = sock;
	hp->vl = vl;
	hp->client = client;
	hp->timeout = 3000;
	hp->nrxbuf = 640*1024;
	hp->vsb = vsb_newauto();
	hp->rxbuf = malloc(hp->nrxbuf);		/* XXX */
	AN(hp->rxbuf);
	AN(hp->vsb);

	s = strdup(spec);
	q = strchr(s, '\0');
	assert(q > s);
	AN(s);
	parse_string(s, http_cmds, hp, vl);
	vsb_delete(hp->vsb);
	free(hp->rxbuf);
	free(hp);
}
