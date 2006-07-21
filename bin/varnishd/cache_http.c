/*
 * $Id$
 *
 * HTTP request storage and manipulation
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "libvarnish.h"
#include "shmlog.h"
#include "heritage.h"
#include "cache.h"

#define HTTPH(a, b, c, d, e, f, g) char b[] = "*" a ":";
#include "http_headers.h"
#undef HTTPH

#define VSLH(a, b, c, d) \
	VSLR((a), (b), (c)->hd[d][HTTP_START], (c)->hd[d][HTTP_END]);

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
		assert(hp->hd[u][HTTP_START] != NULL);
		assert(hp->hd[u][HTTP_END] != NULL);
		if (hp->hd[u][HTTP_END] < hp->hd[u][HTTP_START] + l)
			continue;
		if (hp->hd[u][HTTP_START][l-1] != ':')
			continue;
		if (strncasecmp(hdr, hp->hd[u][HTTP_START], l))
			continue;
		if (hp->hd[u][HTTP_DATA] == NULL) {
			p = hp->hd[u][HTTP_START] + l;
			while (isspace(*p))
				p++;
			hp->hd[u][HTTP_DATA] = p;
		}
		*ptr = hp->hd[u][HTTP_DATA];
		return (1);
	}
	*ptr = NULL;
	return (0);
}

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

int
http_GetStatus(struct http *hp)
{

	assert(hp->hd[HTTP_HDR_STATUS][HTTP_START] != NULL);
	return (strtoul(hp->hd[HTTP_HDR_STATUS][HTTP_START],
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
			hp->hd[hp->nhd][HTTP_START] = p;
			hp->hd[hp->nhd][HTTP_END] = q;
			VSLH(SLT_RxHeader, fd, hp, hp->nhd);
			hp->nhd++;
		} else {
			VSL_stats->losthdr++;
			VSLR(SLT_LostHeader, fd, p, q);
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
	for (p = hp->s ; isspace(*p); p++)
		continue;

	/* First, the request type (GET/HEAD etc) */
	hp->hd[HTTP_HDR_REQ][HTTP_START] = p;
	for (; isalpha(*p); p++)
		;
	hp->hd[HTTP_HDR_REQ][HTTP_END] = p;
	VSLH(SLT_Request, fd, hp, HTTP_HDR_REQ);
	*p++ = '\0';

	/* Next find the URI */
	while (isspace(*p) && *p != '\n')
		p++;
	if (*p == '\n') {
		VSLR(SLT_HttpGarbage, fd, hp->s, hp->v);
		return (400);
	}
	hp->hd[HTTP_HDR_URL][HTTP_START] = p;
	while (!isspace(*p))
		p++;
	hp->hd[HTTP_HDR_URL][HTTP_END] = p;
	VSLH(SLT_URL, fd, hp, HTTP_HDR_URL);
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
	hp->hd[HTTP_HDR_PROTO][HTTP_START] = p;
	while (!isspace(*p))
		p++;
	hp->hd[HTTP_HDR_PROTO][HTTP_END] = p;
	VSLH(SLT_Protocol, fd, hp, HTTP_HDR_PROTO);
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
	for (p = hp->s ; isspace(*p); p++)
		continue;

	/* First, protocol */
	hp->hd[HTTP_HDR_PROTO][HTTP_START] = p;
	while (!isspace(*p))
		p++;
	hp->hd[HTTP_HDR_PROTO][HTTP_END] = p;
	VSLH(SLT_Protocol, fd, hp, HTTP_HDR_PROTO);
	*p++ = '\0';

	/* Next find the status */
	while (isspace(*p))
		p++;
	hp->hd[HTTP_HDR_STATUS][HTTP_START] = p;
	while (!isspace(*p))
		p++;
	hp->hd[HTTP_HDR_STATUS][HTTP_END] = p;
	VSLH(SLT_Status, fd, hp, HTTP_HDR_STATUS);
	*p++ = '\0';

	/* Next find the response */
	while (isspace(*p))
		p++;
	hp->hd[HTTP_HDR_RESPONSE][HTTP_START] = p;
	while (*p != '\n')
		p++;
	for (q = p; q > hp->hd[HTTP_HDR_RESPONSE][HTTP_START] &&
	    isspace(q[-1]); q--)
		continue;
	*q = '\0';
	hp->hd[HTTP_HDR_RESPONSE][HTTP_END] = q;
	VSLH(SLT_Response, fd, hp, HTTP_HDR_RESPONSE);
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
	assert(hp->t <= hp->v);
	return (1);
}


/*--------------------------------------------------------------------*/

#include <errno.h>

static void
http_read_f(int fd, short event, void *arg)
{
	struct http *hp = arg;
	unsigned l;
	int i, ret = 0;

	(void)event;

	l = hp->e - hp->v;
	if (l <= 1) {
		VSL(SLT_HttpError, fd, "Received too much");
		VSLR(SLT_HttpGarbage, fd, hp->s, hp->v);
		hp->t = NULL;
		ret = 1;
	} else {
		errno = 0;
		i = read(fd, hp->v, l - 1);
		if (i > 0) {
			hp->v += i;
			*hp->v = '\0';
			if (!http_header_complete(hp))
				return;
		} else {
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
			ret = 2;
		}
	}

	assert(hp->t != NULL || ret != 0);
	event_del(&hp->ev);
	if (hp->callback != NULL)
		hp->callback(hp->arg, ret);
}

/*--------------------------------------------------------------------*/

void
http_RecvHead(struct http *hp, int fd, struct event_base *eb, http_callback_f *func, void *arg)
{
	unsigned l;

	assert(hp != NULL);
	assert(hp->v <= hp->e);
	assert(hp->t <= hp->v);
	if (0)
		VSL(SLT_Debug, fd, "Recv t %u v %u",
		    hp->t - hp->s, hp->v - hp->s);
	if (hp->t > hp->s && hp->t < hp->v) {
		l = hp->v - hp->t;
		memmove(hp->s, hp->t, l);
		hp->v = hp->s + l;
		hp->t = hp->s;
		*hp->v = '\0';
		if (http_header_complete(hp)) {
			assert(func != NULL);
			func(arg, 0);
			return;
		}
	} else  {
		hp->v = hp->s;
		hp->t = hp->s;
	}
	hp->callback = func;
	hp->arg = arg;
	event_set(&hp->ev, fd, EV_READ | EV_PERSIST, http_read_f, hp);
	AZ(event_base_set(eb, &hp->ev));
	AZ(event_add(&hp->ev, NULL));      /* XXX: timeout */
}

/*--------------------------------------------------------------------*/

static int
http_supress(const char *hdr, int flag)
{

#define HTTPH_0(a,d)
#define HTTPH_1(a,d)						\
	if ((flag & d) && !strncasecmp(hdr, a, strlen(a))) {	\
		return (1);					\
	}
#define HTTPH_2(a,d)		HTTPH_1(a,d)
#define HTTPH_3(a,d)		HTTPH_1(a,d)

#define HTTPH(a,b,c,d,e,f,g)	HTTPH_ ## d(a ":",d)
#include "http_headers.h"
#undef HTTPH
#undef HTTPH_0
#undef HTTPH_1
#undef HTTPH_2
#undef HTTPH_3

	return (0);
}

/*--------------------------------------------------------------------*/

void
http_BuildSbuf(int fd, enum http_build mode, struct sbuf *sb, struct http *hp)
{
	unsigned u, sup, rr;

	sbuf_clear(sb);
	assert(sb != NULL);
	switch (mode) {
	case Build_Reply: rr = 0; sup = 2; break;
	case Build_Pipe:  rr = 1; sup = 0; break;
	case Build_Pass:  rr = 1; sup = 2; break;
	case Build_Fetch: rr = 2; sup = 1; break;
	default:
		sup = 0;	/* for flexelint */
		rr = 0;	/* for flexelint */
		printf("mode = %d\n", mode);
		assert(__LINE__ == 0);
	}
	if (rr == 0) {
		sbuf_cat(sb, hp->hd[HTTP_HDR_PROTO][HTTP_START]);
		sbuf_cat(sb, " ");
		sbuf_cat(sb, hp->hd[HTTP_HDR_STATUS][HTTP_START]);
		sbuf_cat(sb, " ");
		sbuf_cat(sb, hp->hd[HTTP_HDR_RESPONSE][HTTP_START]);
	} else {
		if (rr == 2) {
			sbuf_cat(sb, "GET ");
		} else {
			sbuf_cat(sb, hp->hd[HTTP_HDR_REQ][HTTP_START]);
			sbuf_cat(sb, " ");
		}
		sbuf_cat(sb, hp->hd[HTTP_HDR_URL][HTTP_START]);
		sbuf_cat(sb, " ");
		sbuf_cat(sb, hp->hd[HTTP_HDR_PROTO][HTTP_START]);
	}

	sbuf_cat(sb, "\r\n");

	for (u = HTTP_HDR_FIRST; u < hp->nhd; u++) {
		if (http_supress(hp->hd[u][HTTP_START], sup))
			continue;
		if (1)
			VSL(SLT_TxHeader, fd, "%s", hp->hd[u][HTTP_START]);
		sbuf_cat(sb, hp->hd[u][HTTP_START]);
		sbuf_cat(sb, "\r\n");
	}
	if (mode != Build_Reply) {
		sbuf_cat(sb, "\r\n");
		sbuf_finish(sb);
	}
}

void
HTTP_Init(void)
{
#define HTTPH(a, b, c, d, e, f, g) b[0] = strlen(b + 1);
#include "http_headers.h"
#undef HTTPH
}
