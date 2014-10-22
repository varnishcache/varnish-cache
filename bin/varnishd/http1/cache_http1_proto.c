/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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
 * Until we see the magic marker, we have to keep the rxbuf NUL terminated
 * because we use strchr(3) on it.
 *
 * We use this both for client and backend connections.
 */

#include "config.h"

#include "cache/cache.h"

#include "vct.h"

const int HTTP1_Req[3] = {
	HTTP_HDR_METHOD, HTTP_HDR_URL, HTTP_HDR_PROTO
};

const int HTTP1_Resp[3] = {
	HTTP_HDR_PROTO, HTTP_HDR_STATUS, HTTP_HDR_REASON
};

/*--------------------------------------------------------------------*/

void
HTTP1_Init(struct http_conn *htc, struct ws *ws, int fd, unsigned maxbytes,
    unsigned maxhdr)
{

	htc->magic = HTTP_CONN_MAGIC;
	htc->ws = ws;
	htc->fd = fd;
	htc->maxbytes = maxbytes;
	htc->maxhdr = maxhdr;

	(void)WS_Reserve(htc->ws, htc->maxbytes);
	htc->rxbuf_b = ws->f;
	htc->rxbuf_e = ws->f;
	*htc->rxbuf_e = '\0';
	htc->pipeline_b = NULL;
	htc->pipeline_e = NULL;
}

/*--------------------------------------------------------------------
 * Start over, and recycle any pipelined input.
 * The WS_Reset is safe, even though the pipelined input is stored in
 * the ws somewhere, because WS_Reset only fiddles pointers.
 */

enum http1_status_e
HTTP1_Reinit(struct http_conn *htc)
{
	ssize_t l;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	(void)WS_Reserve(htc->ws, htc->maxbytes);
	htc->rxbuf_b = htc->ws->f;
	htc->rxbuf_e = htc->ws->f;
	if (htc->pipeline_b != NULL) {
		l = htc->pipeline_e - htc->pipeline_b;
		assert(l > 0);
		memmove(htc->rxbuf_b, htc->pipeline_b, l);
		htc->rxbuf_e += l;
		htc->pipeline_b = NULL;
		htc->pipeline_e = NULL;
	}
	*htc->rxbuf_e = '\0';
	return (HTTP1_Complete(htc));
}

/*--------------------------------------------------------------------
 * Check if we have a complete HTTP request or response yet
 */

enum http1_status_e
HTTP1_Complete(struct http_conn *htc)
{
	char *p;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	AZ(htc->pipeline_b);
	AZ(htc->pipeline_e);

	assert(htc->rxbuf_e >= htc->rxbuf_b);
	assert(*htc->rxbuf_e == '\0');

	/* Skip any leading white space */
	for (p = htc->rxbuf_b ; vct_islws(*p); p++)
		continue;
	if (p == htc->rxbuf_e) {
		/* All white space */
		htc->rxbuf_e = htc->rxbuf_b;
		*htc->rxbuf_e = '\0';
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
	WS_ReleaseP(htc->ws, htc->rxbuf_e);
	if (p < htc->rxbuf_e) {
		htc->pipeline_b = p;
		htc->pipeline_e = htc->rxbuf_e;
		htc->rxbuf_e = p;
	}
	return (HTTP1_COMPLETE);
}

/*--------------------------------------------------------------------
 * Receive more HTTP protocol bytes
 */

enum http1_status_e
HTTP1_Rx(struct http_conn *htc)
{
	int i;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	AN(htc->ws->r);
	AZ(htc->pipeline_b);
	AZ(htc->pipeline_e);
	i = (htc->ws->r - htc->rxbuf_e) - 1;	/* space for NUL */
	if (i <= 0) {
		WS_ReleaseP(htc->ws, htc->rxbuf_b);
		return (HTTP1_OVERFLOW);
	}
	i = read(htc->fd, htc->rxbuf_e, i);
	if (i <= 0) {
		/*
		 * We wouldn't come here if we had a complete HTTP header
		 * so consequently an EOF can not be OK
		 */
		WS_ReleaseP(htc->ws, htc->rxbuf_b);
		return (HTTP1_ERROR_EOF);
	}
	htc->rxbuf_e += i;
	*htc->rxbuf_e = '\0';
	return (HTTP1_Complete(htc));
}

/*--------------------------------------------------------------------
 * Dissect the headers of the HTTP protocol message.
 * Detect conditionals (headers which start with '^[Ii][Ff]-')
 */

static uint16_t
http1_dissect_hdrs(struct http *hp, char *p, const struct http_conn *htc)
{
	char *q, *r;

	assert(p > htc->rxbuf_b);
	assert(p <= htc->rxbuf_e);
	hp->nhd = HTTP_HDR_FIRST;
	hp->conds = 0;
	r = NULL;		/* For FlexeLint */
	for (; p < htc->rxbuf_e; p = r) {

		/* Find end of next header */
		q = r = p;
		while (r < htc->rxbuf_e) {
			if (!vct_iscrlf(r)) {
				r++;
				continue;
			}
			q = r;
			assert(r < htc->rxbuf_e);
			r += vct_skipcrlf(r);
			if (r >= htc->rxbuf_e)
				break;
			/* If line does not continue: got it. */
			if (!vct_issp(*r))
				break;

			/* Clear line continuation LWS to spaces */
			while (vct_islws(*q))
				*q++ = ' ';
		}

		if (q - p > htc->maxhdr) {
			VSLb(hp->vsl, SLT_BogoHeader, "Header too long: %.*s",
			    (int)(q - p > 20 ? 20 : q - p), p);
			return (400);
		}

		/* Empty header = end of headers */
		if (p == q)
			break;

		if (vct_islws(*p)) {
			VSLb(hp->vsl, SLT_BogoHeader,
			    "1st header has white space: %.*s",
			    (int)(q - p > 20 ? 20 : q - p), p);
			return (400);
		}

		if ((p[0] == 'i' || p[0] == 'I') &&
		    (p[1] == 'f' || p[1] == 'F') &&
		    p[2] == '-')
			hp->conds = 1;

		while (q > p && vct_issp(q[-1]))
			q--;
		*q = '\0';

		if (strchr(p, ':') == NULL) {
			VSLb(hp->vsl, SLT_BogoHeader, "Header without ':' %.*s",
			    (int)(q - p > 20 ? 20 : q - p), p);
			return (400);
		}

		if (hp->nhd < hp->shd) {
			hp->hdf[hp->nhd] = 0;
			hp->hd[hp->nhd].b = p;
			hp->hd[hp->nhd].e = q;
			hp->nhd++;
		} else {
			VSLb(hp->vsl, SLT_BogoHeader, "Too many headers: %.*s",
			    (int)(q - p > 20 ? 20 : q - p), p);
			return (400);
		}
	}
	return (0);
}

/*--------------------------------------------------------------------
 * Deal with first line of HTTP protocol message.
 */

static uint16_t
http1_splitline(struct http *hp, const struct http_conn *htc, const int *hf)
{
	char *p;
	int i;

	assert(hf == HTTP1_Req || hf == HTTP1_Resp);
	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	assert(htc->rxbuf_e >= htc->rxbuf_b);

	AZ(hp->hd[hf[0]].b);
	AZ(hp->hd[hf[1]].b);
	AZ(hp->hd[hf[2]].b);

	/* Skip leading LWS */
	for (p = htc->rxbuf_b ; vct_islws(*p); p++)
		continue;
	hp->hd[hf[0]].b = p;

	/* First field cannot contain SP or CTL */
	for (; !vct_issp(*p); p++) {
		if (vct_isctl(*p))
			return (400);
	}
	hp->hd[hf[0]].e = p;
	assert(Tlen(hp->hd[hf[0]]));
	*p++ = '\0';

	/* Skip SP */
	for (; vct_issp(*p); p++) {
		if (vct_isctl(*p))
			return (400);
	}
	hp->hd[hf[1]].b = p;

	/* Second field cannot contain LWS or CTL */
	for (; !vct_islws(*p); p++) {
		if (vct_isctl(*p))
			return (400);
	}
	hp->hd[hf[1]].e = p;
	if (!Tlen(hp->hd[hf[1]]))
		return (400);
	*p++ = '\0';

	/* Skip SP */
	for (; vct_issp(*p); p++) {
		if (vct_isctl(*p))
			return (400);
	}
	hp->hd[hf[2]].b = p;

	/* Third field is optional and cannot contain CTL except TAB */
	for (; !vct_iscrlf(p); p++) {
		if (vct_isctl(*p) && !vct_issp(*p)) {
			hp->hd[hf[2]].b = NULL;
			return (400);
		}
	}
	hp->hd[hf[2]].e = p;

	/* Skip CRLF */
	i = vct_skipcrlf(p);
	*p = '\0';
	p += i;

	return (http1_dissect_hdrs(hp, p, htc));
}

/*--------------------------------------------------------------------*/

static enum body_status
http1_body_status(const struct http *hp, struct http_conn *htc)
{
	ssize_t cl;
	const char *b;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

	htc->content_length = -1;

	if (http_HdrIs(hp, H_Transfer_Encoding, "chunked"))
		return (BS_CHUNKED);

	if (http_GetHdr(hp, H_Transfer_Encoding, &b))
		return (BS_ERROR);

	cl = http_GetContentLength(hp);
	if (cl == -2)
		return (BS_ERROR);
	if (cl >= 0) {
		htc->content_length = cl;
		return (cl == 0 ? BS_NONE : BS_LENGTH);
	}

	if (http_HdrIs(hp, H_Connection, "keep-alive")) {
		/*
		 * Keep alive with neither TE=Chunked or C-Len is impossible.
		 * We assume a zero length body.
		 */
		return (BS_NONE);
	}

	if (http_HdrIs(hp, H_Connection, "close")) {
		/*
		 * In this case, it is safe to just read what comes.
		 */
		return (BS_EOF);
	}

	if (hp->protover < 11) {
		/*
		 * With no Connection header, assume EOF.
		 */
		return (BS_EOF);
	}

	/*
	 * Fall back to EOF transfer.
	 */
	return (BS_EOF);
}

/*--------------------------------------------------------------------*/

static void
http1_proto_ver(struct http *hp)
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
HTTP1_DissectRequest(struct http_conn *htc, struct http *hp)
{
	uint16_t retval;
	const char *p;
	const char *b, *e;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

	retval = http1_splitline(hp, htc, HTTP1_Req);
	if (retval != 0)
		return (retval);
	http1_proto_ver(hp);

	if (http_CountHdr(hp, H_Host) > 1)
		return (400);

	if (http_CountHdr(hp, H_Content_Length) > 1)
		return (400);

	/* RFC2616, section 5.2, point 1 */
	if (!strncasecmp(hp->hd[HTTP_HDR_URL].b, "http://", 7)) {
		b = e = hp->hd[HTTP_HDR_URL].b + 7;
		while (*e != '/' && *e != '\0')
			e++;
		if (*e == '/') {
			http_Unset(hp, H_Host);
			http_PrintfHeader(hp, "Host: %.*s", (int)(e - b), b);
			hp->hd[HTTP_HDR_URL].b = e;
		}
	}

	htc->body_status = http1_body_status(hp, htc);
	if (htc->body_status == BS_ERROR)
		return (400);

	p = http_GetMethod(hp);
	AN(p);

	/* We handle EOF bodies only for PUT and POST */
	if (htc->body_status == BS_EOF &&
	    strcasecmp(p, "put") && strcasecmp(p, "post"))
		htc->body_status = BS_NONE;

	/* HEAD with a body is a hard error */
	if (htc->body_status != BS_NONE && !strcasecmp(p, "head"))
		return (400);

	return (retval);
}

/*--------------------------------------------------------------------*/

uint16_t
HTTP1_DissectResponse(struct http *hp, struct http_conn *htc)
{
	uint16_t retval = 0;
	const char *p;


	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

	if (http1_splitline(hp, htc, HTTP1_Resp))
		retval = 503;

	if (retval == 0) {
		http1_proto_ver(hp);
		if (hp->protover != 10 && hp->protover != 11)
			retval = 503;
	}

	if (retval == 0 && Tlen(hp->hd[HTTP_HDR_STATUS]) != 3)
		retval = 503;

	if (retval == 0) {
		p = hp->hd[HTTP_HDR_STATUS].b;

		if (p[0] >= '1' && p[0] <= '9' &&
		    p[1] >= '0' && p[1] <= '9' &&
		    p[2] >= '0' && p[2] <= '9')
			hp->status =
			    100 * (p[0] - '0') + 10 * (p[1] - '0') + p[2] - '0';
		else
			retval = 503;
	}

	if (retval != 0) {
		VSLb(hp->vsl, SLT_HttpGarbage, "%.*s",
		    (int)(htc->rxbuf_e - htc->rxbuf_b), htc->rxbuf_b);
		assert(retval >= 100 && retval <= 999);
		assert(retval == 503);
		hp->status = retval;
		http_SetH(hp, HTTP_HDR_STATUS, "503");
		http_SetH(hp, HTTP_HDR_REASON, http_Status2Reason(retval));
	}

	if (hp->hd[HTTP_HDR_REASON].b == NULL ||
	    !Tlen(hp->hd[HTTP_HDR_REASON]))
		http_SetH(hp, HTTP_HDR_REASON, http_Status2Reason(hp->status));

	htc->body_status = http1_body_status(hp, htc);

	return (retval);
}

/*--------------------------------------------------------------------*/

static unsigned
http1_WrTxt(const struct worker *wrk, const txt *hh, const char *suf)
{
	unsigned u;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AN(wrk);
	AN(hh);
	AN(hh->b);
	AN(hh->e);
	u = WRW_Write(wrk, hh->b, hh->e - hh->b);
	if (suf != NULL)
		u += WRW_Write(wrk, suf, -1);
	return (u);
}

unsigned
HTTP1_Write(const struct worker *w, const struct http *hp, const int *hf)
{
	unsigned u, l;

	assert(hf == HTTP1_Req || hf == HTTP1_Resp);
	AN(hp->hd[hf[0]].b);
	AN(hp->hd[hf[1]].b);
	AN(hp->hd[hf[2]].b);
	l = http1_WrTxt(w, &hp->hd[hf[0]], " ");
	l += http1_WrTxt(w, &hp->hd[hf[1]], " ");
	l += http1_WrTxt(w, &hp->hd[hf[2]], "\r\n");

	for (u = HTTP_HDR_FIRST; u < hp->nhd; u++)
		l += http1_WrTxt(w, &hp->hd[u], "\r\n");
	l += WRW_Write(w, "\r\n", -1);
	return (l);
}
