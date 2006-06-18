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
#include "vcl_lang.h"
#include "cache.h"

static unsigned		http_bufsize	= 4096;
static unsigned		http_nhdr	= 128;

/*--------------------------------------------------------------------*/

struct http {
	struct event		ev;
	http_callback_f		*callback;
	void			*arg;

	char			*s;		/* start of buffer */
	char			*e;		/* end of buffer */
	char			*v;		/* valid bytes */
	char			*t;		/* start of trailing data */


	char			*req;
	char			*url;
	char			*proto;
	char			*status;
	char			*response;
	
	char			**hdr;
	unsigned		nhdr;
};

/*--------------------------------------------------------------------*/

struct http *
http_New(void)
{
	struct http *hp;

	hp = calloc(sizeof *hp, 1);
	assert(hp != NULL);

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
	return (1);
}

int
http_GetStatus(struct http *hp)
{

	return (strtoul(hp->status, NULL /* XXX */, 10));
}

/*--------------------------------------------------------------------*/

void
http_Dissect(struct http *hp, int fd, int rr)
{
	char *p, *q, *r;

	assert(hp->t != NULL);
	if (rr == 1) {
		/* First, isolate and possibly identify request type */
		hp->req = hp->s;
		for (p = hp->s; isalpha(*p); p++)
			;
		VSLR(SLT_Request, fd, hp->req, p);
		*p++ = '\0';

		/* Next find the URI */
		while (isspace(*p))
			p++;
		hp->url = p;
		while (!isspace(*p))
			p++;
		VSLR(SLT_URL, fd, hp->url, p);
		*p++ = '\0';

		/* Finally, look for protocol, if any */
		while (isspace(*p) && *p != '\n')
			p++;
		hp->proto = p;
		if (*p != '\n') {
			while (!isspace(*p))
				p++;
		}
		VSLR(SLT_Protocol, fd, hp->proto, p);
		*p++ = '\0';

		while (isspace(*p) && *p != '\n')
			p++;
		p++;
	} else {
		/* First, protocol */
		hp->proto = hp->s;
		p = hp->s;
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
	if (*++p == '\r')
		p++;
	hp->t = ++p;
}

/*--------------------------------------------------------------------*/

#include <errno.h>

static void
http_read_f(int fd, short event, void *arg)
{
	struct http *hp = arg;
	char *p;
	int i;

	assert(hp->v < hp->e);
	errno = 0;
	i = read(fd, hp->v, hp->e - hp->v);
	if (i <= 0) {
		if (hp->v != hp->s)
			VSL(SLT_HttpError, fd,
			    "Received (only) %d bytes, errno %d",
			    hp->v - hp->s, errno);
		else if (errno == 0)
			VSL(SLT_HttpError, fd, "Received nothing");
		else
			VSL(SLT_HttpError, fd, "Received errno %d", errno);
		hp->t = NULL;
		event_del(&hp->ev);
		if (hp->callback != NULL)
			hp->callback(hp->arg, 0);
		return;
	}

	hp->v += i;
	*hp->v = '\0';

	p = hp->s;
	while (1) {
		/* XXX: we could save location of all linebreaks for later */
		p = strchr(p, '\n');
		if (p == NULL)
			return;
		p++;
		if (*p == '\r')
			p++;
		if (*p != '\n')
			continue;
		break;
	}
	hp->t = ++p;

#if 0
printf("Head:\n%#H\n", hp->s, hp->t - hp->s);
printf("Tail:\n%#H\n", hp->t, hp->v - hp->t);
#endif

	event_del(&hp->ev);
	if (hp->callback != NULL)
		hp->callback(hp->arg, 1);
}

/*--------------------------------------------------------------------*/

void
http_RecvHead(struct http *hp, int fd, struct event_base *eb, http_callback_f *func, void *arg)
{

	assert(hp != NULL);
	VSL(SLT_Debug, fd, "%s s %p t %p v %p", __func__, hp->s, hp->t, hp->v);
	assert(hp->t == hp->s || hp->t == hp->v);	/* XXX pipelining */
	hp->callback = func;
	hp->arg = arg;
	hp->v = hp->s;
	hp->t = hp->s;
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
http_BuildSbuf(int resp, struct sbuf *sb, struct http *hp)
{
	unsigned u;

	sbuf_clear(sb);
	assert(sb != NULL);
	if (resp == 2 || resp == 3) {
		sbuf_cat(sb, hp->proto);
		sbuf_cat(sb, " ");
		sbuf_cat(sb, hp->status);
		sbuf_cat(sb, " ");
		sbuf_cat(sb, hp->response);
	} else if (resp == 1) {
		sbuf_cat(sb, hp->req);
		sbuf_cat(sb, " ");
		sbuf_cat(sb, hp->url);
		sbuf_cat(sb, " ");
		sbuf_cat(sb, hp->proto);
	} else {
		printf("resp = %d\n", resp);
		assert(resp == 1 || resp == 2);
	}
	sbuf_cat(sb, "\r\n");

	for (u = 0; u < hp->nhdr; u++) {
		if (http_supress(hp->hdr[u], resp))
			continue;
		if (1)
			VSL(SLT_Debug, 0, "Build %s", hp->hdr[u]);
		sbuf_cat(sb, hp->hdr[u]);
		sbuf_cat(sb, "\r\n");
	}
	if (resp != 3)
		sbuf_cat(sb, "\r\n");
	sbuf_finish(sb);
}
