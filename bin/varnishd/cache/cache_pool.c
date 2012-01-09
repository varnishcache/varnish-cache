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

#include <math.h>
#include <stdlib.h>

#include "cache.h"
#include "common/heritage.h"

#include "waiter/cache_waiter.h"
#include "vtcp.h"
#include "vtim.h"

/*--------------------------------------------------------------------
 * MAC OS/X is incredibly moronic when it comes to time and such...
 */

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC	0

#include <sys/time.h>

static int
clock_gettime(int foo, struct timespec *ts)
{
	struct timeval tv;

	(void)foo;
	gettimeofday(&tv, NULL);
	ts->tv_sec = tv.tv_sec;
	ts->tv_nsec = tv.tv_usec * 1000;
	return (0);
}

static int
pthread_condattr_setclock(pthread_condattr_t *attr, int foo)
{
	(void)attr;
	(void)foo;
	return (0);
}
#endif /* !CLOCK_MONOTONIC */

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

	pthread_cond_t		herder_cond;
	struct lock		herder_mtx;
	pthread_t		herder_thr;

	struct lock		mtx;
	struct workerhead	idle;
	VTAILQ_HEAD(, sess)	queue;
	VTAILQ_HEAD(, poolsock)	socks;
	unsigned		nthr;
	unsigned		lqueue;
	unsigned		last_lqueue;
	uintmax_t		ndropped;
	uintmax_t		nqueued;
	struct sesspool		*sesspool;
};

static struct lock		pool_mtx;
static pthread_t		thr_pool_herder;

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

static int
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

		if (ps->lsock->sock < 0) {
			/* Socket Shutdown */
			Lck_Lock(&pp->mtx);
			return (-1);
		}
		if (VCA_Accept(ps->lsock, wa) < 0) {
			w->stats.sess_fail++;
			/* We're going to pace in vca anyway... */
			(void)WRK_TrySumStat(w);
			continue;
		}

		Lck_Lock(&pp->mtx);
		if (VTAILQ_EMPTY(&pp->idle))
			return (0);
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
	int stats_clean, i;
	struct poolsock *ps;

	CAST_OBJ_NOTNULL(pp, priv, POOL_MAGIC);
	w->pool = pp;
	Lck_Lock(&pp->mtx);
	stats_clean = 1;
	while (1) {

		Lck_AssertHeld(&pp->mtx);

		CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
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
			i = pool_accept(pp, w, ps);
			Lck_AssertHeld(&pp->mtx);
			if (i < 0) {
				/* Socket Shutdown */
				FREE_OBJ(ps);
				WS_Release(w->ws, 0);
				continue;
			}
			VTAILQ_INSERT_TAIL(&pp->socks, ps, list);
		} else if (VTAILQ_EMPTY(&pp->socks)) {
			/* Nothing to do: To sleep, perchance to dream ... */
			if (isnan(w->lastused))
				w->lastused = VTIM_real();
			VTAILQ_INSERT_HEAD(&pp->idle, w, list);
			if (!stats_clean)
				WRK_SumStat(w);
			(void)Lck_CondWait(&w->cond, &pp->mtx, NULL);
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
			CNT_Session(w->sp);
			THR_SetSession(NULL);
			w->sp = NULL;

			WS_Assert(w->ws);
			AZ(w->busyobj);
			AZ(w->resp->ws);
			AZ(w->wrw.wfd);
			AZ(w->storage_hint);
			assert(w->wlp == w->wlb);
			if (cache_param->diag_bitmap & 0x00040000) {
				if (w->vcl != NULL)
					VCL_Rel(&w->vcl);
			}
		}
		stats_clean = WRK_TrySumStat(w);
		Lck_Lock(&pp->mtx);
	}
	Lck_Unlock(&pp->mtx);
	w->pool = NULL;
}

/*--------------------------------------------------------------------
 * Queue a workrequest if possible.
 *
 * Return zero if the request was queued, negative if it wasn't.
 */

static int
pool_queue(struct pool *pp, struct sess *sp)
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
	if (pp->lqueue > (cache_param->queue_max * pp->nthr) / 100) {
		pp->ndropped++;
		Lck_Unlock(&pp->mtx);
		return (-1);
	}

	VTAILQ_INSERT_TAIL(&pp->queue, sp, poollist);
	pp->nqueued++;
	pp->lqueue++;
	Lck_Unlock(&pp->mtx);
	AZ(pthread_cond_signal(&pp->herder_cond));
	return (0);
}

/*--------------------------------------------------------------------*/

int
Pool_Schedule(struct pool *pp, struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	AZ(sp->wrk);
	if (pool_queue(pp, sp) == 0)
		return(0);

	VSC_C_main->client_drop_late++;

	/*
	 * Couldn't queue it -- kill it.
	 *
	 * XXX: a notice might be polite, but would potentially
	 * XXX: sleep whichever thread got us here
	 */
	sp->t_end = VTIM_real();
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
 * Wait for another request
 */

void
Pool_Wait(struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	AZ(sp->vcl);
	assert(sp->fd >= 0);
	sp->wrk = NULL;

	/*
	 * Set nonblocking in the worker-thread, before passing to the
	 * acceptor thread, to reduce syscall density of the latter.
	 */
	if (VTCP_nonblocking(sp->fd))
		SES_Close(sp, "remote closed");
	waiter->pass(waiter_priv, sp);
}

/*--------------------------------------------------------------------
 * Create another thread, if necessary & possible
 */

static void
pool_breed(struct pool *qp, const pthread_attr_t *tp_attr)
{
	pthread_t tp;

	/*
	 * If we need more threads, and have space, create
	 * one more thread.
	 */
	if (qp->nthr < cache_param->wthread_min ||	/* Not enough threads yet */
	    (qp->lqueue > cache_param->wthread_add_threshold && /* more needed */
	    qp->lqueue > qp->last_lqueue)) {	/* not getting better since last */
		if (qp->nthr > cache_param->wthread_max) {
			Lck_Lock(&pool_mtx);
			VSC_C_main->threads_limited++;
			Lck_Unlock(&pool_mtx);
		} else if (pthread_create(&tp, tp_attr, WRK_thread, qp)) {
			VSL(SLT_Debug, 0, "Create worker thread failed %d %s",
			    errno, strerror(errno));
			Lck_Lock(&pool_mtx);
			VSC_C_main->threads_limited++;
			Lck_Unlock(&pool_mtx);
			VTIM_sleep(cache_param->wthread_fail_delay * 1e-3);
		} else {
			AZ(pthread_detach(tp));
			VTIM_sleep(cache_param->wthread_add_delay * 1e-3);
			qp->nthr++;
			Lck_Lock(&pool_mtx);
			VSC_C_main->threads++;
			VSC_C_main->threads_created++;
			Lck_Unlock(&pool_mtx);
		}
	}
	qp->last_lqueue = qp->lqueue;
}

/*--------------------------------------------------------------------
 * Herd a single pool
 *
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

static void*
pool_herder(void *priv)
{
	struct pool *pp;
	pthread_attr_t tp_attr;
	struct timespec ts;
	double t_idle;
	struct worker *w;
	int i;

	CAST_OBJ_NOTNULL(pp, priv, POOL_MAGIC);
	AZ(pthread_attr_init(&tp_attr));

	while (1) {
		/* Set the stacksize for worker threads we create */
		if (cache_param->wthread_stacksize != UINT_MAX)
			AZ(pthread_attr_setstacksize(&tp_attr,
			    cache_param->wthread_stacksize));
		else {
			AZ(pthread_attr_destroy(&tp_attr));
			AZ(pthread_attr_init(&tp_attr));
		}

		pool_breed(pp, &tp_attr);

		if (pp->nthr < cache_param->wthread_min)
			continue;

		AZ(clock_gettime(CLOCK_MONOTONIC, &ts));
		ts.tv_sec += cache_param->wthread_purge_delay / 1000;
		ts.tv_nsec +=
		    (cache_param->wthread_purge_delay % 1000) * 1000000;
		if (ts.tv_nsec >= 1000000000) {
			ts.tv_sec++;
			ts.tv_nsec -= 1000000000;
		}

		Lck_Lock(&pp->herder_mtx);
		i = Lck_CondWait(&pp->herder_cond, &pp->herder_mtx, &ts);
		Lck_Unlock(&pp->herder_mtx);
		if (!i)
			continue;

		if (pp->nthr <= cache_param->wthread_min)
			continue;

		t_idle = VTIM_real() - cache_param->wthread_timeout;

		Lck_Lock(&pp->mtx);
		VSC_C_main->sess_queued += pp->nqueued;
		VSC_C_main->sess_dropped += pp->ndropped;
		pp->nqueued = pp->ndropped = 0;
		w = VTAILQ_LAST(&pp->idle, workerhead);
		if (w != NULL &&
		    (w->lastused < t_idle || pp->nthr > cache_param->wthread_max)) {
			VTAILQ_REMOVE(&pp->idle, w, list);
		} else
			w = NULL;
		Lck_Unlock(&pp->mtx);

		/* And give it a kiss on the cheek... */
		if (w != NULL) {
			pp->nthr--;
			Lck_Lock(&pool_mtx);
			VSC_C_main->threads--;
			VSC_C_main->threads_destroyed++;
			Lck_Unlock(&pool_mtx);
			AZ(w->sp);
			AZ(pthread_cond_signal(&w->cond));
		}
	}
	NEEDLESS_RETURN(NULL);
}

/*--------------------------------------------------------------------
 * Add a thread pool
 */

static struct pool *
pool_mkpool(void)
{
	struct pool *pp;
	struct listen_sock *ls;
	struct poolsock *ps;
	pthread_condattr_t cv_attr;

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

	AZ(pthread_condattr_init(&cv_attr));
	AZ(pthread_condattr_setclock(&cv_attr, CLOCK_MONOTONIC));
	AZ(pthread_cond_init(&pp->herder_cond, &cv_attr));
	AZ(pthread_condattr_destroy(&cv_attr));
	Lck_New(&pp->herder_mtx, lck_herder);
	AZ(pthread_create(&pp->herder_thr, NULL, pool_herder, pp));

	return (pp);
}

/*--------------------------------------------------------------------
 * This thread adjusts the number of pools to match the parameter.
 *
 */

static void *
pool_poolherder(void *priv)
{
	unsigned nwq;
	VTAILQ_HEAD(,pool)	pools = VTAILQ_HEAD_INITIALIZER(pools);
	struct pool *pp;
	uint64_t u;

	THR_SetName("pool_herder");
	(void)priv;

	nwq = 0;
	while (1) {
		if (nwq < cache_param->wthread_pools) {
			pp = pool_mkpool();
			if (pp != NULL) {
				VTAILQ_INSERT_TAIL(&pools, pp, list);
				VSC_C_main->pools++;
				nwq++;
				continue;
			}
		}
		/* XXX: remove pools */
		if (0)
			SES_DeletePool(NULL, NULL);
		(void)sleep(1);
		u = 0;
		VTAILQ_FOREACH(pp, &pools, list)
			u += pp->lqueue;
		VSC_C_main->thread_queue_len = u;
	}
	NEEDLESS_RETURN(NULL);
}

/*--------------------------------------------------------------------*/

void
Pool_Init(void)
{

	waiter_priv = waiter->init();
	Lck_New(&pool_mtx, lck_wq);
	AZ(pthread_create(&thr_pool_herder, NULL, pool_poolherder, NULL));
}
