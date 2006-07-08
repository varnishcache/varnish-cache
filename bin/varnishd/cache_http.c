/*
 * $Id$
 *
 * HTTP request storage and manipulation
 */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>
#include <event.h>
#include <sbuf.h>

#include "libvarnish.h"
#include "shmlog.h"
#include "cache.h"

static unsigned		http_bufsize	= 4096;
static unsigned		http_nhdr	= 128;

/*--------------------------------------------------------------------*/

struct http *
http_New(void)
{
	struct http *hp;

	hp = calloc(sizeof *hp, 1);
	assert(hp != NULL);
	VSL_stats->n_http++;

	hp->s = malloc(http_bufsize);
	assert(hp->s != NULL);

	hp->e = hp->s + http_bufsize;
	hp->v = hp->s;
	hp->t = hp->s;

	hp->hdr = malloc(sizeof *hp->hdr * http_nhdr);
	assert(hp->hdr != NULL);

	return (hp);
}

void
http_Delete(struct http *hp)
{

	free(hp->hdr);
	free(hp->s);
	free(hp);
	VSL_stats->n_http--;
}

/*--------------------------------------------------------------------*/

int
http_GetHdr(struct http *hp, const char *hdr, char **ptr)
{
	unsigned u, l;
	char *p;

	l = strlen(hdr);
	for (u = 0; u < hp->nhdr; u++) {
		if (strncasecmp(hdr, hp->hdr[u], l))
			continue;
		p = hp->hdr[u];
		if (p[l] != ':')
			continue;
		p += l + 1;
		while (isspace(*p))
			p++;
		*ptr = p;
		return (1);
	}
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
http_GetReq(struct http *hp, char **b)
{
	if (hp->req == NULL)
		return (0);
	*b = hp->req;
	return (1);
}

int
http_GetURL(struct http *hp, char **b)
{
	if (hp->url == NULL)
		return (0);
	*b = hp->url;
	return (1);
}

int
http_GetProto(struct http *hp, char **b)
{
	if (hp->proto == NULL)
		return (0);
	*b = hp->proto;
	return (1);
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

int
http_GetStatus(struct http *hp)
{

	return (strtoul(hp->status, NULL /* XXX */, 10));
}

/*--------------------------------------------------------------------*/

int
http_Dissect(struct http *hp, int fd, int rr)
{
	char *p, *q, *r;

	assert(hp->t != NULL);
	assert(hp->s < hp->t);
	assert(hp->t <= hp->v);
	for (p = hp->s ; isspace(*p); p++)
		continue;
	if (rr == 1) {
		/* First, the request type (GET/HEAD etc) */
		hp->req = p;
		for (; isalpha(*p); p++)
			;
		VSLR(SLT_Request, fd, hp->req, p);
		*p++ = '\0';

		/* Next find the URI */
		while (isspace(*p) && *p != '\n')
			p++;
		if (*p == '\n') {
			VSLR(SLT_Debug, fd, hp->s, hp->v);
			return (400);
		}
		hp->url = p;
		while (!isspace(*p))
			p++;
		VSLR(SLT_URL, fd, hp->url, p);
		if (*p == '\n') {
			VSLR(SLT_Debug, fd, hp->s, hp->v);
			return (400);
		}
		*p++ = '\0';

		/* Finally, look for protocol */
		while (isspace(*p) && *p != '\n')
			p++;
		if (*p == '\n') {
			VSLR(SLT_Debug, fd, hp->s, hp->v);
			return (400);
		}
		hp->proto = p;
		while (!isspace(*p))
			p++;
		VSLR(SLT_Protocol, fd, hp->proto, p);
		if (*p != '\n')
			*p++ = '\0';
		while (isspace(*p) && *p != '\n')
			p++;
		if (*p != '\n') {
			VSLR(SLT_Debug, fd, hp->s, hp->v);
			return (400);
		}
		*p++ = '\0';
	} else {
		/* First, protocol */
		hp->proto = p;
		while (!isspace(*p))
			p++;
		VSLR(SLT_Protocol, fd, hp->proto, p);
		*p++ = '\0';

		/* Next find the status */
		while (isspace(*p))
			p++;
		hp->status = p;
		while (!isspace(*p))
			p++;
		VSLR(SLT_Status, fd, hp->status, p);
		*p++ = '\0';

		/* Next find the response */
		while (isspace(*p))
			p++;
		hp->response = p;
		while (*p != '\n')
			p++;
		for (q = p; q > hp->response && isspace(q[-1]); q--)
			continue;
		*q = '\0';
		VSLR(SLT_Response, fd, hp->response, q);
		p++;
	}

	if (*p == '\r')
		p++;

	hp->nhdr = 0;
	for (; p < hp->v; p = r) {
		q = strchr(p, '\n');
		assert(q != NULL);
		r = q + 1;
		if (q > p && q[-1] == '\r')
			q--;
		*q = '\0';
		if (p == q)
			break;

		if (hp->nhdr < http_nhdr) {
			hp->hdr[hp->nhdr++] = p;
			VSLR(SLT_Header, fd, p, q);
		} else {
			VSLR(SLT_LostHeader, fd, p, q);
		}
	}
	assert(hp->t <= hp->v);
	if (hp->t != r)
		printf("hp->t %p r %p\n", hp->t, r);
	assert(hp->t == r);
	return (0);
}

/*--------------------------------------------------------------------*/

static int
http_header_complete(struct http *hp)
{
	char *p;

	for (p = hp->s ; p < hp->v && isspace(*p); p++)
		continue;
	if (p >= hp->v)
		return (0);
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
	char *p;
	unsigned l;
	int i;

	l = hp->e - hp->v;
	if (l <= 1) {
		VSLR(SLT_Debug, fd, hp->s, hp->v);
		hp->t = NULL;
		event_del(&hp->ev);
		if (hp->callback != NULL)
			hp->callback(hp->arg, 1);
		return;
	}
	assert(l > 1);
	errno = 0;
	i = read(fd, hp->v, l - 1);
	if (i <= 0) {
		if (hp->v != hp->s)
			VSL(SLT_HttpError, fd,
			    "Received (only) %d bytes, errno %d",
			    hp->v - hp->s, errno);
		else if (errno == 0)
			VSL(SLT_HttpError, fd, "Received nothing");
		else
			VSL(SLT_HttpError, fd, "Received errno %d", errno);
		VSLR(SLT_Debug, fd, hp->s, hp->v);
		hp->t = NULL;
		event_del(&hp->ev);
		if (hp->callback != NULL)
			hp->callback(hp->arg, 2);
		return;
	}

	hp->v += i;
	*hp->v = '\0';
	if (!http_header_complete(hp))
		return;

	assert(hp->t != NULL);
	event_del(&hp->ev);
	if (hp->callback != NULL)
		hp->callback(hp->arg, 0);
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
	VSL(SLT_Debug, fd, "Recv t %u v %u", hp->t - hp->s, hp->v - hp->s);
	if (hp->t > hp->s && hp->t < hp->v) {
		l = hp->v - hp->t;
		memmove(hp->s, hp->t, l);
		hp->v = hp->s + l;
		hp->t = hp->s;
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
	event_base_set(eb, &hp->ev);
	event_add(&hp->ev, NULL);      /* XXX: timeout */
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
	unsigned u, sup;

	sbuf_clear(sb);
	assert(sb != NULL);
	switch (mode) {
	case Build_Reply:
		sbuf_cat(sb, hp->proto);
		sbuf_cat(sb, " ");
		sbuf_cat(sb, hp->status);
		sbuf_cat(sb, " ");
		sbuf_cat(sb, hp->response);
		sup = 2;
		break;
	case Build_Pipe:
		sbuf_cat(sb, hp->req);
		sbuf_cat(sb, " ");
		sbuf_cat(sb, hp->url);
		sbuf_cat(sb, " ");
		sbuf_cat(sb, hp->proto);
		sup = 0;
		break;
	case Build_Pass:
		sbuf_cat(sb, hp->req);
		sbuf_cat(sb, " ");
		sbuf_cat(sb, hp->url);
		sbuf_cat(sb, " ");
		sbuf_cat(sb, hp->proto);
		sup = 2;
		break;
	case Build_Fetch:
		sbuf_cat(sb, "GET ");
		sbuf_cat(sb, hp->url);
		sbuf_cat(sb, " ");
		sbuf_cat(sb, hp->proto);
		sup = 1;
		break;
	default:
		printf("mode = %d\n", mode);
		assert(mode == 1 || mode == 2);
	}
	sbuf_cat(sb, "\r\n");

	for (u = 0; u < hp->nhdr; u++) {
		if (http_supress(hp->hdr[u], sup))
			continue;
		if (1)
			VSL(SLT_BldHdr, fd, "%s", hp->hdr[u]);
		sbuf_cat(sb, hp->hdr[u]);
		sbuf_cat(sb, "\r\n");
	}
	if (mode != Build_Reply) {
		sbuf_cat(sb, "\r\n");
		sbuf_finish(sb);
	}
}
