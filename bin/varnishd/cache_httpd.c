/*
 * $Id$
 *
 * Stuff relating to HTTP server side
 */

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>

#include "libvarnish.h"
#include "shmlog.h"
#include "vcl_lang.h"
#include "cache.h"

void
HttpdAnalyze(struct sess *sp)
{
	char *p, *q, *r;

	sp->handling = HND_Unclass;

	memset(&sp->http, 0, sizeof sp->http);

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

#define HTTPH(a, b, c, d, e, f, g)	W(a ":", b, p, q, sp)
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
