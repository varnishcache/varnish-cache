/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id$
 *
 * We maintain a number of worker thread pools, to spread lock contention.
 *
 * Pools can be added on the fly, as a means to mitigate lock contention,
 * but can only be removed again by a restart. (XXX: we could fix that)
 *
 * Two threads herd the pools, one eliminates idle threads and aggregates
 * statistics for all the pools, the other thread creates new threads
 * on demand, subject to various numerical constraints.
 *
 * The algorithm for when to create threads needs to be reactive enough
 * to handle startup spikes, but sufficiently attenuated to not cause
 * thread pileups.  This remains subject for improvement.
 */

#include "config.h"

#include <sys/types.h>
#include <sys/uio.h>

#ifdef SENDFILE_WORKS
#if defined(__FreeBSD__)
#include <sys/socket.h>
#elif defined(__linux__)
#include <sys/sendfile.h>
#elif defined(__sun)
#include <sys/sendfile.h>
#else
#error Unknown sendfile() implementation
#endif
#endif /* SENDFILE_WORKS */

#include <errno.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shmlog.h"
#include "vcl.h"
#include "cli_priv.h"
#include "cache.h"
#include "stevedore.h"
#include "hash_slinger.h"
#include "vsha256.h"

VTAILQ_HEAD(workerhead, worker);

/* Number of work requests queued in excess of worker threads available */

struct wq {
	unsigned		magic;
#define WQ_MAGIC		0x606658fa
	struct lock		mtx;
	struct workerhead	idle;
	VTAILQ_HEAD(, workreq)	overflow;
	unsigned		nthr;
	unsigned		nqueue;
	unsigned		lqueue;
	uintmax_t		ndrop;
	uintmax_t		noverflow;
};

static struct wq		**wq;
static unsigned			nwq;
static unsigned			ovfl_max;
static unsigned			nthr_max;

static pthread_cond_t		herder_cond;
static struct lock		herder_mtx;

/*--------------------------------------------------------------------
 * Write data to fd
 * We try to use writev() if possible in order to minimize number of
 * syscalls made and packets sent.  It also just might allow the worker
 * thread to complete the request without holding stuff locked.
 */

void
WRW_Reserve(struct worker *w, int *fd)
{

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	AZ(w->wfd);
	w->werr = 0;
	w->liov = 0;
	w->niov = 0;
	w->wfd = fd;
}

void
WRW_Release(struct worker *w)
{

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	w->werr = 0;
	w->liov = 0;
	w->niov = 0;
	w->wfd = NULL;
}

unsigned
WRW_Flush(struct worker *w)
{
	ssize_t i;

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	AN(w->wfd);
	if (*w->wfd >= 0 && w->niov > 0 && w->werr == 0) {
		i = writev(*w->wfd, w->iov, w->niov);
		if (i != w->liov) {
			w->werr++;
			WSL(w, SLT_Debug, *w->wfd,
			    "Write error, len = %d/%d, errno = %s",
			    i, w->liov, strerror(errno));
		}
	}
	w->liov = 0;
	w->niov = 0;
	return (w->werr);
}

unsigned
WRW_FlushRelease(struct worker *w)
{
	unsigned u;

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	AN(w->wfd);
	u = WRW_Flush(w);
	WRW_Release(w);
	return (u);
}

unsigned
WRW_WriteH(struct worker *w, const txt *hh, const char *suf)
{
	unsigned u;

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	AN(w->wfd);
	AN(w);
	AN(hh);
	AN(hh->b);
	AN(hh->e);
	u = WRW_Write(w, hh->b, hh->e - hh->b);
	if (suf != NULL)
		u += WRW_Write(w, suf, -1);
	return (u);
}

unsigned
WRW_Write(struct worker *w, const void *ptr, int len)
{

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	AN(w->wfd);
	if (len == 0 || *w->wfd < 0)
		return (0);
	if (len == -1)
		len = strlen(ptr);
	if (w->niov == MAX_IOVS)
		(void)WRW_Flush(w);
	w->iov[w->niov].iov_base = TRUST_ME(ptr);
	w->iov[w->niov].iov_len = len;
	w->liov += len;
	w->niov++;
	return (len);
}

#ifdef SENDFILE_WORKS
void
WRW_Sendfile(struct worker *w, int fd, off_t off, unsigned len)
{

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	AN(w->wfd);
	assert(fd >= 0);
	assert(len > 0);

#if defined(__FreeBSD__)
	do {
		struct sf_hdtr sfh;
		memset(&sfh, 0, sizeof sfh);
		if (w->niov > 0) {
			sfh.headers = w->iov;
			sfh.hdr_cnt = w->niov;
		}
		if (sendfile(fd, *w->wfd, off, len, &sfh, NULL, 0) != 0)
			w->werr++;
		w->liov = 0;
		w->niov = 0;
	} while (0);
#elif defined(__linux__)
	do {
		if (WRK_Flush(w) == 0 &&
		    sendfile(*w->wfd, fd, &off, len) != len)
			w->werr++;
	} while (0);
#elif defined(__sun) && defined(HAVE_SENDFILEV)
	do {
		sendfilevec_t svvec[HTTP_HDR_MAX * 2 + 1];
		size_t xferred = 0, expected = 0;
		int i;
		for (i = 0; i < w->niov; i++) {
			svvec[i].sfv_fd = SFV_FD_SELF;
			svvec[i].sfv_flag = 0;
			svvec[i].sfv_off = (off_t) w->iov[i].iov_base;
			svvec[i].sfv_len = w->iov[i].iov_len;
			expected += svvec[i].sfv_len;
		}
		svvec[i].sfv_fd = fd;
		svvec[i].sfv_flag = 0;
		svvec[i].sfv_off = off;
		svvec[i].sfv_len = len;
		expected += svvec[i].sfv_len;
		if (sendfilev(*w->wfd, svvec, i, &xferred) == -1 ||
		    xferred != expected)
			w->werr++;
		w->liov = 0;
		w->niov = 0;
	} while (0);
#elif defined(__sun) && defined(HAVE_SENDFILE)
	do {
		if (WRK_Flush(w) == 0 &&
		    sendfile(*w->wfd, fd, &off, len) != len)
			w->werr++;
	} while (0);
#else
#error Unknown sendfile() implementation
#endif
}
#endif /* SENDFILE_WORKS */

/*--------------------------------------------------------------------*/

static void *
wrk_thread(void *priv)
{
	struct worker *w, ww;
	struct wq *qp;
	unsigned char wlog[params->shm_workspace];
	struct SHA256Context sha256;

	THR_SetName("cache-worker");
	w = &ww;
	CAST_OBJ_NOTNULL(qp, priv, WQ_MAGIC);
	memset(w, 0, sizeof *w);
	w->magic = WORKER_MAGIC;
	w->lastused = NAN;
	w->wlb = w->wlp = wlog;
	w->wle = wlog + sizeof wlog;
	w->sha256ctx = &sha256;
	AZ(pthread_cond_init(&w->cond, NULL));

	VSL(SLT_WorkThread, 0, "%p start", w);

	Lck_Lock(&qp->mtx);
	qp->nthr++;
	while (1) {
		CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);

		/* Process overflow requests, if any */
		w->wrq = VTAILQ_FIRST(&qp->overflow);
		if (w->wrq != NULL) {
			VTAILQ_REMOVE(&qp->overflow, w->wrq, list);
			qp->nqueue--;
		} else {
			if (isnan(w->lastused))
				w->lastused = TIM_real();
			VTAILQ_INSERT_HEAD(&qp->idle, w, list);
			Lck_CondWait(&w->cond, &qp->mtx);
		}
		if (w->wrq == NULL)
			break;
		Lck_Unlock(&qp->mtx);
		AN(w->wrq);
		AN(w->wrq->func);
		w->lastused = NAN;
		w->wrq->func(w, w->wrq->priv);
		AZ(w->wfd);
		assert(w->wlp == w->wlb);
		w->wrq = NULL;
		Lck_Lock(&qp->mtx);
	}
	qp->nthr--;
	Lck_Unlock(&qp->mtx);

	VSL(SLT_WorkThread, 0, "%p end", w);
	if (w->vcl != NULL)
		VCL_Rel(&w->vcl);
	AZ(pthread_cond_destroy(&w->cond));
	if (w->srcaddr != NULL)
		free(w->srcaddr);
	if (w->nobjhead != NULL) {
		Lck_Delete(&w->nobjhead->mtx);
		FREE_OBJ(w->nobjhead);
	}
	if (w->nobj!= NULL)
		STV_free(w->nobj->objstore);
	return (NULL);
}

/*--------------------------------------------------------------------
 * Queue a workrequest if possible.
 *
 * Return zero if the request was queued, negative if it wasn't.
 */

int
WRK_Queue(struct workreq *wrq)
{
	struct worker *w;
	struct wq *qp;
	static unsigned nq = 0;
	unsigned onq;

	/*
	 * Select which pool we issue to
	 * XXX: better alg ?
	 * XXX: per CPU ?
	 */
	onq = nq + 1;
	if (onq >= nwq)
		onq = 0;
	qp = wq[onq];
	nq = onq;

	Lck_Lock(&qp->mtx);

	/* If there are idle threads, we tickle the first one into action */
	w = VTAILQ_FIRST(&qp->idle);
	if (w != NULL) {
		VTAILQ_REMOVE(&qp->idle, w, list);
		Lck_Unlock(&qp->mtx);
		w->wrq = wrq;
		AZ(pthread_cond_signal(&w->cond));
		return (0);
	}

	/* If we have too much in the overflow already, refuse. */
	if (qp->nqueue > ovfl_max) {
		qp->ndrop++;
		Lck_Unlock(&qp->mtx);
		return (-1);
	}

	VTAILQ_INSERT_TAIL(&qp->overflow, wrq, list);
	qp->noverflow++;
	qp->nqueue++;
	Lck_Unlock(&qp->mtx);
	AZ(pthread_cond_signal(&herder_cond));
	return (0);
}

/*--------------------------------------------------------------------*/

static void
wrk_do_cnt_sess(struct worker *w, void *priv)
{
	struct sess *sess;

	CAST_OBJ_NOTNULL(sess, priv, SESS_MAGIC);
	THR_SetSession(sess);
	sess->wrk = w;
	CHECK_OBJ_ORNULL(w->nobj, OBJECT_MAGIC);
	CHECK_OBJ_ORNULL(w->nobjhead, OBJHEAD_MAGIC);
	CNT_Session(sess);
	CHECK_OBJ_ORNULL(w->nobj, OBJECT_MAGIC);
	CHECK_OBJ_ORNULL(w->nobjhead, OBJHEAD_MAGIC);
	THR_SetSession(NULL);
}

/*--------------------------------------------------------------------*/

void
WRK_QueueSession(struct sess *sp)
{
	sp->workreq.func = wrk_do_cnt_sess;
	sp->workreq.priv = sp;
	if (WRK_Queue(&sp->workreq) == 0)
		return;

	/*
	 * Couldn't queue it -- kill it.
	 *
	 * XXX: a notice might be polite, but would potentially
	 * XXX: sleep whichever thread got us here
	 */
	sp->t_end = TIM_real();
	vca_close_session(sp, "dropped");
	if (sp->vcl != NULL) {
		/*
		 * A session parked on a busy object can come here
		 * after it wakes up.  Loose the VCL reference.
		 */
		VCL_Rel(&sp->vcl);
	}
	SES_Delete(sp);
}

/*--------------------------------------------------------------------
 * Add (more) thread pools
 */

static void
wrk_addpools(const unsigned pools)
{
	struct wq **pwq, **owq;
	unsigned u;

	pwq = calloc(sizeof *pwq, pools);
	if (pwq == NULL)
		return;
	if (wq != NULL)
		memcpy(pwq, wq, sizeof *pwq * nwq);
	owq = wq;
	wq = pwq;
	for (u = nwq; u < pools; u++) {
		wq[u] = calloc(sizeof *wq[u], 1);
		XXXAN(wq[u]);
		wq[u]->magic = WQ_MAGIC;
		Lck_New(&wq[u]->mtx);
		VTAILQ_INIT(&wq[u]->overflow);
		VTAILQ_INIT(&wq[u]->idle);
	}
	(void)owq;	/* XXX: avoid race, leak it. */
	nwq = pools;
}

/*--------------------------------------------------------------------
 * If a thread is idle or excess, pick it out of the pool.
 */

static void
wrk_decimate_flock(struct wq *qp, double t_idle, struct varnish_stats *vs)
{
	struct worker *w = NULL;

	Lck_Lock(&qp->mtx);
	vs->n_wrk += qp->nthr;
	vs->n_wrk_queue += qp->nqueue;
	vs->n_wrk_drop += qp->ndrop;
	vs->n_wrk_overflow += qp->noverflow;

	if (qp->nthr > params->wthread_min) {
		w = VTAILQ_LAST(&qp->idle, workerhead);
		if (w != NULL && (w->lastused < t_idle || qp->nthr > nthr_max))
			VTAILQ_REMOVE(&qp->idle, w, list);
		else
			w = NULL;
	}
	Lck_Unlock(&qp->mtx);

	/* And give it a kiss on the cheek... */
	if (w != NULL) {
		AZ(w->wrq);
		AZ(pthread_cond_signal(&w->cond));
		(void)usleep(params->wthread_purge_delay * 1000);
	}
}

/*--------------------------------------------------------------------
 * Periodic pool herding thread
 *
 * Do things which we can do at our leisure:
 *  Add pools
 *  Scale constants
 *  Get rid of excess threads
 *  Aggregate stats across pools
 */

static void *
wrk_herdtimer_thread(void *priv)
{
	volatile unsigned u;
	double t_idle;
	struct varnish_stats vsm, *vs;

	THR_SetName("wrk_herdtimer");

	memset(&vsm, 0, sizeof vsm);
	vs = &vsm;

	(void)priv;
	while (1) {
		/* Add Pools */
		u = params->wthread_pools;
		if (u > nwq)
			wrk_addpools(u);

		/* Scale parameters */

		u = params->wthread_max / nwq;
		if (u < params->wthread_min)
			u = params->wthread_min;
		nthr_max = u;

		ovfl_max = (nthr_max * params->overflow_max) / 100;

		vs->n_wrk = 0;
		vs->n_wrk_queue = 0;
		vs->n_wrk_drop = 0;
		vs->n_wrk_overflow = 0;

		t_idle = TIM_real() - params->wthread_timeout;
		for (u = 0; u < nwq; u++)
			wrk_decimate_flock(wq[u], t_idle, vs);

		VSL_stats->n_wrk= vs->n_wrk;
		VSL_stats->n_wrk_queue = vs->n_wrk_queue;
		VSL_stats->n_wrk_drop = vs->n_wrk_drop;
		VSL_stats->n_wrk_overflow = vs->n_wrk_overflow;

		(void)usleep(params->wthread_purge_delay * 1000);
	}
}

/*--------------------------------------------------------------------
 * Create another thread, if necessary & possible
 */

static void
wrk_breed_flock(struct wq *qp)
{
	pthread_t tp;

	/*
	 * If we need more threads, and have space, create
	 * one more thread.
	 */
	if (qp->nthr < params->wthread_min ||	/* Not enough threads yet */
	    (qp->nqueue > params->wthread_add_threshold && /* more needed */
	    qp->nqueue > qp->lqueue)) {	/* not getting better since last */
		if (qp->nthr >= nthr_max) {
			VSL_stats->n_wrk_max++;
		} else if (pthread_create(&tp, NULL, wrk_thread, qp)) {
			VSL(SLT_Debug, 0, "Create worker thread failed %d %s",
			    errno, strerror(errno));
			VSL_stats->n_wrk_failed++;
			(void)usleep(params->wthread_fail_delay * 1000);
		} else {
			AZ(pthread_detach(tp));
			VSL_stats->n_wrk_create++;
			(void)usleep(params->wthread_add_delay * 1000);
		}
	}
	qp->lqueue = qp->nqueue;
}

/*--------------------------------------------------------------------
 * This thread wakes up whenever a pool overflows.
 *
 * The trick here is to not be too aggressive about creating threads.
 * We do this by only examining one pool at a time, and by sleeping
 * a short while whenever we create a thread and a little while longer
 * whenever we fail to, hopefully missing a lot of cond_signals in
 * the meantime.
 *
 * XXX: probably need a lot more work.
 *
 */

static void *
wrk_herder_thread(void *priv)
{
	unsigned u, w;

	THR_SetName("wrk_herder");
	(void)priv;
	while (1) {
		for (u = 0 ; u < nwq; u++) {
			/*
			 * Make sure all pools have their minimum complement
			 */
			for (w = 0 ; w < nwq; w++)
				while (wq[w]->nthr < params->wthread_min)
					wrk_breed_flock(wq[w]);
			/*
			 * We cannot avoid getting a mutex, so we have a
			 * bogo mutex just for POSIX_STUPIDITY
			 */
			Lck_Lock(&herder_mtx);
			Lck_CondWait(&herder_cond, &herder_mtx);
			Lck_Unlock(&herder_mtx);
			wrk_breed_flock(wq[u]);
		}
	}
}

/*--------------------------------------------------------------------*/

void
WRK_Init(void)
{
	pthread_t tp;

	AZ(pthread_cond_init(&herder_cond, NULL));
	Lck_New(&herder_mtx);

	wrk_addpools(params->wthread_pools);
	AZ(pthread_create(&tp, NULL, wrk_herdtimer_thread, NULL));
	AZ(pthread_detach(tp));
	AZ(pthread_create(&tp, NULL, wrk_herder_thread, NULL));
	AZ(pthread_detach(tp));
}

/*--------------------------------------------------------------------*/
