/*
 * $Id$
 *
 * Stuff relating to HTTP server side
 */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>
#include <event.h>

#include "libvarnish.h"
#include "shmlog.h"
#include "vcl_lang.h"
#include "cache.h"

/*--------------------------------------------------------------------*/

void
HttpdAnalyze(struct sess *sp, int rr)
{
	char *p, *q, *r;

	sp->handling = HND_Unclass;

	memset(&sp->http, 0, sizeof sp->http);

	if (rr == 1) {
		/* First, isolate and possibly identify request type */
		sp->http.req = sp->rcv;
		for (p = sp->rcv; isalpha(*p); p++)
			;
		VSLR(SLT_Request, sp->fd, sp->http.req, p);
		*p++ = '\0';

		/* Next find the URI */
		while (isspace(*p))
			p++;
		sp->http.url = p;
		while (!isspace(*p))
			p++;
		VSLR(SLT_URL, sp->fd, sp->http.url, p);
		*p++ = '\0';

		/* Finally, look for protocol, if any */
		while (isspace(*p) && *p != '\n')
			p++;
		sp->http.proto = p;
		if (*p != '\n') {
			while (!isspace(*p))
				p++;
		}
		VSLR(SLT_Protocol, sp->fd, sp->http.proto, p);
		*p++ = '\0';

		while (isspace(*p) && *p != '\n')
			p++;
		p++;
	} else {
		/* First, protocol */
		sp->http.proto = sp->rcv;
		p = sp->rcv;
		while (!isspace(*p))
			p++;
		*p++ = '\0';
		VSLR(SLT_Protocol, sp->fd, sp->http.proto, p);

		/* Next find the status */
		while (isspace(*p))
			p++;
		sp->http.status = p;
		while (!isspace(*p))
			p++;
		VSLR(SLT_Status, sp->fd, sp->http.status, p);
		*p++ = '\0';

		/* Next find the response */
		while (isspace(*p))
			p++;
		sp->http.response = p;
		while (*p != '\n')
			p++;
		for (q = p; q > sp->http.response && isspace(q[-1]); q--)
			continue;
		*q = '\0';
		VSLR(SLT_Response, sp->fd, sp->http.response, q);
		p++;
	}

	if (*p == '\r')
		p++;

	for (; p < sp->rcv + sp->rcv_len; p = r) {
		q = strchr(p, '\n');
		r = q + 1;
		if (q > p && q[-1] == '\r')
			q--;
		*q = '\0';
		if (p == q)
			break;

#define W(a, b, p, q, sp) 				\
    if (!strncasecmp(p, a, strlen(a))) {		\
	for (p += strlen(a); p < q && isspace(*p); p++) \
		continue;				\
	sp->http.b = p;					\
	VSLR(SLT_##b, sp->fd, p, q);			\
	continue;					\
    } 

#define HTTPH(a, b, c, d, e, f, g)			\
	if (c & rr) {					\
		W(a ":", b, p, q, sp);			\
	}
#include "http_headers.h"
#undef HTTPH
#undef W
		if (sp->http.nuhdr < VCA_UNKNOWNHDR) {
			sp->http.uhdr[sp->http.nuhdr++] = p;
			VSLR(SLT_HD_Unknown, sp->fd, p, q);
		} else {
			VSLR(SLT_HD_Lost, sp->fd, p, q);
		}
	}
}

/*--------------------------------------------------------------------*/

static void
http_read_f(int fd, short event, void *arg)
{
	struct sess *sp = arg;
	const char *p;
	int i;

	assert(VCA_RXBUFSIZE - sp->rcv_len > 0);
	i = read(fd, sp->rcv + sp->rcv_len, VCA_RXBUFSIZE - sp->rcv_len);
	if (i <= 0) {
		VSL(SLT_SessionClose, sp->fd, "remote %d", sp->rcv_len);
		event_del(sp->rd_e);
		close(sp->fd);
		free(sp->mem);
		return;
	}

	sp->rcv_len += i;
	sp->rcv[sp->rcv_len] = '\0';

	p = sp->rcv;
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
	event_del(sp->rd_e);
	sp->sesscb(sp);
}

/*--------------------------------------------------------------------*/

void
HttpdGetHead(struct sess *sp, struct event_base *eb, sesscb_f *func)
{

	sp->sesscb = func;
	assert(sp->rd_e != NULL);
	event_set(sp->rd_e, sp->fd, EV_READ | EV_PERSIST, http_read_f, sp);
        event_base_set(eb, sp->rd_e);
        event_add(sp->rd_e, NULL);      /* XXX: timeout */
}
