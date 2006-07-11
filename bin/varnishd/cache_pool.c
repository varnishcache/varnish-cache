/*
 * $Id$
 *
 * XXX: automatic thread-pool size adaptation.
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

static pthread_mutex_t wrk_mtx;
static unsigned		xids;

/* Number of work requests queued in excess of worker threads available */
static unsigned		wrk_overflow;

static TAILQ_HEAD(, worker) wrk_head = TAILQ_HEAD_INITIALIZER(wrk_head);
static TAILQ_HEAD(, workreq) wrk_reqhead = TAILQ_HEAD_INITIALIZER(wrk_reqhead);

/*--------------------------------------------------------------------*/

static void *
wrk_thread(void *priv)
{
	struct worker *w, ww;
	struct workreq *wrq;
	struct timespec ts;

	w = &ww;
	memset(w, 0, sizeof w);

	AZ(pthread_cond_init(&w->cv, NULL));

	w->eb = event_init();
	assert(w->eb != NULL);

	w->sb = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
	assert(w->sb != NULL);
	
	AZ(pthread_mutex_lock(&wrk_mtx));
	w->nbr = VSL_stats->n_wrk;
	if (priv == NULL) {
		VSL_stats->n_wrk_create++;
		VSL(SLT_WorkThread, 0, "%u born dynamic", w->nbr);
	} else {
		VSL(SLT_WorkThread, 0, "%u born permanent", w->nbr);
	}
	TAILQ_INSERT_HEAD(&wrk_head, w, list);
	while (1) {
		wrq = TAILQ_FIRST(&wrk_reqhead);
		if (wrq != NULL) {
			VSL_stats->n_wrk_busy++;
			TAILQ_REMOVE(&wrk_head, w, list);
			TAILQ_REMOVE(&wrk_reqhead, wrq, list);
			AZ(pthread_mutex_unlock(&wrk_mtx));
			assert(wrq->sess != NULL);
			CNT_Session(w, wrq->sess);
			AZ(pthread_mutex_lock(&wrk_mtx));
			VSL_stats->n_wrk_busy--;
			TAILQ_INSERT_HEAD(&wrk_head, w, list);
		}
		if (wrk_overflow > 0) {
			wrk_overflow--;
			continue;
		}

		/* If we are a reserved thread we don't die */
		if (priv != NULL) {
			AZ(pthread_cond_wait(&w->cv, &wrk_mtx));
			continue;
		}

		/* If we are a dynamic thread, time out and die */
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += heritage.wthread_timeout;
		if (pthread_cond_timedwait(&w->cv, &wrk_mtx, &ts)) {
			VSL_stats->n_wrk--;
			TAILQ_REMOVE(&wrk_head, w, list);
			AZ(pthread_mutex_unlock(&wrk_mtx));
			VSL(SLT_WorkThread, 0, "%u suicide", w->nbr);
			sbuf_delete(w->sb);
			event_base_free(w->eb);
			AZ(pthread_cond_destroy(&w->cv));
			return (NULL);
		}
	}
}

/*--------------------------------------------------------------------*/

void
WRK_QueueSession(struct sess *sp)
{
	struct worker *w;
	pthread_t tp;

	time(&sp->t_req);

	/*
	 * No locking necessary, we're serialized in the acceptor thread
	 * XXX: still ?
	 */
	sp->xid = xids++;
	VSL(SLT_XID, sp->fd, "%u", sp->xid);

	sp->workreq.sess = sp;
	VSL_stats->client_req++;

	AZ(pthread_mutex_lock(&wrk_mtx));
	TAILQ_INSERT_TAIL(&wrk_reqhead, &sp->workreq, list);

	/* If there are idle threads, we tickle the first one into action */
	w = TAILQ_FIRST(&wrk_head);
	if (w != NULL) {
		AZ(pthread_cond_signal(&w->cv));
		AZ(pthread_mutex_unlock(&wrk_mtx));
		return;
	}
	
	/* Register overflow if max threads reached */
	if (VSL_stats->n_wrk >= heritage.wthread_max) {
		wrk_overflow++;
		VSL_stats->n_wrk_short++;
		AZ(pthread_mutex_unlock(&wrk_mtx));
		return;
	}

	/* Try to create a thread */
	VSL_stats->n_wrk++;
	AZ(pthread_mutex_unlock(&wrk_mtx));
	if (!pthread_create(&tp, NULL, wrk_thread, NULL)) {
		AZ(pthread_detach(tp));
		return;
	}

	VSL(SLT_Debug, 0, "Create worker thread failed %d %s",
	    errno, strerror(errno));

	/* Register overflow */
	AZ(pthread_mutex_lock(&wrk_mtx));
	VSL_stats->n_wrk--;
	wrk_overflow++;
	VSL_stats->n_wrk_failed++;
	VSL_stats->n_wrk_short++;
	AZ(pthread_mutex_unlock(&wrk_mtx));
}
	

/*--------------------------------------------------------------------*/

void
WRK_Init(void)
{
	pthread_t tp;
	int i;

	AZ(pthread_mutex_init(&wrk_mtx, NULL));

	VSL(SLT_Debug, 0, "Starting %u worker threads", heritage.wthread_min);
	for (i = 0; i < heritage.wthread_min; i++) {
		VSL_stats->n_wrk++;
		AZ(pthread_create(&tp, NULL, wrk_thread, &i));
		AZ(pthread_detach(tp));
	}
	srandomdev();
	xids = random();
}
