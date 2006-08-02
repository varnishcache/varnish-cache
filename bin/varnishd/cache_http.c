/*
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

#include "libvarnish.h"
#include "shmlog.h"
#include "cache.h"

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
T(struct http *hp, enum httptag t)
{

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	assert(hp->logtag >= HTTP_Rx && hp->logtag <= HTTP_Obj);
	assert(t >= HTTP_T_Request && t <= HTTP_T_LostHeader);
	return (logmtx[hp->logtag][t]);
}

#define VSLH(ax, bx, cx, dx) \
	VSLR(T((cx), (ax)), (bx), (cx)->hd[(dx)].b, (cx)->hd[(dx)].e);

/*--------------------------------------------------------------------*/

void
http_Setup(struct http *hp, void *space, unsigned len)
{
	char *sp = space;

	assert(len > 0);
	memset(hp, 0, sizeof *hp);
	hp->magic = HTTP_MAGIC;
	hp->s = sp;
	hp->t = sp;
	hp->v = sp;
	hp->f = sp;
	hp->e = sp + len;
	hp->nhd = HTTP_HDR_FIRST;
}

/*--------------------------------------------------------------------*/


int
http_IsHdr(struct http_hdr *hh, char *hdr)
{
	unsigned l;

	assert(hh->b != NULL);
	assert(hh->e != NULL);
	assert(hdr != NULL);
	l = hdr[0];
	assert(l == strlen(hdr + 1));
	assert(hdr[l] == ':');
	hdr++;
	return (!strncasecmp(hdr, hh->b, l));
}

/*--------------------------------------------------------------------*/

int
http_GetHdr(struct http *hp, const char *hdr, char **ptr)
{
	unsigned u, l;
	char *p;

	l = hdr[0];
	assert(l == strlen(hdr + 1));
	assert(hdr[l] == ':');
	hdr++;
	for (u = HTTP_HDR_FIRST; u < hp->nhd; u++) {
		assert(hp->hd[u].b != NULL);
		assert(hp->hd[u].e != NULL);
		if (hp->hd[u].e < hp->hd[u].b + l)
			continue;
		if (hp->hd[u].b[l-1] != ':')
			continue;
		if (strncasecmp(hdr, hp->hd[u].b, l))
			continue;
		p = hp->hd[u].b + l;
		while (isspace(*p))
			p++;
		*ptr = p;
		return (1);
	}
	*ptr = NULL;
	return (0);
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

int
http_HdrIs(struct http *hp, const char *hdr, const char *val)
{
	char *p;

	if (!http_GetHdr(hp, hdr, &p))
		return (0);
	assert(p != NULL);
	if (!strcasecmp(p, val))
		return (1);
	return (0);
}

/*--------------------------------------------------------------------*/

int
http_GetTail(struct http *hp, unsigned len, char **b, char **e)
{

	if (hp->t >= hp->v)
		return (0);

	if (len == 0)
		len = hp->v - hp->t;

	if (hp->t + len > hp->v)
		len = hp->v - hp->t;
	if (len == 0) 
		return (0);
	*b = hp->t;
	*e = hp->t + len;
	hp->t += len;
	assert(hp->t <= hp->v);
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
	if (hp->t < hp->v) {
		u = hp->v - hp->t;
		if (u > len)
			u = len;
		memcpy(b, hp->t, u);
		hp->t += u;
		b += u;
		len -= u;
	}
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

	assert(hp->hd[HTTP_HDR_STATUS].b != NULL);
	return (strtoul(hp->hd[HTTP_HDR_STATUS].b,
	    NULL /* XXX */, 10));
}

/*--------------------------------------------------------------------
 * Dissect the headers of the HTTP protocol message.
 * Detect conditionals (headers which start with '^[Ii][Ff]-')
 */

static int
http_dissect_hdrs(struct http *hp, int fd, char *p)
{
	char *q, *r;

	if (*p == '\r')
		p++;

	hp->nhd = HTTP_HDR_FIRST;
	hp->conds = 0;
	r = NULL;		/* For FlexeLint */
	assert(p < hp->v);	/* http_header_complete() guarantees this */
	for (; p < hp->v; p = r) {
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

		if (hp->nhd < MAX_HTTP_HDRS) {
			hp->hd[hp->nhd].b = p;
			hp->hd[hp->nhd].e = q;
			VSLH(HTTP_T_Header, fd, hp, hp->nhd);
			hp->nhd++;
		} else {
			VSL_stats->losthdr++;
			VSLR(T(hp, HTTP_T_LostHeader), fd, p, q);
		}
	}
	assert(hp->t <= hp->v);
	assert(hp->t == r);
	return (0);
}

/*--------------------------------------------------------------------*/

int
http_DissectRequest(struct http *hp, int fd)
{
	char *p;

	assert(hp->t != NULL);
	assert(hp->s < hp->t);
	assert(hp->t <= hp->v);
	hp->logtag = HTTP_Rx;

	for (p = hp->s ; isspace(*p); p++)
		continue;

	/* First, the request type (GET/HEAD etc) */
	hp->hd[HTTP_HDR_REQ].b = p;
	for (; isalpha(*p); p++)
		;
	hp->hd[HTTP_HDR_REQ].e = p;
	VSLH(HTTP_T_Request, fd, hp, HTTP_HDR_REQ);
	*p++ = '\0';

	/* Next find the URI */
	while (isspace(*p) && *p != '\n')
		p++;
	if (*p == '\n') {
		VSLR(SLT_HttpGarbage, fd, hp->s, hp->v);
		return (400);
	}
	hp->hd[HTTP_HDR_URL].b = p;
	while (!isspace(*p))
		p++;
	hp->hd[HTTP_HDR_URL].e = p;
	VSLH(HTTP_T_URL, fd, hp, HTTP_HDR_URL);
	if (*p == '\n') {
		VSLR(SLT_HttpGarbage, fd, hp->s, hp->v);
		return (400);
	}
	*p++ = '\0';

	/* Finally, look for protocol */
	while (isspace(*p) && *p != '\n')
		p++;
	if (*p == '\n') {
		VSLR(SLT_HttpGarbage, fd, hp->s, hp->v);
		return (400);
	}
	hp->hd[HTTP_HDR_PROTO].b = p;
	while (!isspace(*p))
		p++;
	hp->hd[HTTP_HDR_PROTO].e = p;
	VSLH(HTTP_T_Protocol, fd, hp, HTTP_HDR_PROTO);
	if (*p != '\n')
		*p++ = '\0';
	while (isspace(*p) && *p != '\n')
		p++;
	if (*p != '\n') {
		VSLR(SLT_HttpGarbage, fd, hp->s, hp->v);
		return (400);
	}
	*p++ = '\0';

	return (http_dissect_hdrs(hp, fd, p));
}

/*--------------------------------------------------------------------*/

int
http_DissectResponse(struct http *hp, int fd)
{
	char *p, *q;

	assert(hp->t != NULL);
	assert(hp->s < hp->t);
	assert(hp->t <= hp->v);
	hp->logtag = HTTP_Rx;

	for (p = hp->s ; isspace(*p); p++)
		continue;

	/* First, protocol */
	hp->hd[HTTP_HDR_PROTO].b = p;
	while (!isspace(*p))
		p++;
	hp->hd[HTTP_HDR_PROTO].e = p;
	VSLH(HTTP_T_Protocol, fd, hp, HTTP_HDR_PROTO);
	*p++ = '\0';

	/* Next find the status */
	while (isspace(*p))
		p++;
	hp->hd[HTTP_HDR_STATUS].b = p;
	while (!isspace(*p))
		p++;
	hp->hd[HTTP_HDR_STATUS].e = p;
	VSLH(HTTP_T_Status, fd, hp, HTTP_HDR_STATUS);
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
	VSLH(HTTP_T_Response, fd, hp, HTTP_HDR_RESPONSE);
	p++;

	return (http_dissect_hdrs(hp, fd, p));
}

/*--------------------------------------------------------------------*/

static int
http_header_complete(struct http *hp)
{
	char *p;

	assert(hp->v <= hp->e);
	assert(*hp->v == '\0');
	/* Skip any leading white space */
	for (p = hp->s ; p < hp->v && isspace(*p); p++)
		continue;
	if (p >= hp->v) {
		hp->v = hp->s;
		return (0);
	}
	while (1) {
		/* XXX: we could save location of all linebreaks for later */
		p = strchr(p, '\n');
		if (p == NULL)
			return (0);
		p++;
		if (*p == '\r')
			p++;
		if (*p != '\n')
			continue;
		break;
	}
	if (++p > hp->v)
		return (0);
	hp->t = p;
	assert(hp->t > hp->s);
	assert(hp->t <= hp->v);
	return (1);
}

/*--------------------------------------------------------------------*/

void
http_RecvPrep(struct http *hp)
{
	unsigned l;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	assert(hp->v <= hp->e);
	assert(hp->t <= hp->v);
	if (hp->t > hp->s && hp->t < hp->v) {
		l = hp->v - hp->t;
		memmove(hp->s, hp->t, l);
		hp->v = hp->s + l;
		hp->t = hp->s;
		*hp->v = '\0';
	} else  {
		hp->v = hp->s;
		hp->t = hp->s;
	}
}

int
http_RecvPrepAgain(struct http *hp)
{
	http_RecvPrep(hp);
	if (hp->v == hp->s)
		return (0);
	return (http_header_complete(hp));
}

/*--------------------------------------------------------------------*/

int
http_RecvSome(int fd, struct http *hp)
{
	unsigned l;
	int i;

	l = (hp->e - hp->s) / 2;
	if (l < hp->v - hp->s)
		l = 0;
	else
		l -= hp->v - hp->s;
	if (l <= 1) {
		VSL(SLT_HttpError, fd, "Received too much");
		VSLR(SLT_HttpGarbage, fd, hp->s, hp->v);
		hp->t = NULL;
		return (1);
	}
	errno = 0;
	i = read(fd, hp->v, l - 1);
	if (i > 0) {
		hp->v += i;
		*hp->v = '\0';
		if (http_header_complete(hp))
			return(0);
		return (-1);
	} 

	if (hp->v != hp->s) {
		VSL(SLT_HttpError, fd,
		    "Received (only) %d bytes, errno %d",
		    hp->v - hp->s, errno);
		VSLR(SLT_Debug, fd, hp->s, hp->v);
	} else if (errno == 0)
		VSL(SLT_HttpError, fd, "Received nothing");
	else
		VSL(SLT_HttpError, fd,
		    "Received errno %d", errno);
	hp->t = NULL;
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

/*--------------------------------------------------------------------
 * Copy HTTP headers into malloc'ed space.
 */

void
http_CopyHttp(struct http *to, struct http *fm)
{
	unsigned u, l;

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(fm, HTTP_MAGIC);
	l = 0;
	for (u = 0; u < fm->nhd; u++) {
		if (fm->hd[u].b == NULL)
			continue;
		assert(fm->hd[u].e != NULL);
		l += (fm->hd[u].e - fm->hd[u].b) + 1;
	}
	to->s = malloc(l);
	assert(to->s != NULL);
	to->e = to->s + l;
	to->f = to->s;
	for (u = 0; u < fm->nhd; u++) {
		if (fm->hd[u].b == NULL)
			continue;
		assert(fm->hd[u].e != NULL);
		assert(*fm->hd[u].e == '\0');
		l = fm->hd[u].e - fm->hd[u].b;
		assert(l == strlen(fm->hd[u].b));
		memcpy(to->f, fm->hd[u].b, l);
		to->hd[u].b = to->f;
		to->hd[u].e = to->f + l;
		*to->hd[u].e = '\0';
		to->f += l + 1;
	}
	to->nhd = fm->nhd;
}

/*--------------------------------------------------------------------*/

static void
http_seth(int fd, struct http *to, unsigned n, enum httptag tag, const char *fm)
{
	assert(n < MAX_HTTP_HDRS);
	assert(fm != NULL);
	to->hd[n].b = (void*)(uintptr_t)fm;
	to->hd[n].e = (void*)(uintptr_t)strchr(fm, '\0');
	VSLH(tag, fd, to, n);
}

static void
http_copyh(int fd, struct http *to, struct http *fm, unsigned n, enum httptag tag)
{

	assert(n < MAX_HTTP_HDRS);
	assert(fm->hd[n].b != NULL);
	to->hd[n].b = fm->hd[n].b;
	to->hd[n].e = fm->hd[n].e;
	VSLH(tag, fd, to, n);
}

void
http_GetReq(int fd, struct http *to, struct http *fm)
{

	CHECK_OBJ_NOTNULL(fm, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	http_seth(fd, to, HTTP_HDR_REQ, HTTP_T_Request, "GET");
	http_copyh(fd, to, fm, HTTP_HDR_URL, HTTP_T_URL);
	http_seth(fd, to, HTTP_HDR_PROTO, HTTP_T_Protocol, "HTTP/1.1");
}

void
http_CopyReq(int fd, struct http *to, struct http *fm)
{

	CHECK_OBJ_NOTNULL(fm, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	http_copyh(fd, to, fm, HTTP_HDR_REQ, HTTP_T_Request);
	http_copyh(fd, to, fm, HTTP_HDR_URL, HTTP_T_URL);
	http_copyh(fd, to, fm, HTTP_HDR_PROTO, HTTP_T_Protocol);
}


void
http_CopyResp(int fd, struct http *to, struct http *fm)
{

	CHECK_OBJ_NOTNULL(fm, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	http_copyh(fd, to, fm, HTTP_HDR_PROTO, HTTP_T_Protocol);
	http_copyh(fd, to, fm, HTTP_HDR_STATUS, HTTP_T_Status);
	http_copyh(fd, to, fm, HTTP_HDR_RESPONSE, HTTP_T_Response);
}

void
http_SetResp(int fd, struct http *to, const char *proto, const char *status, const char *response)
{
	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	http_seth(fd, to, HTTP_HDR_PROTO, HTTP_T_Protocol, proto);
	http_seth(fd, to, HTTP_HDR_STATUS, HTTP_T_Status, status);
	http_seth(fd, to, HTTP_HDR_RESPONSE, HTTP_T_Response, response);
}

static void
http_copyheader(int fd, struct http *to, struct http *fm, unsigned n)
{

	CHECK_OBJ_NOTNULL(fm, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	assert(n < MAX_HTTP_HDRS);
	assert(fm->hd[n].b != NULL);
	if (to->nhd < MAX_HTTP_HDRS) {
		to->hd[to->nhd].b = fm->hd[n].b;
		to->hd[to->nhd].e = fm->hd[n].e;
		VSLH(HTTP_T_Header, fd, to, to->nhd);
		to->nhd++;
	} else  {
		VSL_stats->losthdr++;
		VSLH(HTTP_T_LostHeader, fd, fm, n);
	}
}

/*--------------------------------------------------------------------*/

void
http_FilterHeader(int fd, struct http *to, struct http *fm, unsigned how)
{
	unsigned u;

	CHECK_OBJ_NOTNULL(fm, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	to->nhd = HTTP_HDR_FIRST;
	for (u = HTTP_HDR_FIRST; u < fm->nhd; u++) {
#define HTTPH(a, b, c, d, e, f, g) \
		if (((e) & how) && http_IsHdr(&fm->hd[u], (b))) \
			continue;
#include "http_headers.h"
#undef HTTPH
		http_copyheader(fd, to, fm, u);
	}
}

/*--------------------------------------------------------------------*/

void
http_ClrHeader(struct http *to)
{

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	to->f = to->v;
	to->nhd = HTTP_HDR_FIRST;
	memset(to->hd, 0, sizeof to->hd);
}

/*--------------------------------------------------------------------*/

void
http_SetHeader(int fd, struct http *to, const char *hdr)
{

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	if (to->nhd >= MAX_HTTP_HDRS) {
		VSL_stats->losthdr++;
		VSL(T(to, HTTP_T_LostHeader), fd, "%s", hdr);
		return;
	}
	http_seth(fd, to, to->nhd++, HTTP_T_Header, hdr);
}

/*--------------------------------------------------------------------*/

void
http_PrintfHeader(int fd, struct http *to, const char *fmt, ...)
{
	va_list ap;
	unsigned l, n;

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	va_start(ap, fmt);
	l = to->e - to->f;
	n = vsnprintf(to->f, l, fmt, ap);
	if (n + 1 > l || to->nhd >= MAX_HTTP_HDRS) {
		VSL_stats->losthdr++;
		VSL(T(to, HTTP_T_LostHeader), fd, "%s", to->f);
	} else {
		assert(to->f < to->e);
		to->hd[to->nhd].b = to->f;
		to->hd[to->nhd].e = to->f + n;
		to->f += n + 1;
		VSLH(HTTP_T_Header, fd, to, to->nhd);
		to->nhd++;
	}
	va_end(ap);
}

/*--------------------------------------------------------------------*/

unsigned
http_Write(struct worker *w, struct http *hp, int resp)
{
	unsigned u, l;

	if (resp) {
		assert(hp->hd[HTTP_HDR_STATUS].b != NULL);
		l = WRK_WriteH(w, &hp->hd[HTTP_HDR_PROTO], " ");
		l += WRK_WriteH(w, &hp->hd[HTTP_HDR_STATUS], " ");
		l += WRK_WriteH(w, &hp->hd[HTTP_HDR_RESPONSE], "\r\n");
	} else {
		assert(hp->hd[HTTP_HDR_URL].b != NULL);
		l = WRK_WriteH(w, &hp->hd[HTTP_HDR_REQ], " ");
		l += WRK_WriteH(w, &hp->hd[HTTP_HDR_URL], " ");
		l += WRK_WriteH(w, &hp->hd[HTTP_HDR_PROTO], "\r\n");
	}
	for (u = HTTP_HDR_FIRST; u < hp->nhd; u++) {
		assert(hp->hd[u].b != NULL);
		assert(hp->hd[u].e != NULL);
		l += WRK_WriteH(w, &hp->hd[u], "\r\n");
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
