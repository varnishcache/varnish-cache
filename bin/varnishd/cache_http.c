/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2007 Linpro AS
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
 *
 * HTTP request storage and manipulation
 */

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "heritage.h"
#include "shmlog.h"
#include "cache.h"

#ifndef HAVE_STRLCPY
#include <compat/strlcpy.h>
#endif

#define HTTPH(a, b, c, d, e, f, g) char b[] = "*" a ":";
#include "http_headers.h"
#undef HTTPH

enum httptag {
	HTTP_T_Request,
	HTTP_T_Response,
	HTTP_T_Status,
	HTTP_T_URL,
	HTTP_T_Protocol,
	HTTP_T_Header,
	HTTP_T_LostHeader,
};

#define LOGMTX2(ax, bx) 	\
	[HTTP_T_##bx] = SLT_##ax##bx

#define LOGMTX1(ax) { 		\
	LOGMTX2(ax, Request),	\
	LOGMTX2(ax, Response),	\
	LOGMTX2(ax, Status),	\
	LOGMTX2(ax, URL),	\
	LOGMTX2(ax, Protocol),	\
	LOGMTX2(ax, Header),	\
	LOGMTX2(ax, LostHeader)	\
	}

static enum shmlogtag logmtx[3][7] = {
	[HTTP_Rx] = LOGMTX1(Rx),
	[HTTP_Tx] = LOGMTX1(Tx),
	[HTTP_Obj] = LOGMTX1(Obj)
};

static enum shmlogtag
http2shmlog(struct http *hp, enum httptag t)
{

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	assert(/* hp->logtag >= HTTP_Rx && */hp->logtag <= HTTP_Obj);
	assert(/* t >= HTTP_T_Request && */t <= HTTP_T_LostHeader);
	return (logmtx[hp->logtag][t]);
}

static void
WSLH(struct worker *w, enum httptag t, unsigned xid, struct http *hp, int hdr)
{

	WSLR(w, http2shmlog(hp, t), xid, hp->hd[hdr].b, hp->hd[hdr].e);
}

/*--------------------------------------------------------------------*/
/* List of canonical HTTP response code names from RFC2616 */

static struct http_msg {
	unsigned	nbr;
	const char	*txt;
} http_msg[] = {
	{ 101, "Switching Protocols" },
	{ 200, "OK" },
	{ 201, "Created" },
	{ 202, "Accepted" },
	{ 203, "Non-Authoritative Information" },
	{ 204, "No Content" },
	{ 205, "Reset Content" },
	{ 206, "Partial Content" },
	{ 300, "Multiple Choices" },
	{ 301, "Moved Permanently" },
	{ 302, "Found" },
	{ 303, "See Other" },
	{ 304, "Not Modified" },
	{ 305, "Use Proxy" },
	{ 306, "(Unused)" },
	{ 307, "Temporary Redirect" },
	{ 400, "Bad Request" },
	{ 401, "Unauthorized" },
	{ 402, "Payment Required" },
	{ 403, "Forbidden" },
	{ 404, "Not Found" },
	{ 405, "Method Not Allowed" },
	{ 406, "Not Acceptable" },
	{ 407, "Proxy Authentication Required" },
	{ 408, "Request Timeout" },
	{ 409, "Conflict" },
	{ 410, "Gone" },
	{ 411, "Length Required" },
	{ 412, "Precondition Failed" },
	{ 413, "Request Entity Too Large" },
	{ 414, "Request-URI Too Long" },
	{ 415, "Unsupported Media Type" },
	{ 416, "Requested Range Not Satisfiable" },
	{ 417, "Expectation Failed" },
	{ 500, "Internal Server Error" },
	{ 501, "Not Implemented" },
	{ 502, "Bad Gateway" },
	{ 503, "Service Unavailable" },
	{ 504, "Gateway Timeout" },
	{ 505, "HTTP Version Not Supported" },
	{ 0, NULL }
};

const char *
http_StatusMessage(int status)
{
	struct http_msg *mp;

	assert(status >= 100 && status <= 999);
	for (mp = http_msg; mp->nbr != 0 && mp->nbr <= status; mp++)
		if (mp->nbr == status)
			return (mp->txt);
	return ("Unknown Error");
}

/*--------------------------------------------------------------------*/

void
http_Setup(struct http *hp, void *space, unsigned len)
{

	assert(len > 0);
	memset(hp, 0, sizeof *hp);
	hp->magic = HTTP_MAGIC;
	WS_Init(hp->ws, space, len);
	hp->nhd = HTTP_HDR_FIRST;
}

/*--------------------------------------------------------------------*/


static int
http_IsHdr(struct http_hdr *hh, const char *hdr)
{
	unsigned l;

	AN(hh->b);
	AN(hh->e);
	AN(hdr);
	l = hdr[0];
	assert(l == strlen(hdr + 1));
	assert(hdr[l] == ':');
	hdr++;
	return (!strncasecmp(hdr, hh->b, l));
}

/*--------------------------------------------------------------------*/

static unsigned
http_findhdr(struct http *hp, unsigned l, const char *hdr)
{
	unsigned u;

	for (u = HTTP_HDR_FIRST; u < hp->nhd; u++) {
		AN(hp->hd[u].b);
		AN(hp->hd[u].e);
		if (hp->hd[u].e < hp->hd[u].b + l + 1)
			continue;
		if (hp->hd[u].b[l] != ':')
			continue;
		if (strncasecmp(hdr, hp->hd[u].b, l))
			continue;
		return (u);
	}
	return (0);
}

int
http_GetHdr(struct http *hp, const char *hdr, char **ptr)
{
	unsigned u, l;
	char *p;

	l = hdr[0];
	assert(l == strlen(hdr + 1));
	assert(hdr[l] == ':');
	hdr++;
	u = http_findhdr(hp, l - 1, hdr);
	if (u == 0) {
		*ptr = NULL;
		return (0);
	}
	p = hp->hd[u].b + l;
	while (isspace(*p))
		p++;
	*ptr = p;
	return (1);
}

/*--------------------------------------------------------------------*/

int
http_GetHdrField(struct http *hp, const char *hdr, const char *field, char **ptr)
{
	char *h;
	int fl;

	if (!http_GetHdr(hp, hdr, &h))
		return (0);
	fl = strlen(field);
	while (*h) {
		if (isspace(*h)) {
			h++;
			continue;
		}
		if (*h == ',') {
			h++;
			continue;
		}
		if (memcmp(h, field, fl) ||
		    isalpha(h[fl]) || h[fl] == '-') {
			while (*h && !(isspace(*h) || *h == ','))
				h++;
			continue;
		}
		if (h[fl] == '=')
			*ptr = &h[fl + 1];
		else
			*ptr = NULL;
		return (1);
	}
	return (0);
}

/*--------------------------------------------------------------------*/

void
http_DoConnection(struct sess *sp)
{
	struct http *hp = sp->http;
	char *p, *q;
	int i;
	unsigned u;

	if (!http_GetHdr(hp, H_Connection, &p)) {
		if (strcmp(hp->hd[HTTP_HDR_PROTO].b, "HTTP/1.1"))
			sp->doclose = "not HTTP/1.1";
		return;
	}
	for (; *p; p++) {
		if (isspace(*p))
			continue;
		if (*p == ',')
			continue;
		for (q = p + 1; *q; q++)
			if (*q == ',' || isspace(*q))
				break;
		i = q - p;
		if (i == 5 && !strncasecmp(p, "close", i))
			sp->doclose = "Connection: close";
		u = http_findhdr(hp, i, p);
		if (u != 0)
			hp->hdf[u] |= HDF_FILTER;
		if (!*q)
			break;
		p = q;
	}
}

/*--------------------------------------------------------------------*/

int
http_HdrIs(struct http *hp, const char *hdr, const char *val)
{
	char *p;

	if (!http_GetHdr(hp, hdr, &p))
		return (0);
	AN(p);
	if (!strcasecmp(p, val))
		return (1);
	return (0);
}

/*--------------------------------------------------------------------*/

int
http_GetTail(struct http *hp, unsigned len, char **b, char **e)
{

	if (hp->pl_s >= hp->pl_e)
		return (0);

	if (len == 0)
		len = hp->pl_e - hp->pl_e;

	if (hp->pl_s + len > hp->pl_e)
		len = hp->pl_e - hp->pl_s;
	if (len == 0)
		return (0);
	*b = hp->pl_s;
	*e = hp->pl_s + len;
	hp->pl_s += len;
	assert(hp->pl_s <= hp->pl_e);
	return (1);
}

/*--------------------------------------------------------------------*/
/* Read from fd, but soak up any tail first */

int
http_Read(struct http *hp, int fd, void *p, unsigned len)
{
	int i;
	int u;
	char *b = p;

	u = 0;
	if (hp->pl_s < hp->pl_e) {
		u = hp->pl_e - hp->pl_s;
		if (u > len)
			u = len;
		memcpy(b, hp->pl_s, u);
		hp->pl_s += u;
		b += u;
		len -= u;
	}
	if (hp->pl_e == hp->pl_s)
		hp->pl_s = hp->pl_e = NULL;
	if (len > 0) {
		i = read(fd, b, len);
		if (i < 0)
			return (i);
		u += i;
	}
	return (u);
}

/*--------------------------------------------------------------------*/

int
http_GetStatus(struct http *hp)
{

	AN(hp->hd[HTTP_HDR_STATUS].b);
	return (strtoul(hp->hd[HTTP_HDR_STATUS].b,
	    NULL /* XXX */, 10));
}

/*--------------------------------------------------------------------
 * Dissect the headers of the HTTP protocol message.
 * Detect conditionals (headers which start with '^[Ii][Ff]-')
 */

static int
http_dissect_hdrs(struct worker *w, struct http *hp, int fd, char *p)
{
	char *q, *r;

	if (*p == '\r')
		p++;

	hp->nhd = HTTP_HDR_FIRST;
	hp->conds = 0;
	r = NULL;		/* For FlexeLint */
	assert(p < hp->rx_e);	/* http_header_complete() guarantees this */
	for (; p < hp->rx_e; p = r) {
		/* XXX: handle continuation lines */
		q = strchr(p, '\n');
		assert(q != NULL);
		r = q + 1;
		if (q > p && q[-1] == '\r')
			q--;
		*q = '\0';
		if (p == q)
			break;

		if ((p[0] == 'i' || p[0] == 'I') &&
		    (p[1] == 'f' || p[1] == 'F') &&
		    p[2] == '-')
			hp->conds = 1;

		if (hp->nhd < HTTP_HDR_MAX) {
			hp->hdf[hp->nhd] = 0;
			hp->hd[hp->nhd].b = p;
			hp->hd[hp->nhd].e = q;
			WSLH(w, HTTP_T_Header, fd, hp, hp->nhd);
			hp->nhd++;
		} else {
			VSL_stats->losthdr++;
			WSLR(w, http2shmlog(hp, HTTP_T_LostHeader), fd, p, q);
		}
	}
	return (0);
}

/*--------------------------------------------------------------------*/

int
http_DissectRequest(struct worker *w, struct http *hp, int fd)
{
	char *p;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	/* Assert a NUL at rx_e */
	assert(hp->rx_s < hp->rx_e);
	hp->logtag = HTTP_Rx;

	for (p = hp->rx_s ; isspace(*p); p++)
		continue;

	/* First, the request type (GET/HEAD etc) */
	hp->hd[HTTP_HDR_REQ].b = p;
	for (; isalpha(*p); p++)
		;
	hp->hd[HTTP_HDR_REQ].e = p;
	WSLH(w, HTTP_T_Request, fd, hp, HTTP_HDR_REQ);
	*p++ = '\0';

	/* Next find the URI */
	while (isspace(*p) && *p != '\n')
		p++;
	if (*p == '\n') {
		WSLR(w, SLT_HttpGarbage, fd, hp->rx_s, hp->rx_e);
		return (400);
	}
	hp->hd[HTTP_HDR_URL].b = p;
	while (!isspace(*p))
		p++;
	hp->hd[HTTP_HDR_URL].e = p;
	WSLH(w, HTTP_T_URL, fd, hp, HTTP_HDR_URL);
	if (*p == '\n') {
		WSLR(w, SLT_HttpGarbage, fd, hp->rx_s, hp->rx_e);
		return (400);
	}
	*p++ = '\0';

	/* Finally, look for protocol */
	while (isspace(*p) && *p != '\n')
		p++;
	if (*p == '\n') {
		WSLR(w, SLT_HttpGarbage, fd, hp->rx_s, hp->rx_e);
		return (400);
	}
	hp->hd[HTTP_HDR_PROTO].b = p;
	while (!isspace(*p))
		p++;
	hp->hd[HTTP_HDR_PROTO].e = p;
	WSLH(w, HTTP_T_Protocol, fd, hp, HTTP_HDR_PROTO);
	if (*p != '\n')
		*p++ = '\0';
	while (isspace(*p) && *p != '\n')
		p++;
	if (*p != '\n') {
		WSLR(w, SLT_HttpGarbage, fd, hp->rx_s, hp->rx_e);
		return (400);
	}
	*p++ = '\0';

	return (http_dissect_hdrs(w, hp, fd, p));
}

/*--------------------------------------------------------------------*/

int
http_DissectResponse(struct worker *w, struct http *hp, int fd)
{
	char *p, *q;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	/* Assert a NUL at rx_e */
	assert(hp->rx_s < hp->rx_e);
	hp->logtag = HTTP_Rx;

	for (p = hp->rx_s ; isspace(*p); p++)
		continue;

	if (memcmp(p, "HTTP/1.", 7)) {
		WSLR(w, SLT_HttpGarbage, fd, hp->rx_s, hp->rx_e);
		return (400);
	}
	/* First, protocol */
	hp->hd[HTTP_HDR_PROTO].b = p;
	while (!isspace(*p))
		p++;
	hp->hd[HTTP_HDR_PROTO].e = p;
	WSLH(w, HTTP_T_Protocol, fd, hp, HTTP_HDR_PROTO);
	*p++ = '\0';

	/* Next find the status */
	while (isspace(*p))
		p++;
	hp->hd[HTTP_HDR_STATUS].b = p;
	while (!isspace(*p))
		p++;
	hp->hd[HTTP_HDR_STATUS].e = p;
	WSLH(w, HTTP_T_Status, fd, hp, HTTP_HDR_STATUS);
	*p++ = '\0';

	/* Next find the response */
	while (isspace(*p))
		p++;
	hp->hd[HTTP_HDR_RESPONSE].b = p;
	while (*p != '\n')
		p++;
	for (q = p; q > hp->hd[HTTP_HDR_RESPONSE].b &&
	    isspace(q[-1]); q--)
		continue;
	*q = '\0';
	hp->hd[HTTP_HDR_RESPONSE].e = q;
	WSLH(w, HTTP_T_Response, fd, hp, HTTP_HDR_RESPONSE);
	p++;

	return (http_dissect_hdrs(w, hp, fd, p));
}

/*--------------------------------------------------------------------
 * Return nonzero if we have a complete HTTP request.
 */

static int
http_header_complete(struct http *hp)
{
	char *p;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	assert(*hp->rx_e == '\0');
	/* Skip any leading white space */
	for (p = hp->rx_s ; p < hp->rx_e && isspace(*p); p++)
		continue;
	if (p >= hp->rx_e) {
		hp->rx_e = hp->rx_s;
		return (0);
	}
	while (1) {
		/* XXX: we could save location of all linebreaks for later */
		p = strchr(p, '\n');
		if (p == NULL)
			return (0);	/* XXX: Could cache p */
		p++;
		if (*p == '\r')
			p++;
		if (*p == '\n')
			break;
	}
	p++;
	WS_ReleaseP(hp->ws, hp->rx_e);
	if (p != hp->rx_e) {
		hp->pl_s = p;
		hp->pl_e = hp->rx_e;
		hp->rx_e = p;
	}
	/* XXX: Check this stuff... */
	return (1);
}

/*--------------------------------------------------------------------*/

void
http_RecvPrep(struct http *hp)
{
	unsigned l;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	WS_Assert(hp->ws);
	WS_Reset(hp->ws);
	WS_Reserve(hp->ws, 0);
	hp->rx_s = hp->ws->f;
	hp->rx_e = hp->rx_s;
	if (hp->pl_s != NULL) {
		assert(hp->pl_s < hp->pl_e);
		l = hp->pl_e - hp->pl_s;
		memmove(hp->rx_s, hp->pl_s, l);
		hp->rx_e = hp->rx_s + l;
		hp->pl_s = hp->pl_e = NULL;
	}
	*hp->rx_e = '\0';
}

int
http_RecvPrepAgain(struct http *hp)
{
	http_RecvPrep(hp);
	if (hp->rx_s == hp->rx_e)
		return (0);
	return (http_header_complete(hp));
}

/*--------------------------------------------------------------------*/

int
http_RecvSome(int fd, struct http *hp)
{
	unsigned l;
	int i;

	l = (hp->ws->e - hp->rx_e) - 1;
	l /= 2;		/* Don't fill all of workspace with read-ahead */
	if (l <= 1) {
		VSL(SLT_HttpError, fd, "Received too much");
		VSLR(SLT_HttpGarbage, fd, hp->rx_s, hp->rx_e);
		hp->rx_s = hp->rx_e = NULL;
		WS_Release(hp->ws, 0);
		return (1);
	}
	errno = 0;
	i = read(fd, hp->rx_e, l - 1);
	if (i > 0) {
		hp->rx_e += i;
		*hp->rx_e = '\0';
		if (http_header_complete(hp))
			return(0);
		return (-1);
	}

	if (hp->rx_e != hp->rx_s) {
		VSL(SLT_HttpError, fd,
		    "Received (only) %d bytes, errno %d",
		    hp->rx_e - hp->rx_s, errno);
		VSLR(SLT_Debug, fd, hp->rx_s, hp->rx_e);
	} else if (errno == 0)
		VSL(SLT_HttpError, fd, "Received nothing");
	else
		VSL(SLT_HttpError, fd, "Received errno %d", errno);
	hp->rx_s = hp->rx_e = NULL;
	WS_Release(hp->ws, 0);
	return(2);
}

/*--------------------------------------------------------------------*/

int
http_RecvHead(struct http *hp, int fd)
{
	int i;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	http_RecvPrep(hp);
	do
		i = http_RecvSome(fd, hp);
	while (i == -1);
	return (i);
}

/*--------------------------------------------------------------------*/

static void
http_seth(struct http *to, unsigned n, const char *fm)
{

	assert(n < HTTP_HDR_MAX);
	AN(fm);
	to->hd[n].b = (void*)(uintptr_t)fm;
	to->hd[n].e = (void*)(uintptr_t)strchr(fm, '\0');
	to->hdf[n] = 0;
}

static void
http_copyh(struct http *to, struct http *fm, unsigned n)
{

	assert(n < HTTP_HDR_MAX);
	AN(fm->hd[n].b);
	to->hd[n].b = fm->hd[n].b;
	to->hd[n].e = fm->hd[n].e;
	to->hdf[n] = fm->hdf[n];
}

static void
http_getreq(struct http *to, struct http *fm)
{

	CHECK_OBJ_NOTNULL(fm, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	http_seth(to, HTTP_HDR_REQ, "GET");
	http_copyh(to, fm, HTTP_HDR_URL);
	http_seth(to, HTTP_HDR_PROTO, "HTTP/1.1");
}

void
http_CopyResp(struct http *to, struct http *fm)
{

	CHECK_OBJ_NOTNULL(fm, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	if (params->client_http11)
		http_seth(to, HTTP_HDR_PROTO, "HTTP/1.1");
	else
		http_copyh(to, fm, HTTP_HDR_PROTO);
	http_copyh(to, fm, HTTP_HDR_STATUS);
	http_copyh(to, fm, HTTP_HDR_RESPONSE);
}

void
http_SetResp(struct http *to, const char *proto, const char *status, const char *response)
{

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	http_seth(to, HTTP_HDR_PROTO, proto);
	http_seth(to, HTTP_HDR_STATUS, status);
	http_seth(to, HTTP_HDR_RESPONSE, response);
}

static void
http_copyheader(struct worker *w, int fd, struct http *to, struct http *fm, unsigned n)
{

	CHECK_OBJ_NOTNULL(fm, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	assert(n < HTTP_HDR_MAX);
	AN(fm->hd[n].b);
	if (to->nhd < HTTP_HDR_MAX) {
		to->hd[to->nhd].b = fm->hd[n].b;
		to->hd[to->nhd].e = fm->hd[n].e;
		to->nhd++;
	} else  {
		VSL_stats->losthdr++;
		WSLH(w, HTTP_T_LostHeader, fd, fm, n);
	}
}

/*--------------------------------------------------------------------*/

void
http_FilterFields(struct worker *w, int fd, struct http *to, struct http *fm, unsigned how)
{
	unsigned u;

	CHECK_OBJ_NOTNULL(fm, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	to->nhd = HTTP_HDR_FIRST;
	for (u = HTTP_HDR_FIRST; u < fm->nhd; u++) {
		if (fm->hdf[u] & HDF_FILTER)
			continue;
#define HTTPH(a, b, c, d, e, f, g) \
		if (((e) & how) && http_IsHdr(&fm->hd[u], (b))) \
			continue;
#include "http_headers.h"
#undef HTTPH
		http_copyheader(w, fd, to, fm, u);
	}
}

/*--------------------------------------------------------------------*/

void
http_FilterHeader(struct sess *sp, unsigned how)
{
	struct bereq *bereq;
	struct http *hp;
	char *b;

        bereq = vbe_new_bereq();
        AN(bereq);
        hp = bereq->http;
        hp->logtag = HTTP_Tx;

	http_getreq(hp, sp->http);
	http_FilterFields(sp->wrk, sp->fd, hp, sp->http, how);
	http_PrintfHeader(sp->wrk, sp->fd, hp, "X-Varnish: %u", sp->xid);
	http_PrintfHeader(sp->wrk, sp->fd, hp,
	    "X-Forwarded-for: %s", sp->addr);
	if (!http_GetHdr(hp, H_Host, &b)) {
		http_PrintfHeader(sp->wrk, sp->fd, hp, "Host: %s",
		    sp->backend->hostname);
	}
	sp->bereq = bereq;
}

/*--------------------------------------------------------------------
 * This function copies any header fields which reference foreign
 * storage into our own WS.
 */

void
http_CopyHome(struct worker *w, int fd, struct http *hp)
{
	unsigned u, l;
	enum httptag htt;
	char *p;

	for (u = 0; u < hp->nhd; u++) {
		if (hp->hd[u].b == NULL)
			continue;
		switch (u) {
		case HTTP_HDR_PROTO:
			htt = HTTP_T_Protocol;
			break;
		case HTTP_HDR_STATUS:
			htt = HTTP_T_Status;
			break;
		case HTTP_HDR_RESPONSE:
			htt = HTTP_T_Response;
			break;
		default:
			htt = HTTP_T_Header;
			break;
		}
		if (hp->hd[u].b >= hp->ws->s && hp->hd[u].e <= hp->ws->e) {
			WSLH(w, htt, fd, hp, u);
			continue;
		}
		l = hp->hd[u].e - hp->hd[u].b;
		p = WS_Alloc(hp->ws, l + 1);
		if (p != NULL) {
			WSLH(w, htt, fd, hp, u);
			memcpy(p, hp->hd[u].b, l + 1);
			hp->hd[u].b = p;
			hp->hd[u].e = p + l;
		} else {
			WSLH(w, HTTP_T_LostHeader, fd, hp, u);
			hp->hd[u].b = NULL;
			hp->hd[u].e = NULL;
		}
	}
}

/*--------------------------------------------------------------------*/

void
http_ClrHeader(struct http *to)
{

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	/* XXX ??? to->f = to->v;  Not sure this is valid */
	to->nhd = HTTP_HDR_FIRST;
	memset(to->hd, 0, sizeof to->hd);
}

/*--------------------------------------------------------------------*/

void
http_SetHeader(struct worker *w, int fd, struct http *to, const char *hdr)
{

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	if (to->nhd >= HTTP_HDR_MAX) {
		VSL_stats->losthdr++;
		WSL(w, http2shmlog(to, HTTP_T_LostHeader), fd, "%s", hdr);
		return;
	}
	http_seth(to, to->nhd++, hdr);
}

/*--------------------------------------------------------------------*/

static void
http_PutField(struct worker *w, int fd, struct http *to, int field, const char *string)
{
	const char *e;
	char *p;
	int l;

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	e = strchr(string, '\0');
	l = (e - string);
	p = WS_Alloc(to->ws, l + 1);
	if (p == NULL) {
		WSL(w, http2shmlog(to, HTTP_T_LostHeader), fd, "%s", string);
		to->hd[field].b = NULL;
		to->hd[field].e = NULL;
	} else {
		memcpy(p, string, l + 1);
		to->hd[field].b = p;
		to->hd[field].e = p + l;
	}
}

void
http_PutProtocol(struct worker *w, int fd, struct http *to, const char *protocol)
{

	http_PutField(w, fd, to, HTTP_HDR_PROTO, protocol);
}

void
http_PutStatus(struct worker *w, int fd, struct http *to, int status)
{
	char stat[4];

	assert(status >= 0 && status <= 999);
	sprintf(stat, "%d", status);
	http_PutField(w, fd, to, HTTP_HDR_STATUS, stat);
}

void
http_PutResponse(struct worker *w, int fd, struct http *to, const char *response)
{

	http_PutField(w, fd, to, HTTP_HDR_RESPONSE, response);
}

void
http_PrintfHeader(struct worker *w, int fd, struct http *to, const char *fmt, ...)
{
	va_list ap;
	unsigned l, n;

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	l = WS_Reserve(to->ws, 0);
	va_start(ap, fmt);
	n = vsnprintf(to->ws->f, l, fmt, ap);
	va_end(ap);
	if (n + 1 >= l || to->nhd >= HTTP_HDR_MAX) {
		VSL_stats->losthdr++;
		WSL(w, http2shmlog(to, HTTP_T_LostHeader), fd, "%s", to->ws->f);
		WS_Release(to->ws, 0);
	} else {
		to->hd[to->nhd].b = to->ws->f;
		to->hd[to->nhd].e = to->ws->f + n;
		WS_Release(to->ws, n + 1);
		to->nhd++;
	}
}
/*--------------------------------------------------------------------*/

void
http_Unset(struct http *hp, const char *hdr)
{
	unsigned u, v;

	for (v = u = HTTP_HDR_FIRST; u < hp->nhd; u++) {
		if (http_IsHdr(&hp->hd[u], hdr)) 
			continue;
		if (v != u)
			memcpy(&hp->hd[v], &hp->hd[u], sizeof hp->hd[v]);
		v++;
	}
	hp->nhd = v;
}

/*--------------------------------------------------------------------*/

unsigned
http_Write(struct worker *w, struct http *hp, int resp)
{
	unsigned u, l;

	if (resp) {
		AN(hp->hd[HTTP_HDR_STATUS].b);
		l = WRK_WriteH(w, &hp->hd[HTTP_HDR_PROTO], " ");
		WSLH(w, HTTP_T_Protocol, *w->wfd, hp, HTTP_HDR_PROTO);
		l += WRK_WriteH(w, &hp->hd[HTTP_HDR_STATUS], " ");
		WSLH(w, HTTP_T_Status, *w->wfd, hp, HTTP_HDR_STATUS);
		l += WRK_WriteH(w, &hp->hd[HTTP_HDR_RESPONSE], "\r\n");
		WSLH(w, HTTP_T_Response, *w->wfd, hp, HTTP_HDR_RESPONSE);
	} else {
		AN(hp->hd[HTTP_HDR_URL].b);
		l = WRK_WriteH(w, &hp->hd[HTTP_HDR_REQ], " ");
		WSLH(w, HTTP_T_Request, *w->wfd, hp, HTTP_HDR_REQ);
		l += WRK_WriteH(w, &hp->hd[HTTP_HDR_URL], " ");
		WSLH(w, HTTP_T_URL, *w->wfd, hp, HTTP_HDR_URL);
		l += WRK_WriteH(w, &hp->hd[HTTP_HDR_PROTO], "\r\n");
		WSLH(w, HTTP_T_Protocol, *w->wfd, hp, HTTP_HDR_PROTO);
	}
	for (u = HTTP_HDR_FIRST; u < hp->nhd; u++) {
		AN(hp->hd[u].b);
		AN(hp->hd[u].e);
		l += WRK_WriteH(w, &hp->hd[u], "\r\n");
		WSLH(w, HTTP_T_Header, *w->wfd, hp, u);
	}
	l += WRK_Write(w, "\r\n", -1);
	return (l);
}

/*--------------------------------------------------------------------*/

void
HTTP_Init(void)
{
#define HTTPH(a, b, c, d, e, f, g) b[0] = (char)strlen(b + 1);
#include "http_headers.h"
#undef HTTPH
}
