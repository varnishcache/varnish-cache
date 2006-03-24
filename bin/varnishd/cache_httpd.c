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
	const char *p, *q, *u;

	p = sp->rcv;

	if (p[0] == 'G' && p[1] == 'E' && p[2] == 'T' && p[3] == ' ') {
		p += 4;
		VSL(SLT_Request, sp->fd, "GET");
	} else if (p[0] == 'H' && p[1] == 'E' && p[2] == 'A' && p[3] == 'D'
	    && p[4] == ' ') {
		p += 5;
		VSL(SLT_Request, sp->fd, "HEAD");
	} else {
		for (q = p; isupper(*q); q++)
			;
		VSLR(SLT_Request, sp->fd, p, q);
		p = q;
	}
	while (isspace(*p))
		p++;
	u = p;
	while (!isspace(*p))
		p++;
	VSLR(SLT_URL, sp->fd, u, p);
}
