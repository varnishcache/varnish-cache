/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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

#include "cache/cache_varnishd.h"
#include "cache/cache_transport.h"

#include "cache_http1.h"

#include "vct.h"

const int HTTP1_Req[3] = {
	HTTP_HDR_METHOD, HTTP_HDR_URL, HTTP_HDR_PROTO
};

const int HTTP1_Resp[3] = {
	HTTP_HDR_PROTO, HTTP_HDR_STATUS, HTTP_HDR_REASON
};

/*--------------------------------------------------------------------
 * Check if we have a complete HTTP request or response yet
 */

enum htc_status_e v_matchproto_(htc_complete_f)
HTTP1_Complete(struct http_conn *htc)
{
	char *p;
	enum htc_status_e retval;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	AN(WS_Reservation(htc->ws));
	assert(pdiff(htc->rxbuf_b, htc->rxbuf_e) <= WS_ReservationSize(htc->ws));

	/* Skip any leading white space */
	for (p = htc->rxbuf_b ; p < htc->rxbuf_e && vct_islws(*p); p++)
		continue;
	if (p == htc->rxbuf_e)
		return (HTC_S_EMPTY);

	/* Do not return a partial H2 connection preface */
	retval = H2_prism_complete(htc);
	if (retval != HTC_S_JUNK)
		return (retval);

	/*
	 * Here we just look for NL[CR]NL to see that reception
	 * is completed.  More stringent validation happens later.
	 */
	while (1) {
		p = memchr(p, '\n', htc->rxbuf_e - p);
		if (p == NULL)
			return (HTC_S_MORE);
		if (++p == htc->rxbuf_e)
			return (HTC_S_MORE);
		if (*p == '\r' && ++p == htc->rxbuf_e)
			return (HTC_S_MORE);
		if (*p == '\n')
			break;
	}
	return (HTC_S_COMPLETE);
}

/*--------------------------------------------------------------------
 * Dissect the headers of the HTTP protocol message.
 */

static uint16_t
http1_dissect_hdrs(struct http *hp, char *p, struct http_conn *htc,
    unsigned maxhdr)
{
	char *q, *r, *s;
	int i;

	assert(p > htc->rxbuf_b);
	assert(p <= htc->rxbuf_e);
	hp->nhd = HTTP_HDR_FIRST;
	r = NULL;		/* For FlexeLint */
	for (; p < htc->rxbuf_e; p = r) {

		/* Find end of next header */
		q = r = p;
		if (vct_iscrlf(p, htc->rxbuf_e))
			break;
		while (r < htc->rxbuf_e) {
			if (vct_ishdrval(*r)) {
				r++;
				continue;
			}
			i = vct_iscrlf(r, htc->rxbuf_e);
			if (i == 0) {
				VSLb(hp->vsl, SLT_BogoHeader,
				    "Header has ctrl char 0x%02x", *r);
				return (400);
			}
			q = r;
			r += i;
			assert(r <= htc->rxbuf_e);
			if (r == htc->rxbuf_e)
				break;
			if (vct_iscrlf(r, htc->rxbuf_e))
				break;
			/* If line does not continue: got it. */
			if (!vct_issp(*r))
				break;

			/* Clear line continuation LWS to spaces */
			while (q < r)
				*q++ = ' ';
			while (q < htc->rxbuf_e && vct_issp(*q))
				*q++ = ' ';
		}

		/* Empty header = end of headers */
		if (p == q)
			break;

		if (q - p > maxhdr) {
			VSLb(hp->vsl, SLT_BogoHeader, "Header too long: %.*s",
			    (int)(q - p > 20 ? 20 : q - p), p);
			return (400);
		}

		if (vct_islws(*p)) {
			VSLb(hp->vsl, SLT_BogoHeader,
			    "1st header has white space: %.*s",
			    (int)(q - p > 20 ? 20 : q - p), p);
			return (400);
		}

		if (*p == ':') {
			VSLb(hp->vsl, SLT_BogoHeader,
			    "Missing header name: %.*s",
			    (int)(q - p > 20 ? 20 : q - p), p);
			return (400);
		}

		while (q > p && vct_issp(q[-1]))
			q--;
		*q = '\0';

		for (s = p; *s != ':' && s < q; s++) {
			if (!vct_istchar(*s)) {
				VSLb(hp->vsl, SLT_BogoHeader,
				    "Illegal char 0x%02x in header name", *s);
				return (400);
			}
		}
		if (*s != ':') {
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
	i = vct_iscrlf(p, htc->rxbuf_e);
	assert(i > 0);		/* HTTP1_Complete guarantees this */
	p += i;
	HTC_RxPipeline(htc, p);
	htc->rxbuf_e = p;
	return (0);
}

/*--------------------------------------------------------------------
 * Deal with first line of HTTP protocol message.
 */

static uint16_t
http1_splitline(struct http *hp, struct http_conn *htc, const int *hf,
    unsigned maxhdr)
{
	char *p, *q;
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

	/* Skip SP */
	q = p;
	for (; vct_issp(*p); p++) {
		if (vct_isctl(*p))
			return (400);
	}
	if (q < p)
		*q = '\0';	/* Nul guard for the 2nd field. If q == p
				 * (the third optional field is not
				 * present), the last nul guard will
				 * cover this field. */

	/* Third field is optional and cannot contain CTL except TAB */
	q = p;
	for (; p < htc->rxbuf_e && !vct_iscrlf(p, htc->rxbuf_e); p++) {
		if (vct_isctl(*p) && !vct_issp(*p))
			return (400);
	}
	if (p > q) {
		hp->hd[hf[2]].b = q;
		hp->hd[hf[2]].e = p;
	}

	/* Skip CRLF */
	i = vct_iscrlf(p, htc->rxbuf_e);
	if (!i)
		return (400);
	*p = '\0';
	p += i;

	http_Proto(hp);

	return (http1_dissect_hdrs(hp, p, htc, maxhdr));
}

/*--------------------------------------------------------------------*/

static body_status_t
http1_body_status(const struct http *hp, struct http_conn *htc, int request)
{
	ssize_t cl;
	const char *b;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

	htc->content_length = -1;

	cl = http_GetContentLength(hp);
	if (cl == -2)
		return (BS_ERROR);
	if (http_GetHdr(hp, H_Transfer_Encoding, &b)) {
		if (!http_coding_eq(b, chunked))
			return (BS_ERROR);
		if (cl != -1) {
			/*
			 * RFC7230 3.3.3 allows more lenient handling
			 * but we're going to be strict.
			 */
			return (BS_ERROR);
		}
		return (BS_CHUNKED);
	}
	if (cl >= 0) {
		htc->content_length = cl;
		return (cl == 0 ? BS_NONE : BS_LENGTH);
	}

	if (hp->protover == 11 && request)
		return (BS_NONE);

	if (http_HdrIs(hp, H_Connection, "keep-alive")) {
		/*
		 * Keep alive with neither TE=Chunked or C-Len is impossible.
		 * We assume a zero length body.
		 */
		return (BS_NONE);
	}

	/*
	 * Fall back to EOF transfer.
	 */
	return (BS_EOF);
}

/*--------------------------------------------------------------------*/

uint16_t
HTTP1_DissectRequest(struct http_conn *htc, struct http *hp)
{
	uint16_t retval;
	const char *p;
	const char *b = NULL, *e;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

	retval = http1_splitline(hp, htc,
	    HTTP1_Req, cache_param->http_req_hdr_len);
	if (retval != 0)
		return (retval);

	if (hp->protover < 10 || hp->protover > 11)
		return (400);

	/* RFC2616, section 5.2, point 1 */
	if (http_scheme_at(hp->hd[HTTP_HDR_URL].b, http))
		b = hp->hd[HTTP_HDR_URL].b + 7;
	else if (FEATURE(FEATURE_HTTPS_SCHEME) &&
	    http_scheme_at(hp->hd[HTTP_HDR_URL].b, https))
		b = hp->hd[HTTP_HDR_URL].b + 8;
	if (b) {
		e = strchr(b, '/');
		if (e) {
			http_Unset(hp, H_Host);
			http_PrintfHeader(hp, "Host: %.*s", (int)(e - b), b);
			hp->hd[HTTP_HDR_URL].b = e;
		}
	}

	htc->body_status = http1_body_status(hp, htc, 1);
	if (htc->body_status == BS_ERROR)
		return (400);

	p = http_GetMethod(hp);
	AN(p);
	http_SetWellKnownMethod(hp);

	if (htc->body_status == BS_EOF) {
		assert(hp->protover == 10);
		/* RFC1945 8.3 p32 and D.1.1 p58 */
		if (http_method_among(hp->wkm, (WKM_POST | WKM_PUT)))
			return (400);
		htc->body_status = BS_NONE;
	}

	/* HEAD with a body is a hard error */
	if (htc->body_status != BS_NONE && http_method_eq(hp->wkm, WKM_HEAD))
		return (400);

	return (retval);
}

/*--------------------------------------------------------------------*/

uint16_t
HTTP1_DissectResponse(struct http_conn *htc, struct http *hp,
    const struct http *rhttp)
{
	uint16_t retval = 0;
	const char *p;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(rhttp, HTTP_MAGIC);

	if (http1_splitline(hp, htc,
	    HTTP1_Resp, cache_param->http_resp_hdr_len))
		retval = 503;

	if (retval == 0 && hp->protover < 10)
		retval = 503;

	if (retval == 0 && hp->protover > rhttp->protover)
		http_SetH(hp, HTTP_HDR_PROTO, rhttp->hd[HTTP_HDR_PROTO].b);

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
		http_SetStatus(hp, 503, NULL);
	}

	if (hp->hd[HTTP_HDR_REASON].b == NULL ||
	    !Tlen(hp->hd[HTTP_HDR_REASON])) {
		http_SetH(hp, HTTP_HDR_REASON,
		    http_Status2Reason(hp->status, NULL));
	}

	htc->body_status = http1_body_status(hp, htc, 0);

	return (retval);
}

/*--------------------------------------------------------------------*/

static unsigned
http1_WrTxt(struct v1l *v1l, const txt *hh, const char *suf)
{
	unsigned u;

	AN(hh);
	AN(hh->b);
	AN(hh->e);
	u = V1L_Write(v1l, hh->b, hh->e - hh->b);
	if (suf != NULL)
		u += V1L_Write(v1l, suf, -1);
	return (u);
}

unsigned
HTTP1_Write(struct v1l *v1l, const struct http *hp, const int *hf)
{
	unsigned u, l;

	assert(hf == HTTP1_Req || hf == HTTP1_Resp);
	AN(hp->hd[hf[0]].b);
	AN(hp->hd[hf[1]].b);
	AN(hp->hd[hf[2]].b);
	l = http1_WrTxt(v1l, &hp->hd[hf[0]], " ");
	l += http1_WrTxt(v1l, &hp->hd[hf[1]], " ");
	l += http1_WrTxt(v1l, &hp->hd[hf[2]], "\r\n");

	for (u = HTTP_HDR_FIRST; u < hp->nhd; u++)
		l += http1_WrTxt(v1l, &hp->hd[u], "\r\n");
	l += V1L_Write(v1l, "\r\n", -1);
	return (l);
}
