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

struct poolsock {
	unsigned			magic;
#define POOLSOCK_MAGIC			0x1b0a2d38
	VTAILQ_ENTRY(poolsock)		list;
	struct listen_sock		*lsock;
};

/* Number of work requests queued in excess of worker threads available */

struct pool {
	unsigned		magic;
#define POOL_MAGIC		0x606658fa
	VTAILQ_ENTRY(pool)	list;
	struct lock		mtx;
	struct workerhead	idle;
	VTAILQ_HEAD(, sess)	queue;
	VTAILQ_HEAD(, poolsock)	socks;
	unsigned		nthr;
	unsigned		lqueue;
	unsigned		last_lqueue;
	uintmax_t		ndrop;
	uintmax_t		nqueue;
	struct sesspool		*sesspool;
};

static VTAILQ_HEAD(,pool)	pools = VTAILQ_HEAD_INITIALIZER(pools);

static unsigned			queue_max;
static unsigned			nthr_max;

static unsigned			nwq;

static pthread_cond_t		herder_cond;
static struct lock		herder_mtx;

/*--------------------------------------------------------------------
 * Nobody is accepting on this socket, so we do.
 *
 * As long as we can stick the accepted connection to another thread
 * we do so, otherwise we return and handle it ourselves.
 *
 * Notice calling convention:  Called locked and returns locked, but
 * works lock in the meantime.
 *
 * We store data about the accept in reserved workspace, it is only used
 * for a brief moment and it takes up around 144 bytes.
 */

static void
pool_accept(struct pool *pp, struct worker *w, const struct poolsock *ps)
{
	struct worker *w2;
	struct wrk_accept *wa, *wa2;

	CHECK_OBJ_NOTNULL(pp, POOL_MAGIC);
	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(ps, POOLSOCK_MAGIC);

	CHECK_OBJ_NOTNULL(ps->lsock, LISTEN_SOCK_MAGIC);
	Lck_AssertHeld(&pp->mtx);
	Lck_Unlock(&pp->mtx);
	assert(sizeof *wa == WS_Reserve(w->ws, sizeof *wa));
	wa = (void*)w->ws->f;
	while (1) {
		memset(wa, 0, sizeof *wa);
		wa->magic = WRK_ACCEPT_MAGIC;

		if (VCA_Accept(ps->lsock, wa) < 0) {
			w->stats.sess_fail++;
			/* We're going to pace in vca anyway... */
			(void)WRK_TrySumStat(w);
			continue;
		}

		Lck_Lock(&pp->mtx);
		if (VTAILQ_EMPTY(&pp->idle))
			return;
		w2 = VTAILQ_FIRST(&pp->idle);
		VTAILQ_REMOVE(&pp->idle, w2, list);
		Lck_Unlock(&pp->mtx);
		assert(sizeof *wa2 == WS_Reserve(w2->ws, sizeof *wa2));
		wa2 = (void*)w2->ws->f;
		memcpy(wa2, wa, sizeof *wa);
		AZ(pthread_cond_signal(&w2->cond));
	}
}

/*--------------------------------------------------------------------
 * This is the work function for worker threads in the pool.
 */

void
Pool_Work_Thread(void *priv, struct worker *w)
{
	struct pool *pp;
	int stats_clean;
	struct poolsock *ps;

	CAST_OBJ_NOTNULL(pp, priv, POOL_MAGIC);
	w->pool = pp;
	Lck_Lock(&pp->mtx);
	pp->nthr++;
	stats_clean = 1;
	while (1) {

		Lck_AssertHeld(&pp->mtx);

		CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
		CHECK_OBJ_NOTNULL(w->bereq, HTTP_MAGIC);
		CHECK_OBJ_NOTNULL(w->beresp, HTTP_MAGIC);
		CHECK_OBJ_NOTNULL(w->resp, HTTP_MAGIC);

		WS_Reset(w->ws, NULL);

		w->sp = VTAILQ_FIRST(&pp->queue);
		if (w->sp != NULL) {
			/* Process queued requests, if any */
			assert(pp->lqueue > 0);
			VTAILQ_REMOVE(&pp->queue, w->sp, poollist);
			pp->lqueue--;
		} else if (!VTAILQ_EMPTY(&pp->socks)) {
			/* Accept on a socket */
			ps = VTAILQ_FIRST(&pp->socks);
			VTAILQ_REMOVE(&pp->socks, ps, list);
			pool_accept(pp, w, ps);
			Lck_AssertHeld(&pp->mtx);
			VTAILQ_INSERT_TAIL(&pp->socks, ps, list);
		} else if (VTAILQ_EMPTY(&pp->socks)) {
			/* Nothing to do: To sleep, perchance to dream ... */
			if (isnan(w->lastused))
				w->lastused = TIM_real();
			VTAILQ_INSERT_HEAD(&pp->idle, w, list);
			if (!stats_clean)
				WRK_SumStat(w);
			Lck_CondWait(&w->cond, &pp->mtx);
		}

		/*
		 * If we got neither session or accepted a socket, we were
		 * woken up to die to cull the herd.
		 */
		if (w->sp == NULL && w->ws->r == NULL)
			break;

		Lck_Unlock(&pp->mtx);

		if (w->sp == NULL) {
			/* Turn accepted socket into a session */
			assert(w->ws->r != NULL);
			w->sp = SES_New(w, pp->sesspool);
			if (w->sp == NULL)
				VCA_FailSess(w);
			else
				VCA_SetupSess(w);
			WS_Release(w->ws, 0);
		}
		assert(w->ws->r == NULL);

		if (w->sp != NULL) {
			CHECK_OBJ_NOTNULL(w->sp, SESS_MAGIC);

			stats_clean = 0;
			w->lastused = NAN;
			w->storage_hint = NULL;

			AZ(w->sp->wrk);
			THR_SetSession(w->sp);
			w->sp->wrk = w;
			CHECK_OBJ_ORNULL(w->nobjhead, OBJHEAD_MAGIC);
			CNT_Session(w->sp);
			CHECK_OBJ_ORNULL(w->nobjhead, OBJHEAD_MAGIC);
			THR_SetSession(NULL);
			w->sp = NULL;

			WS_Assert(w->ws);
			AZ(w->bereq->ws);
			AZ(w->beresp->ws);
			AZ(w->resp->ws);
			AZ(w->wrw.wfd);
			AZ(w->storage_hint);
			assert(w->wlp == w->wlb);
			if (params->diag_bitmap & 0x00040000) {
				if (w->vcl != NULL)
					VCL_Rel(&w->vcl);
			}
		}
		stats_clean = WRK_TrySumStat(w);
		Lck_Lock(&pp->mtx);
	}
	assert(pp->nthr > 0);
	pp->nthr--;
	Lck_Unlock(&pp->mtx);
	w->pool = NULL;
}

/*--------------------------------------------------------------------
 * Queue a workrequest if possible.
 *
 * Return zero if the request was queued, negative if it wasn't.
 */

static int
WRK_Queue(struct pool *pp, struct sess *sp)
{
	struct worker *w;

	Lck_Lock(&pp->mtx);

	/* If there are idle threads, we tickle the first one into action */
	w = VTAILQ_FIRST(&pp->idle);
	if (w != NULL) {
		VTAILQ_REMOVE(&pp->idle, w, list);
		Lck_Unlock(&pp->mtx);
		w->sp = sp;
		AZ(pthread_cond_signal(&w->cond));
		return (0);
	}

	/* If we have too much in the queue already, refuse. */
	if (pp->lqueue > queue_max) {
		pp->ndrop++;
		Lck_Unlock(&pp->mtx);
		return (-1);
	}

	VTAILQ_INSERT_TAIL(&pp->queue, sp, poollist);
	pp->nqueue++;
	pp->lqueue++;
	Lck_Unlock(&pp->mtx);
	AZ(pthread_cond_signal(&herder_cond));
	return (0);
}

/*--------------------------------------------------------------------*/

int
Pool_Schedule(struct pool *pp, struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	AZ(sp->wrk);
	if (WRK_Queue(pp, sp) == 0)
		return(0);

	VSC_C_main->client_drop_late++;

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
	return (1);
}

/*--------------------------------------------------------------------
 * Add (more) thread pools
 */

static struct pool *
pool_mkpool(void)
{
	struct pool *pp;
	struct listen_sock *ls;
	struct poolsock *ps;

	ALLOC_OBJ(pp, POOL_MAGIC);
	XXXAN(pp);
	Lck_New(&pp->mtx, lck_wq);
	VTAILQ_INIT(&pp->queue);
	VTAILQ_INIT(&pp->idle);
	VTAILQ_INIT(&pp->socks);
	pp->sesspool = SES_NewPool(pp);
	AN(pp->sesspool);

	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		if (ls->sock < 0)
			continue;
		ALLOC_OBJ(ps, POOLSOCK_MAGIC);
		XXXAN(ps);
		ps->lsock = ls;
		VTAILQ_INSERT_TAIL(&pp->socks, ps, list);
	}
	VTAILQ_INSERT_TAIL(&pools, pp, list);
	return (pp);
}

static void
wrk_addpools(const unsigned npools)
{
	struct pool *pp;
	unsigned u;

	for (u = nwq; u < npools; u++) {
		pp = pool_mkpool();
		XXXAN(pp);
	}
	nwq = npools;
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
	struct pool *pp;

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
		VTAILQ_FOREACH(pp, &pools, list)
			wrk_decimate_flock(pp, t_idle, vs);

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
	pthread_attr_t tp_attr;
	struct pool *pp, *pp2;

	/* Set the stacksize for worker threads */
	AZ(pthread_attr_init(&tp_attr));

	THR_SetName("wrk_herder");
	(void)priv;
	while (1) {
		VTAILQ_FOREACH(pp, &pools, list) {
			if (params->wthread_stacksize != UINT_MAX)
				AZ(pthread_attr_setstacksize(&tp_attr,
				    params->wthread_stacksize));

			wrk_breed_flock(pp, &tp_attr);

			/*
			 * Make sure all pools have their minimum complement
			 */
			VTAILQ_FOREACH(pp2, &pools, list)
				while (pp2->nthr < params->wthread_min)
					wrk_breed_flock(pp2, &tp_attr);
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
