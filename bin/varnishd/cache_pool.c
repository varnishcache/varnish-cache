/*
 * $Id$
 *
 * XXX: automatic thread-pool size adaptation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sbuf.h>

#include "libvarnish.h"
#include "heritage.h"
#include "shmlog.h"
#include "vcl.h"
#include "cache.h"

static TAILQ_HEAD(, sess) shd = TAILQ_HEAD_INITIALIZER(shd);

static pthread_cond_t	shdcnd;
static unsigned		xids;

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

static void *
CacheWorker(void *priv)
{
	struct sess *sp;
	struct worker w;
	int done;
	char *b;

	memset(&w, 0, sizeof w);
	w.eb = event_init();
	assert(w.eb != NULL);
	w.sb = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
	assert(w.sb != NULL);
	
	(void)priv;
	AZ(pthread_mutex_lock(&sessmtx));
	while (1) {
		while (1) {
			sp = TAILQ_FIRST(&shd);
			if (sp != NULL)
				break;
			AZ(pthread_cond_wait(&shdcnd, &sessmtx));
		}
		TAILQ_REMOVE(&shd, sp, list);
		time(&sp->t0);
		sp->vcl = GetVCL();
		AZ(pthread_mutex_unlock(&sessmtx));

		done = http_Dissect(sp->http, sp->fd, 1);
		if (done != 0) {
			RES_Error(&w, sp, done, NULL);
			goto out;
		}

		sp->backend = sp->vcl->backend[0];

		VCL_recv_method(sp);

		for (done = 0; !done; ) {
			switch(sp->handling) {
			case VCL_RET_LOOKUP:
				done = LookupSession(&w, sp);
				break;
			case VCL_RET_FETCH:
				done = FetchSession(&w, sp);
				break;
			case VCL_RET_DELIVER:
				done = DeliverSession(&w, sp);
				break;
			case VCL_RET_PIPE:
				PipeSession(&w, sp);
				done = 1;
				break;
			case VCL_RET_PASS:
				PassSession(&w, sp);
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
		sp->vcl = NULL;
		vca_return_session(sp);
	}
}

void
DealWithSession(void *arg)
{
	struct sess *sp = arg;

	time(&sp->t_req);

	/*
	 * No locking necessary, we're serialized in the acceptor thread
	 */
	sp->xid = xids++;
	VSL(SLT_XID, sp->fd, "%u", sp->xid);

	VSL_stats->client_req++;
	AZ(pthread_mutex_lock(&sessmtx));
	TAILQ_INSERT_TAIL(&shd, sp, list);
	AZ(pthread_mutex_unlock(&sessmtx));
	AZ(pthread_cond_signal(&shdcnd));
}

void
CacheInitPool(void)
{
	pthread_t tp;
	int i;

	AZ(pthread_cond_init(&shdcnd, NULL));

	VSL(SLT_Debug, 0, "Starting %u worker threads", heritage.wthread_min);
	for (i = 0; i < heritage.wthread_min; i++) {
		AZ(pthread_create(&tp, NULL, CacheWorker, NULL));
		AZ(pthread_detach(tp));
	}
	srandomdev();
	xids = random();
}
