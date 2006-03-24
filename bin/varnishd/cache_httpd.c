/*
 * $Id$
 *
 * Stuff relating to HTTP server side
 */

#include <stdio.h>
#include <ctype.h>

#include "libvarnish.h"
#include "shmlog.h"
#include "cache.h"

void
HttpdAnalyze(struct sess *sp)
{
	const char *p, *q;

	sp->handling = HND_Unclass;

	/* First, isolate and possibly identify request type */
	p = sp->req_b = sp->rcv;
	if (p[0] == 'G' && p[1] == 'E' && p[2] == 'T' && p[3] == ' ') {
		p = sp->req_e = p + 4;
		sp->handling = HND_Handle;
	} else if (p[0] == 'H' && p[1] == 'E' && p[2] == 'A' && p[3] == 'D'
	    && p[4] == ' ') {
		p = sp->req_e = p + 5;
		sp->handling = HND_Handle;
	} else {
		/*
		 * We don't bother to identify the rest, we won't handle
		 * them in any case
		 */
		for (q = p; isalpha(*q); q++)
			;
		p = sp->req_e = q;
		sp->handling = HND_Pass;
	}
	VSLR(SLT_Request, sp->fd, sp->req_b, sp->req_e);

	/* Next find the URI */
	while (isspace(*p))
		p++;
	sp->url_b = p;
	while (!isspace(*p))
		p++;
	sp->url_e = p;
	VSLR(SLT_URL, sp->fd, sp->url_b, sp->url_e);

	/* Finally, look for protocol, if any */
	while (isspace(*p) && *p != '\n')
		p++;
	sp->proto_b = sp->proto_e = p;
	if (*p != '\n') {
		while (!isspace(*p))
			p++;
		sp->proto_e = p;
	}
	VSLR(SLT_Protocol, sp->fd, sp->proto_b, sp->proto_e);

	/*
	 * And mark the start of headers.  The end of headers 
	 * is already set in acceptor where we detected the complete request.
	 */
	while (*p != '\n')
		p++;
	p++;
	while (isspace(*p) && *p != '\n')
		p++;
	sp->hdr_b = p;
	VSLR(SLT_Headers, sp->fd, sp->hdr_b, sp->hdr_e);
}
