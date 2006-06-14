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
#include <md5.h>

#include "libvarnish.h"
#include "shmlog.h"
#include "vcl_lang.h"
#include "cache.h"

static TAILQ_HEAD(, sess) shd = TAILQ_HEAD_INITIALIZER(shd);

static pthread_cond_t	shdcnd;

static int
LookupSession(struct worker *w, struct sess *sp)
{
	struct object *o;
	unsigned char key[16];
	MD5_CTX ctx;
	char *b;

	if (w->nobj == NULL) {
		w->nobj = calloc(sizeof *w->nobj, 1);	
		assert(w->nobj != NULL);
		w->nobj->busy = 1;
		TAILQ_INIT(&w->nobj->store);
	}

	assert(http_GetURL(sp->http, &b));
	MD5Init(&ctx);
	MD5Update(&ctx, b, strlen(b));
	MD5Final(key, &ctx);
	o = hash->lookup(key, w->nobj);
	if (o == w->nobj) {
		VSL(SLT_Debug, 0, "Lookup new %p %s", o, b);
		w->nobj = NULL;
	} else {
		VSL(SLT_Debug, 0, "Lookup found %p %s", o, b);
	}
	/*
	 * XXX: if obj is busy, park session on it
	 */

	sp->obj = o;
	sp->handling = HND_Unclass;
	sp->vcl->lookup_func(sp);
	if (sp->handling == HND_Unclass) {
		if (o->valid && o->cacheable)
			sp->handling = HND_Deliver;
		else
			sp->handling = HND_Pass;
	}
	return (0);
}

static int
DeliverSession(struct worker *w, struct sess *sp)
{

	sbuf_clear(w->sb);
	sbuf_printf(w->sb,
	    "HTTP/1.1 200 OK\r\n"
	    "Server: Varnish\r\n"
	    "Content-Length: %u\r\n"
	    "\r\n", sp->obj->len);

	vca_write_obj(sp, w->sb);
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
		sp->vcl = GetVCL();
		AZ(pthread_mutex_unlock(&sessmtx));

		http_Dissect(sp->http, sp->fd, 1);

		sp->backend = sp->vcl->default_backend;

		/*
		 * Call the VCL recv function.
		 * Default action is to lookup
		 */
		sp->handling = HND_Lookup;
		
		sp->vcl->recv_func(sp);

		for (done = 0; !done; ) {
			switch(sp->handling) {
			case HND_Lookup:
				VSL(SLT_Handling, sp->fd, "Lookup");
				done = LookupSession(&w, sp);
				break;
			case HND_Fetch:
				done = FetchSession(&w, sp);
				break;
			case HND_Deliver:
				VSL(SLT_Handling, sp->fd, "Deliver");
				done = DeliverSession(&w, sp);
				break;
			case HND_Pipe:
				VSL(SLT_Handling, sp->fd, "Pipe");
				PipeSession(&w, sp);
				done = 1;
				break;
			case HND_Pass:
				VSL(SLT_Handling, sp->fd, "Pass");
				PassSession(&w, sp);
				done = 1;
				break;
			default:
				VSL(SLT_Handling, sp->fd, "Unclass");
				assert(sp->handling == HND_Unclass);
				assert(sp->handling != HND_Unclass);
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
	AZ(pthread_mutex_lock(&sessmtx));
	TAILQ_INSERT_TAIL(&shd, sp, list);
	AZ(pthread_mutex_unlock(&sessmtx));
	AZ(pthread_cond_signal(&shdcnd));
}

void
CacheInitPool(void)
{
	pthread_t tp;

	AZ(pthread_cond_init(&shdcnd, NULL));

	AZ(pthread_create(&tp, NULL, CacheWorker, NULL));
	AZ(pthread_detach(tp));
}
