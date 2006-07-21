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

#include "libvarnish.h"
#include "heritage.h"
#include "shmlog.h"
#include "vcl.h"
#include "cache.h"

static pthread_mutex_t wrk_mtx;

/* Number of work requests queued in excess of worker threads available */
static unsigned		wrk_overflow;

static TAILQ_HEAD(, worker) wrk_head = TAILQ_HEAD_INITIALIZER(wrk_head);
static TAILQ_HEAD(, workreq) wrk_reqhead = TAILQ_HEAD_INITIALIZER(wrk_reqhead);

/*--------------------------------------------------------------------
 * Write data to fd
 * We try to use writev() if possible in order to minimize number of
 * syscalls made and packets sent.  It also just might allow the worker
 * thread to complete the request without holding stuff locked.
 */

void
WRK_Reset(struct worker *w, int *fd)
{

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	w->werr = 0;
	w->liov = 0;
	w->niov = 0;
	w->wfd = fd;
}

int
WRK_Flush(struct worker *w)
{
	int i;

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	if (*w->wfd < 0 || w->niov == 0 || w->werr)
		return (w->werr);
	i = writev(*w->wfd, w->iov, w->niov);
	if (i != w->liov)
		w->werr++;
	else {
		w->liov = 0;
		w->niov = 0;
	}
	return (w->werr);
}

void
WRK_WriteH(struct worker *w, struct http_hdr *hh, const char *suf)
{
	
	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	assert(w != NULL);
	assert(hh != NULL);
	assert(hh->b != NULL);
	assert(hh->e != NULL);
	WRK_Write(w, hh->b, hh->e - hh->b);
	if (suf != NULL)
		WRK_Write(w, suf, -1);
}

void
WRK_Write(struct worker *w, const void *ptr, size_t len)
{

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	if (len == 0 || *w->wfd < 0)
		return;
	if (len == -1)
		len = strlen(ptr);
	if (w->niov == MAX_IOVS)
		WRK_Flush(w);
	w->iov[w->niov].iov_base = (void*)(uintptr_t)ptr;
	w->iov[w->niov++].iov_len = len;
	w->liov += len;
}

/*--------------------------------------------------------------------*/

static void
wrk_do_one(struct worker *w)
{
	struct workreq *wrq;

	wrq = TAILQ_FIRST(&wrk_reqhead);
	assert(wrq != NULL);
	VSL_stats->n_wrk_busy++;
	TAILQ_REMOVE(&wrk_reqhead, wrq, list);
	VSL_stats->n_wrk_queue--;
	AZ(pthread_mutex_unlock(&wrk_mtx));
	CHECK_OBJ_NOTNULL(wrq->sess, SESS_MAGIC);
	wrq->sess->wrk = w;
	if (wrq->sess->srcaddr == NULL)
		SES_RefSrcAddr(wrq->sess);
	if (w->nobj != NULL)
		CHECK_OBJ(w->nobj, OBJECT_MAGIC);
	if (w->nobjhead != NULL)
		CHECK_OBJ(w->nobjhead, OBJHEAD_MAGIC);
	CNT_Session(wrq->sess);
	if (w->nobj != NULL)
		CHECK_OBJ(w->nobj, OBJECT_MAGIC);
	if (w->nobjhead != NULL)
		CHECK_OBJ(w->nobjhead, OBJHEAD_MAGIC);
	AZ(pthread_mutex_lock(&wrk_mtx));
	VSL_stats->n_wrk_busy--;
}

static void *
wrk_thread(void *priv)
{
	struct worker *w, ww;
	struct timespec ts;

	w = &ww;
	memset(w, 0, sizeof *w);
	w->magic = WORKER_MAGIC;

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
	}
	while (1) {
		CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);

		/* Process overflow requests, if any */
		if (wrk_overflow > 0) {
			wrk_overflow--;
			wrk_do_one(w);
			continue;
		}
		
		TAILQ_INSERT_HEAD(&wrk_head, w, list);

		/* If we are a reserved thread we don't die */
		if (priv != NULL) {
			AZ(pthread_cond_wait(&w->cv, &wrk_mtx));
		} else {
			/* If we are a dynamic thread, time out and die */
			AZ(clock_gettime(CLOCK_REALTIME, &ts));
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

		/* we are already removed from wrk_head */
		wrk_do_one(w);
	}
}

/*--------------------------------------------------------------------*/

void
WRK_QueueSession(struct sess *sp)
{
	struct worker *w;
	pthread_t tp;

	sp->t_req = time(NULL);

	sp->workreq.sess = sp;

	AZ(pthread_mutex_lock(&wrk_mtx));
	TAILQ_INSERT_TAIL(&wrk_reqhead, &sp->workreq, list);
	VSL_stats->n_wrk_queue++;

	/* If there are idle threads, we tickle the first one into action */
	w = TAILQ_FIRST(&wrk_head);
	if (w != NULL) {
		AZ(pthread_cond_signal(&w->cv));
		TAILQ_REMOVE(&wrk_head, w, list);
		AZ(pthread_mutex_unlock(&wrk_mtx));
		return;
	}
	
	wrk_overflow++;

	/* Can we create more threads ? */
	if (VSL_stats->n_wrk >= heritage.wthread_max) {
		VSL_stats->n_wrk_max++;
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
	VSL_stats->n_wrk_failed++;
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
}
