/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <queue.h>
#include <sys/time.h>
#include <sbuf.h>
#include <event.h>

#include "libvarnish.h"
#include "shmlog.h"
#include "vcl.h"
#include "cache.h"

static TAILQ_HEAD(, sess) shd = TAILQ_HEAD_INITIALIZER(shd);

static pthread_cond_t	shdcnd;

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
		VCL_hit_method(sp);
	}
	return (0);
}

static int
DeliverSession(struct worker *w, struct sess *sp)
{


	vca_write_obj(sp, sp->obj->header, 0);
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

		http_Dissect(sp->http, sp->fd, 1);

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
		}

		AZ(pthread_mutex_lock(&sessmtx));
		RelVCL(sp->vcl);
		sp->vcl = NULL;
		vca_return_session(sp);
	}
}

void
DealWithSession(void *arg, int good)
{
	struct sess *sp = arg;

	if (!good) {
		vca_close_session(sp, "no request");
		vca_return_session(sp);
		return;
	}
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

	for (i = 0; i < 5; i++)
		AZ(pthread_create(&tp, NULL, CacheWorker, NULL));
	AZ(pthread_detach(tp));
}
