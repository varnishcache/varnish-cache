/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 * HTTP protocol requests
 *
 * The trouble with the "until magic sequence" design of HTTP protocol messages
 * is that either you have to read a single character at a time, which is
 * inefficient, or you risk reading too much, and pre-read some of the object,
 * or even the next pipelined request, which follows the one you want.
 *
 * HTC reads a HTTP protocol header into a workspace, subject to limits,
 * and stops when we see the magic marker (double [CR]NL), and if we overshoot,
 * it keeps track of the "pipelined" data.
 *
 * We use this both for client and backend connections.
 */

#include "config.h"

#include "cache.h"

#include "vct.h"

/*--------------------------------------------------------------------*/

void
HTTP1_Init(struct http_conn *htc, struct ws *ws, int fd, struct vsl_log *vsl,
    unsigned maxbytes, unsigned maxhdr)
{

	htc->magic = HTTP_CONN_MAGIC;
	htc->ws = ws;
	htc->fd = fd;
	htc->vsl = vsl;
	htc->maxbytes = maxbytes;
	htc->maxhdr = maxhdr;
	htc->read = HTTP1_Read;

	(void)WS_Reserve(htc->ws, htc->maxbytes);
	htc->rxbuf.b = ws->f;
	htc->rxbuf.e = ws->f;
	*htc->rxbuf.e = '\0';
	htc->pipeline.b = NULL;
	htc->pipeline.e = NULL;
}

/*--------------------------------------------------------------------
 * Start over, and recycle any pipelined input.
 * The WS_Reset is safe, even though the pipelined input is stored in
 * the ws somewhere, because WS_Reset only fiddles pointers.
 */

enum htc_status_e
HTTP1_Reinit(struct http_conn *htc)
{
	unsigned l;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	(void)WS_Reserve(htc->ws, htc->maxbytes);
	htc->rxbuf.b = htc->ws->f;
	htc->rxbuf.e = htc->ws->f;
	if (htc->pipeline.b != NULL) {
		l = Tlen(htc->pipeline);
		memmove(htc->rxbuf.b, htc->pipeline.b, l);
		htc->rxbuf.e += l;
		htc->pipeline.b = NULL;
		htc->pipeline.e = NULL;
	}
	*htc->rxbuf.e = '\0';
	return (HTTP1_Complete(htc));
}

/*--------------------------------------------------------------------
 * Check if we have a complete HTTP request or response yet
 *
 */

enum htc_status_e
HTTP1_Complete(struct http_conn *htc)
{
	int i;
	const char *p;
	txt *t;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);

	t = &htc->rxbuf;
	Tcheck(*t);
	assert(*t->e == '\0');

	/* Skip any leading white space */
	for (p = t->b ; vct_islws(*p); p++)
		continue;
	if (p == t->e) {
		/* All white space */
		t->e = t->b;
		*t->e = '\0';
		return (HTTP1_ALL_WHITESPACE);
	}
	while (1) {
		p = strchr(p, '\n');
		if (p == NULL)
			return (HTTP1_NEED_MORE);
		p++;
		if (*p == '\r')
			p++;
		if (*p == '\n')
			break;
	}
	p++;
	i = p - t->b;
	WS_ReleaseP(htc->ws, htc->rxbuf.e);
	AZ(htc->pipeline.b);
	AZ(htc->pipeline.e);
	if (htc->rxbuf.b + i < htc->rxbuf.e) {
		htc->pipeline.b = htc->rxbuf.b + i;
		htc->pipeline.e = htc->rxbuf.e;
		htc->rxbuf.e = htc->pipeline.b;
	}
	return (HTTP1_COMPLETE);
}

/*--------------------------------------------------------------------
 * Receive more HTTP protocol bytes
 */

enum htc_status_e
HTTP1_Rx(struct http_conn *htc)
{
	int i;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	AN(htc->ws->r);
	i = (htc->ws->r - htc->rxbuf.e) - 1;	/* space for NUL */
	if (i <= 0) {
		WS_ReleaseP(htc->ws, htc->rxbuf.b);
		return (HTTP1_OVERFLOW);
	}
	i = read(htc->fd, htc->rxbuf.e, i);
	if (i <= 0) {
		/*
		 * We wouldn't come here if we had a complete HTTP header
		 * so consequently an EOF can not be OK
		 */
		WS_ReleaseP(htc->ws, htc->rxbuf.b);
		return (HTTP1_ERROR_EOF);
	}
	htc->rxbuf.e += i;
	*htc->rxbuf.e = '\0';
	return (HTTP1_Complete(htc));
}

/*--------------------------------------------------------------------
 * Read up to len bytes, returning pipelined data first.
 */

ssize_t
HTTP1_Read(struct http_conn *htc, void *d, size_t len)
{
	size_t l;
	unsigned char *p;
	ssize_t i;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	l = 0;
	p = d;
	if (htc->pipeline.b) {
		l = Tlen(htc->pipeline);
		if (l > len)
			l = len;
		memcpy(p, htc->pipeline.b, l);
		p += l;
		len -= l;
		htc->pipeline.b += l;
		if (htc->pipeline.b == htc->pipeline.e)
			htc->pipeline.b = htc->pipeline.e = NULL;
	}
	if (len == 0)
		return (l);
	i = read(htc->fd, p, len);
	if (i < 0) {
		VSLb(htc->vsl, SLT_FetchError, "%s", strerror(errno));
		return (i);
	}
	return (i + l);
}

/*--------------------------------------------------------------------
 * Dissect the headers of the HTTP protocol message.
 * Detect conditionals (headers which start with '^[Ii][Ff]-')
 */

static uint16_t
htc_dissect_hdrs(struct http *hp, char *p, const struct http_conn *htc)
{
	char *q, *r;
	txt t = htc->rxbuf;

	if (*p == '\r')
		p++;

	hp->nhd = HTTP_HDR_FIRST;
	hp->conds = 0;
	r = NULL;		/* For FlexeLint */
	for (; p < t.e; p = r) {

		/* Find end of next header */
		q = r = p;
		while (r < t.e) {
			if (!vct_iscrlf(*r)) {
				r++;
				continue;
			}
			q = r;
			assert(r < t.e);
			r += vct_skipcrlf(r);
			if (r >= t.e)
				break;
			/* If line does not continue: got it. */
			if (!vct_issp(*r))
				break;

			/* Clear line continuation LWS to spaces */
			while (vct_islws(*q))
				*q++ = ' ';
		}

		if (q - p > htc->maxhdr) {
			VSLb(hp->vsl, SLT_BogoHeader, "%.*s",
			    (int)(q - p > 20 ? 20 : q - p), p);
			return (413);
		}

		/* Empty header = end of headers */
		if (p == q)
			break;

		if ((p[0] == 'i' || p[0] == 'I') &&
		    (p[1] == 'f' || p[1] == 'F') &&
		    p[2] == '-')
			hp->conds = 1;

		while (q > p && vct_issp(q[-1]))
			q--;
		*q = '\0';

		if (hp->nhd < hp->shd) {
			hp->hdf[hp->nhd] = 0;
			hp->hd[hp->nhd].b = p;
			hp->hd[hp->nhd].e = q;
			http_VSLH(hp, hp->nhd);
			hp->nhd++;
		} else {
			VSLb(hp->vsl, SLT_BogoHeader, "%.*s",
			    (int)(q - p > 20 ? 20 : q - p), p);
			return (413);
		}
	}
	return (0);
}

/*--------------------------------------------------------------------
 * Deal with first line of HTTP protocol message.
 */

static uint16_t
htc_splitline(struct http *hp, const struct http_conn *htc, int req)
{
	char *p, *q;
	int h1, h2, h3;

	if (req) {
		h1 = HTTP_HDR_METHOD;
		h2 = HTTP_HDR_URL;
		h3 = HTTP_HDR_PROTO;
	} else {
		h1 = HTTP_HDR_PROTO;
		h2 = HTTP_HDR_STATUS;
		h3 = HTTP_HDR_RESPONSE;
	}

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

	/* XXX: Assert a NUL at rx.e ? */
	Tcheck(htc->rxbuf);

	/* Skip leading LWS */
	for (p = htc->rxbuf.b ; vct_islws(*p); p++)
		continue;

	/* First field cannot contain SP, CRLF or CTL */
	q = p;
	for (; !vct_issp(*p); p++) {
		if (vct_isctl(*p))
			return (400);
	}
	hp->hd[h1].b = q;
	hp->hd[h1].e = p;

	/* Skip SP */
	for (; vct_issp(*p); p++) {
		if (vct_isctl(*p))
			return (400);
	}

	/* Second field cannot contain LWS or CTL */
	q = p;
	for (; !vct_islws(*p); p++) {
		if (vct_isctl(*p))
			return (400);
	}
	hp->hd[h2].b = q;
	hp->hd[h2].e = p;

	if (!Tlen(hp->hd[h2]))
		return (413);

	/* Skip SP */
	for (; vct_issp(*p); p++) {
		if (vct_isctl(*p))
			return (400);
	}

	/* Third field is optional and cannot contain CTL */
	q = p;
	if (!vct_iscrlf(*p)) {
		for (; !vct_iscrlf(*p); p++)
			if (!vct_issep(*p) && vct_isctl(*p))
				return (400);
	}
	hp->hd[h3].b = q;
	hp->hd[h3].e = p;

	/* Skip CRLF */
	p += vct_skipcrlf(p);

	*hp->hd[h1].e = '\0';
	http_VSLH(hp, h1);

	*hp->hd[h2].e = '\0';
	http_VSLH(hp, h2);

	if (hp->hd[h3].e != NULL) {
		*hp->hd[h3].e = '\0';
		http_VSLH(hp, h3);
	}

	return (htc_dissect_hdrs(hp, p, htc));
}

/*--------------------------------------------------------------------*/

static void
htc_proto_ver(struct http *hp)
{
	if (!strcasecmp(hp->hd[HTTP_HDR_PROTO].b, "HTTP/1.0"))
		hp->protover = 10;
	else if (!strcasecmp(hp->hd[HTTP_HDR_PROTO].b, "HTTP/1.1"))
		hp->protover = 11;
	else
		hp->protover = 9;
}

/*--------------------------------------------------------------------*/

uint16_t
HTTP1_DissectRequest(struct req *req)
{
	struct http_conn *htc;
	struct http *hp;
	uint16_t retval;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	htc = req->htc;
	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	hp = req->http;
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

	retval = htc_splitline(hp, htc, 1);
	if (retval != 0) {
		VSLbt(req->vsl, SLT_HttpGarbage, htc->rxbuf);
		return (retval);
	}
	htc_proto_ver(hp);
	return (retval);
}
/*--------------------------------------------------------------------*/

uint16_t
HTTP1_DissectResponse(struct http *hp, const struct http_conn *htc)
{
	int j;
	uint16_t retval = 0;
	char *p;


	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

	if (htc_splitline(hp, htc, 0))
		retval = 503;

	if (retval == 0 && memcmp(hp->hd[HTTP_HDR_PROTO].b, "HTTP/1.", 7))
		retval = 503;

	if (retval == 0 && Tlen(hp->hd[HTTP_HDR_STATUS]) != 3)
		retval = 503;

	if (retval == 0) {
		hp->status = 0;
		p = hp->hd[HTTP_HDR_STATUS].b;
		for (j = 100; j != 0; j /= 10) {
			if (!vct_isdigit(*p)) {
				retval = 503;
				break;
			}
			hp->status += (uint16_t)(j * (*p - '0'));
			p++;
		}
		if (*p != '\0')
			retval = 503;
	}

	if (retval != 0) {
		VSLbt(hp->vsl, SLT_HttpGarbage, htc->rxbuf);
		assert(retval >= 100 && retval <= 999);
		hp->status = retval;
	} else
		htc_proto_ver(hp);

	if (hp->hd[HTTP_HDR_RESPONSE].b == NULL ||
	    !Tlen(hp->hd[HTTP_HDR_RESPONSE])) {
		/* Backend didn't send a response string, use the standard */
		hp->hd[HTTP_HDR_RESPONSE].b =
		    TRUST_ME(http_StatusMessage(hp->status));
		hp->hd[HTTP_HDR_RESPONSE].e =
		    strchr(hp->hd[HTTP_HDR_RESPONSE].b, '\0');
	}
	return (retval);
}

