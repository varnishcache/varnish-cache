/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Linpro AS
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

#include <errno.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
	VTAILQ_HEAD(, workreq)	queue;
	unsigned		nthr;
	unsigned		lqueue;
	unsigned		last_lqueue;
	uintmax_t		ndrop;
	uintmax_t		nqueue;
};

static struct wq		**wq;
static unsigned			nwq;
static unsigned			queue_max;
static unsigned			nthr_max;

static pthread_cond_t		herder_cond;
static struct lock		herder_mtx;
static struct lock		wstat_mtx;

/*--------------------------------------------------------------------*/

static void
wrk_sumstat(struct worker *w)
{

	Lck_AssertHeld(&wstat_mtx);
#define L0(n)
#define L1(n) (VSC_main->n += w->stats.n)
#define VSC_DO_MAIN
#define VSC_F(n, t, l, f, d) L##l(n);
#include "vsc_fields.h"
#undef VSC_F
#undef VSC_DO_MAIN
#undef L0
#undef L1
	memset(&w->stats, 0, sizeof w->stats);
}

void
WRK_SumStat(struct worker *w)
{

	Lck_Lock(&wstat_mtx);
	wrk_sumstat(w);
	Lck_Unlock(&wstat_mtx);
}

/*--------------------------------------------------------------------*/

static void *
wrk_thread_real(struct wq *qp, unsigned shm_workspace, unsigned sess_workspace,
    unsigned nhttp, unsigned http_space, unsigned siov)
{
	struct worker *w, ww;
	uint32_t wlog[shm_workspace / 4];
	/* XXX: can we trust these to be properly aligned ? */
	unsigned char ws[sess_workspace];
	unsigned char http0[http_space];
	unsigned char http1[http_space];
	unsigned char http2[http_space];
	struct iovec iov[siov];
	struct SHA256Context sha256;
	int stats_clean;

	THR_SetName("cache-worker");
	w = &ww;
	memset(w, 0, sizeof *w);
	w->magic = WORKER_MAGIC;
	w->lastused = NAN;
	w->wlb = w->wlp = wlog;
	w->wle = wlog + (sizeof wlog) / 4;
	w->sha256ctx = &sha256;
	w->bereq = HTTP_create(http0, nhttp);
	w->beresp = HTTP_create(http1, nhttp);
	w->resp = HTTP_create(http2, nhttp);
	w->wrw.iov = iov;
	w->wrw.siov = siov;
	w->wrw.ciov = siov;
	AZ(pthread_cond_init(&w->cond, NULL));

	WS_Init(w->ws, "wrk", ws, sess_workspace);

	VSL(SLT_WorkThread, 0, "%p start", w);

	Lck_Lock(&qp->mtx);
	qp->nthr++;
	stats_clean = 1;
	while (1) {
		CHECK_OBJ_NOTNULL(w->bereq, HTTP_MAGIC);
		CHECK_OBJ_NOTNULL(w->beresp, HTTP_MAGIC);
		CHECK_OBJ_NOTNULL(w->resp, HTTP_MAGIC);
		CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);

		/* Process queued requests, if any */
		w->wrq = VTAILQ_FIRST(&qp->queue);
		if (w->wrq != NULL) {
			VTAILQ_REMOVE(&qp->queue, w->wrq, list);
			qp->lqueue--;
		} else {
			if (isnan(w->lastused))
				w->lastused = TIM_real();
			VTAILQ_INSERT_HEAD(&qp->idle, w, list);
			if (!stats_clean)
				WRK_SumStat(w);
			Lck_CondWait(&w->cond, &qp->mtx);
		}
		if (w->wrq == NULL)
			break;
		Lck_Unlock(&qp->mtx);
		stats_clean = 0;
		AN(w->wrq);
		AN(w->wrq->func);
		w->lastused = NAN;
		WS_Reset(w->ws, NULL);
		w->storage_hint = NULL;

		w->wrq->func(w, w->wrq->priv);

		WS_Assert(w->ws);
		AZ(w->bereq->ws);
		AZ(w->beresp->ws);
		AZ(w->resp->ws);
		AZ(w->wrw.wfd);
		AZ(w->storage_hint);
		assert(w->wlp == w->wlb);
		w->wrq = NULL;
		if (params->diag_bitmap & 0x00040000) {
			if (w->vcl != NULL)
				VCL_Rel(&w->vcl);
		}
		if (!Lck_Trylock(&wstat_mtx)) {
			wrk_sumstat(w);
			Lck_Unlock(&wstat_mtx);
			stats_clean = 1;
		}
		Lck_Lock(&qp->mtx);
	}
	qp->nthr--;
	Lck_Unlock(&qp->mtx);

	VSL(SLT_WorkThread, 0, "%p end", w);
	if (w->vcl != NULL)
		VCL_Rel(&w->vcl);
	AZ(pthread_cond_destroy(&w->cond));
	HSH_Cleanup(w);
	WRK_SumStat(w);
	return (NULL);
}

static void *
wrk_thread(void *priv)
{
	struct wq *qp;
	unsigned nhttp;
	unsigned siov;

	CAST_OBJ_NOTNULL(qp, priv, WQ_MAGIC);
	/* We need to snapshot these two for consistency */
	nhttp = params->http_max_hdr;
	siov = nhttp * 2;
	if (siov > IOV_MAX)
		siov = IOV_MAX;
	return (wrk_thread_real(qp,
	    params->shm_workspace,
	    params->wthread_workspace,
	    nhttp, HTTP_estimate(nhttp), siov));
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

	/* If we have too much in the queue already, refuse. */
	if (qp->lqueue > queue_max) {
		qp->ndrop++;
		Lck_Unlock(&qp->mtx);
		return (-1);
	}

	VTAILQ_INSERT_TAIL(&qp->queue, wrq, list);
	qp->nqueue++;
	qp->lqueue++;
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
	AZ(sess->wrk);
	THR_SetSession(sess);
	sess->wrk = w;
	CHECK_OBJ_ORNULL(w->nobjhead, OBJHEAD_MAGIC);
	CNT_Session(sess);
	CHECK_OBJ_ORNULL(w->nobjhead, OBJHEAD_MAGIC);
	THR_SetSession(NULL);
}

/*--------------------------------------------------------------------*/

int
WRK_QueueSession(struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	AZ(sp->wrk);
	sp->workreq.func = wrk_do_cnt_sess;
	sp->workreq.priv = sp;
	if (WRK_Queue(&sp->workreq) == 0)
		return (0);

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
	return (1);
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
		wq[u] = calloc(sizeof *wq[0], 1);
		XXXAN(wq[u]);
		wq[u]->magic = WQ_MAGIC;
		Lck_New(&wq[u]->mtx, lck_wq);
		VTAILQ_INIT(&wq[u]->queue);
		VTAILQ_INIT(&wq[u]->idle);
	}
	(void)owq;	/* XXX: avoid race, leak it. */
	nwq = pools;
}

/*--------------------------------------------------------------------
 * If a thread is idle or excess, pick it out of the pool.
 */

static void
wrk_decimate_flock(struct wq *qp, double t_idle, struct vsc_main *vs)
{
	struct worker *w = NULL;

	Lck_Lock(&qp->mtx);
	vs->n_wrk += qp->nthr;
	vs->n_wrk_lqueue += qp->lqueue;
	vs->n_wrk_drop += qp->ndrop;
	vs->n_wrk_queued += qp->nqueue;

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
		TIM_sleep(params->wthread_purge_delay * 1e-3);
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
	struct vsc_main vsm, *vs;
	int errno_is_multi_threaded;

	THR_SetName("wrk_herdtimer");

	/*
	 * This is one of the first threads created, test to see that
	 * errno is really per thread.  If this fails, your C-compiler
	 * needs some magic argument (-mt, -pthread, -pthreads etc etc).
	 */
	errno = 0;
	AN(unlink("/"));		/* This had better fail */
	errno_is_multi_threaded = errno;
	assert(errno_is_multi_threaded != 0);

	memset(&vsm, 0, sizeof vsm);
	vs = &vsm;

	(void)priv;
	while (1) {
		/* Add Pools */
		u = params->wthread_pools;
		if (u > nwq)
			wrk_addpools(u);

		/* Scale parameters */

		u = params->wthread_max;
		if (u < params->wthread_min)
			u = params->wthread_min;
		nthr_max = u;

		queue_max = (nthr_max * params->queue_max) / 100;

		vs->n_wrk = 0;
		vs->n_wrk_lqueue = 0;
		vs->n_wrk_drop = 0;
		vs->n_wrk_queued = 0;

		t_idle = TIM_real() - params->wthread_timeout;
		for (u = 0; u < nwq; u++)
			wrk_decimate_flock(wq[u], t_idle, vs);

		VSC_main->n_wrk= vs->n_wrk;
		VSC_main->n_wrk_lqueue = vs->n_wrk_lqueue;
		VSC_main->n_wrk_drop = vs->n_wrk_drop;
		VSC_main->n_wrk_queued = vs->n_wrk_queued;

		TIM_sleep(params->wthread_purge_delay * 1e-3);
	}
	NEEDLESS_RETURN(NULL);
}

/*--------------------------------------------------------------------
 * Create another thread, if necessary & possible
 */

static void
wrk_breed_flock(struct wq *qp, const pthread_attr_t *tp_attr)
{
	pthread_t tp;

	/*
	 * If we need more threads, and have space, create
	 * one more thread.
	 */
	if (qp->nthr < params->wthread_min ||	/* Not enough threads yet */
	    (qp->lqueue > params->wthread_add_threshold && /* more needed */
	    qp->lqueue > qp->last_lqueue)) {	/* not getting better since last */
		if (qp->nthr >= nthr_max) {
			VSC_main->n_wrk_max++;
		} else if (pthread_create(&tp, tp_attr, wrk_thread, qp)) {
			VSL(SLT_Debug, 0, "Create worker thread failed %d %s",
			    errno, strerror(errno));
			VSC_main->n_wrk_failed++;
			TIM_sleep(params->wthread_fail_delay * 1e-3);
		} else {
			AZ(pthread_detach(tp));
			VSC_main->n_wrk_create++;
			TIM_sleep(params->wthread_add_delay * 1e-3);
		}
	}
	qp->last_lqueue = qp->lqueue;
}

/*--------------------------------------------------------------------
 * This thread wakes up whenever a pool queues.
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
	pthread_attr_t tp_attr;

	/* Set the stacksize for worker threads */
	AZ(pthread_attr_init(&tp_attr));

	THR_SetName("wrk_herder");
	(void)priv;
	while (1) {
		for (u = 0 ; u < nwq; u++) {
			if (params->wthread_stacksize != UINT_MAX)
				AZ(pthread_attr_setstacksize(&tp_attr,
				    params->wthread_stacksize));

			wrk_breed_flock(wq[u], &tp_attr);

			/*
			 * Make sure all pools have their minimum complement
			 */
			for (w = 0 ; w < nwq; w++)
				while (wq[w]->nthr < params->wthread_min)
					wrk_breed_flock(wq[w], &tp_attr);
			/*
			 * We cannot avoid getting a mutex, so we have a
			 * bogo mutex just for POSIX_STUPIDITY
			 */
			Lck_Lock(&herder_mtx);
			Lck_CondWait(&herder_cond, &herder_mtx);
			Lck_Unlock(&herder_mtx);
		}
	}
	NEEDLESS_RETURN(NULL);
}

/*--------------------------------------------------------------------
 * Create and starte a back-ground thread which as its own worker and
 * session data structures;
 */

struct bgthread {
	unsigned	magic;
#define BGTHREAD_MAGIC	0x23b5152b
	const char	*name;
	bgthread_t	*func;
	void		*priv;
};

static void *
wrk_bgthread(void *arg)
{
	struct bgthread *bt;
	struct worker ww;
	struct sess *sp;
	uint32_t logbuf[1024];	/* XXX:  size ? */

	CAST_OBJ_NOTNULL(bt, arg, BGTHREAD_MAGIC);
	THR_SetName(bt->name);
	sp = SES_Alloc();
	XXXAN(sp);
	memset(&ww, 0, sizeof ww);
	sp->wrk = &ww;
	ww.magic = WORKER_MAGIC;
	ww.wlp = ww.wlb = logbuf;
	ww.wle = logbuf + (sizeof logbuf) / 4;

	(void)bt->func(sp, bt->priv);

	WRONG("BgThread terminated");

	NEEDLESS_RETURN(NULL);
}

void
WRK_BgThread(pthread_t *thr, const char *name, bgthread_t *func, void *priv)
{
	struct bgthread *bt;

	ALLOC_OBJ(bt, BGTHREAD_MAGIC);
	AN(bt);

	bt->name = name;
	bt->func = func;
	bt->priv = priv;
	AZ(pthread_create(thr, NULL, wrk_bgthread, bt));
}

/*--------------------------------------------------------------------*/

void
WRK_Init(void)
{
	pthread_t tp;

	AZ(pthread_cond_init(&herder_cond, NULL));
	Lck_New(&herder_mtx, lck_herder);
	Lck_New(&wstat_mtx, lck_wstat);

	wrk_addpools(params->wthread_pools);
	AZ(pthread_create(&tp, NULL, wrk_herdtimer_thread, NULL));
	AZ(pthread_detach(tp));
	AZ(pthread_create(&tp, NULL, wrk_herder_thread, NULL));
	AZ(pthread_detach(tp));
}

/*--------------------------------------------------------------------*/
