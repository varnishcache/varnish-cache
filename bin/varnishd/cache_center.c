/*
 * $Id$
 *
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sbuf.h>

#include "libvarnish.h"
#include "heritage.h"
#include "shmlog.h"
#include "vcl.h"
#include "cache.h"

/*--------------------------------------------------------------------*/

static int
LookupSession(struct worker *w, struct sess *sp)
{
	struct object *o;

	o = HSH_Lookup(w, sp->http);
	sp->obj = o;
	if (o->busy) {
		VSL_stats->cache_miss++;
		VCL_miss_method(sp);
	} else {
		VSL_stats->cache_hit++;
		VSL(SLT_Hit, sp->fd, "%u", o->xid);
		VCL_hit_method(sp);
	}
	return (0);
}

static int
DeliverSession(struct worker *w, struct sess *sp)
{


	vca_write_obj(w, sp);
	HSH_Deref(sp->obj);
	sp->obj = NULL;
	return (1);
}

void
CNT_Session(struct worker *w, struct sess *sp)
{
	int done;
	char *b;

	time(&sp->t0);
	AZ(pthread_mutex_lock(&sessmtx));
	sp->vcl = GetVCL();
	AZ(pthread_mutex_unlock(&sessmtx));

	done = http_DissectRequest(sp->http, sp->fd);
	if (done != 0) {
		RES_Error(w, sp, done, NULL);
		goto out;
	}

	sp->backend = sp->vcl->backend[0];

	VCL_recv_method(sp);

	for (done = 0; !done; ) {
		switch(sp->handling) {
		case VCL_RET_LOOKUP:
			done = LookupSession(w, sp);
			break;
		case VCL_RET_FETCH:
			done = FetchSession(w, sp);
			break;
		case VCL_RET_DELIVER:
			done = DeliverSession(w, sp);
			break;
		case VCL_RET_PIPE:
			PipeSession(w, sp);
			done = 1;
			break;
		case VCL_RET_PASS:
			PassSession(w, sp);
			done = 1;
			break;
		default:
			INCOMPL();
		}
	}
	if (http_GetHdr(sp->http, "Connection", &b) &&
	    !strcmp(b, "close")) {
		vca_close_session(sp, "Connection header");
	} else if (http_GetProto(sp->http, &b) &&
	    strcmp(b, "HTTP/1.1")) {
		vca_close_session(sp, "not HTTP/1.1");
	}

out:
	AZ(pthread_mutex_lock(&sessmtx));
	RelVCL(sp->vcl);
	AZ(pthread_mutex_unlock(&sessmtx));
	sp->vcl = NULL;
	vca_return_session(sp);
}
