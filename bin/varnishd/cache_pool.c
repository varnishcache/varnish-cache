/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
#include "cache_waiter.h"
#include "stevedore.h"
#include "hash_slinger.h"

static void *waiter_priv;

VTAILQ_HEAD(workerhead, worker);

/* Number of work requests queued in excess of worker threads available */

struct pool {
	unsigned		magic;
#define POOL_MAGIC		0x606658fa
	struct lock		mtx;
	struct workerhead	idle;
	VTAILQ_HEAD(, sess)	queue;
	unsigned		nthr;
	unsigned		lqueue;
	unsigned		last_lqueue;
	uintmax_t		ndrop;
	uintmax_t		nqueue;
};

static struct pool		**wq;
static unsigned			nwq;
static unsigned			queue_max;
static unsigned			nthr_max;

static pthread_cond_t		herder_cond;
static struct lock		herder_mtx;

/*--------------------------------------------------------------------*/

void
Pool_Work_Thread(void *priv, struct worker *w)
{
	struct pool *qp;
	int stats_clean;

	CAST_OBJ_NOTNULL(qp, priv, POOL_MAGIC);
	w->pool = qp;
	Lck_Lock(&qp->mtx);
	qp->nthr++;
	stats_clean = 1;
	while (1) {
		CHECK_OBJ_NOTNULL(w->bereq, HTTP_MAGIC);
		CHECK_OBJ_NOTNULL(w->beresp, HTTP_MAGIC);
		CHECK_OBJ_NOTNULL(w->resp, HTTP_MAGIC);
		CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);

		/* Process queued requests, if any */
		w->sp = VTAILQ_FIRST(&qp->queue);
		if (w->sp != NULL) {
			VTAILQ_REMOVE(&qp->queue, w->sp, poollist);
			qp->lqueue--;
		} else {
			if (isnan(w->lastused))
				w->lastused = TIM_real();
			VTAILQ_INSERT_HEAD(&qp->idle, w, list);
			if (!stats_clean)
				WRK_SumStat(w);
			Lck_CondWait(&w->cond, &qp->mtx);
		}
		if (w->sp == NULL)
			break;
		Lck_Unlock(&qp->mtx);
		stats_clean = 0;
		w->lastused = NAN;
		WS_Reset(w->ws, NULL);
		w->storage_hint = NULL;

		AZ(w->sp->wrk);
		THR_SetSession(w->sp);
		w->sp->wrk = w;
		CHECK_OBJ_ORNULL(w->nobjhead, OBJHEAD_MAGIC);
		CNT_Session(w->sp);
		CHECK_OBJ_ORNULL(w->nobjhead, OBJHEAD_MAGIC);
		THR_SetSession(NULL);

		WS_Assert(w->ws);
		AZ(w->bereq->ws);
		AZ(w->beresp->ws);
		AZ(w->resp->ws);
		AZ(w->wrw.wfd);
		AZ(w->storage_hint);
		assert(w->wlp == w->wlb);
		w->sp = NULL;
		if (params->diag_bitmap & 0x00040000) {
			if (w->vcl != NULL)
				VCL_Rel(&w->vcl);
		}
		stats_clean = WRK_TrySumStat(w);
		Lck_Lock(&qp->mtx);
	}
	qp->nthr--;
	Lck_Unlock(&qp->mtx);
	w->pool = NULL;
}

/*--------------------------------------------------------------------
 * Queue a workrequest if possible.
 *
 * Return zero if the request was queued, negative if it wasn't.
 */

static int
WRK_Queue(struct sess *sp)
{
	struct worker *w;
	struct pool *qp;
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
		w->sp = sp;
		AZ(pthread_cond_signal(&w->cond));
		return (0);
	}

	/* If we have too much in the queue already, refuse. */
	if (qp->lqueue > queue_max) {
		qp->ndrop++;
		Lck_Unlock(&qp->mtx);
		return (-1);
	}

	VTAILQ_INSERT_TAIL(&qp->queue, sp, poollist);
	qp->nqueue++;
	qp->lqueue++;
	Lck_Unlock(&qp->mtx);
	AZ(pthread_cond_signal(&herder_cond));
	return (0);
}

/*--------------------------------------------------------------------*/

int
Pool_QueueSession(struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	AZ(sp->wrk);
	if (WRK_Queue(sp) == 0)
		return (0);

	/*
	 * Couldn't queue it -- kill it.
	 *
	 * XXX: a notice might be polite, but would potentially
	 * XXX: sleep whichever thread got us here
	 */
	sp->t_end = TIM_real();
	if (sp->vcl != NULL) {
		/*
		 * A session parked on a busy object can come here
		 * after it wakes up.  Loose the VCL reference.
		 */
		VCL_Rel(&sp->vcl);
	}
	SES_Delete(sp, "dropped");
	return (1);
}

/*--------------------------------------------------------------------
 * Add (more) thread pools
 */

static void
wrk_addpools(const unsigned pools)
{
	struct pool **pwq, **owq;
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
		wq[u]->magic = POOL_MAGIC;
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
wrk_decimate_flock(struct pool *qp, double t_idle, struct VSC_C_main *vs)
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
		AZ(w->sp);
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
	struct VSC_C_main vsm, *vs;
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

		VSC_C_main->n_wrk= vs->n_wrk;
		VSC_C_main->n_wrk_lqueue = vs->n_wrk_lqueue;
		VSC_C_main->n_wrk_drop = vs->n_wrk_drop;
		VSC_C_main->n_wrk_queued = vs->n_wrk_queued;

		TIM_sleep(params->wthread_purge_delay * 1e-3);
	}
	NEEDLESS_RETURN(NULL);
}

/*--------------------------------------------------------------------
 * Create another thread, if necessary & possible
 */

static void
wrk_breed_flock(struct pool *qp, const pthread_attr_t *tp_attr)
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
			VSC_C_main->n_wrk_max++;
		} else if (pthread_create(&tp, tp_attr, WRK_thread, qp)) {
			VSL(SLT_Debug, 0, "Create worker thread failed %d %s",
			    errno, strerror(errno));
			VSC_C_main->n_wrk_failed++;
			TIM_sleep(params->wthread_fail_delay * 1e-3);
		} else {
			AZ(pthread_detach(tp));
			VSC_C_main->n_wrk_create++;
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
 * Wait for another request
 */

void
Pool_Wait(struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	AZ(sp->obj);
	AZ(sp->vcl);
	assert(sp->fd >= 0);
	/*
	 * Set nonblocking in the worker-thread, before passing to the
	 * acceptor thread, to reduce syscall density of the latter.
	 */
	if (VTCP_nonblocking(sp->fd))
		SES_Close(sp, "remote closed");
	waiter->pass(waiter_priv, sp);
}

/*--------------------------------------------------------------------*/

void
Pool_Init(void)
{
	pthread_t tp;

	AZ(pthread_cond_init(&herder_cond, NULL));
	Lck_New(&herder_mtx, lck_herder);

	waiter_priv = waiter->init();

	wrk_addpools(params->wthread_pools);
	AZ(pthread_create(&tp, NULL, wrk_herdtimer_thread, NULL));
	AZ(pthread_detach(tp));
	AZ(pthread_create(&tp, NULL, wrk_herder_thread, NULL));
	AZ(pthread_detach(tp));
}

/*--------------------------------------------------------------------*/
