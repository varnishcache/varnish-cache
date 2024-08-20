/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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
 * Worker thread stuff unrelated to the worker thread pools.
 *
 * --
 * signaling_note:
 *
 * note on worker wakeup signaling through the wrk condition variable (cv)
 *
 * In the general case, a cv needs to be signaled while holding the
 * corresponding mutex, otherwise the signal may be posted before the waiting
 * thread could register itself on the cv and, consequently, the signal may be
 * missed.
 *
 * In our case, any worker thread which we wake up comes from the idle queue,
 * where it put itself under the mutex, releasing that mutex implicitly via
 * Lck_CondWaitUntil() (which calls some variant of pthread_cond_wait). So we avoid
 * additional mutex contention knowing that any worker thread on the idle queue
 * is blocking on the cv.
 *
 * Except -- when it isn't, because it woke up for releasing its VCL
 * Reference. To account for this case, we check if the task function has been
 * set in the meantime, which in turn requires all of the task preparation to be
 * done holding the pool mutex. (see also #2719)
 */

#include "config.h"

#include <stdlib.h>
#include <sched.h>

#include "cache_varnishd.h"
#include "cache_pool.h"

#include "vcli_serve.h"
#include "vtim.h"

#include "hash/hash_slinger.h"

static void Pool_Work_Thread(struct pool *pp, struct worker *wrk);

static uintmax_t reqpoolfail;

/*--------------------------------------------------------------------
 * Create and start a back-ground thread which as its own worker and
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
	struct worker wrk;
	struct worker_priv wpriv[1];
	struct VSC_main_wrk ds;
	void *r;

	CAST_OBJ_NOTNULL(bt, arg, BGTHREAD_MAGIC);
	THR_SetName(bt->name);
	THR_Init();
	INIT_OBJ(&wrk, WORKER_MAGIC);
	INIT_OBJ(wpriv, WORKER_PRIV_MAGIC);
	wrk.wpriv = wpriv;
	// bgthreads do not have a vpi member
	memset(&ds, 0, sizeof ds);
	wrk.stats = &ds;

	r = bt->func(&wrk, bt->priv);
	HSH_Cleanup(&wrk);
	Pool_Sumstat(&wrk);
	return (r);
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
	PTOK(pthread_create(thr, NULL, wrk_bgthread, bt));
}

/*--------------------------------------------------------------------*/

static void
WRK_Thread(struct pool *qp, size_t stacksize, unsigned thread_workspace)
{
	// child_signal_handler stack overflow check uses struct worker addr
	struct worker *w, ww;
	struct VSC_main_wrk ds;
	v_vla_(unsigned char, ws, thread_workspace);
	struct worker_priv wpriv[1];
	v_vla_(unsigned char, vpi, vpi_wrk_len);

	AN(qp);
	AN(stacksize);
	AN(thread_workspace);

	THR_SetName("cache-worker");
	w = &ww;
	INIT_OBJ(w, WORKER_MAGIC);
	INIT_OBJ(wpriv, WORKER_PRIV_MAGIC);
	w->wpriv = wpriv;
	w->lastused = NAN;
	memset(&ds, 0, sizeof ds);
	w->stats = &ds;
	THR_SetWorker(w);
	PTOK(pthread_cond_init(&w->cond, NULL));

	WS_Init(w->aws, "wrk", ws, thread_workspace);
	VPI_wrk_init(w, vpi, sizeof vpi);
	AN(w->vpi);

	VSL(SLT_WorkThread, NO_VXID, "%p start", w);

	Pool_Work_Thread(qp, w);
	AZ(w->pool);

	VSL(SLT_WorkThread, NO_VXID, "%p end", w);
	if (w->wpriv->vcl != NULL)
		VCL_Rel(&w->wpriv->vcl);
	PTOK(pthread_cond_destroy(&w->cond));
	HSH_Cleanup(w);
	Pool_Sumstat(w);
}

/*--------------------------------------------------------------------
 * Summing of stats into pool counters
 */

static unsigned
wrk_addstat(const struct worker *wrk, const struct pool_task *tp, unsigned locked)
{
	struct pool *pp;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	pp = wrk->pool;
	CHECK_OBJ_NOTNULL(pp, POOL_MAGIC);
	if (locked)
		Lck_AssertHeld(&pp->mtx);

	if ((tp == NULL && wrk->stats->summs > 0) ||
	    (wrk->stats->summs >= cache_param->wthread_stats_rate)) {
		if (!locked)
			Lck_Lock(&pp->mtx);

		pp->a_stat->summs++;
		VSC_main_Summ_wrk_wrk(pp->a_stat, wrk->stats);
		memset(wrk->stats, 0, sizeof *wrk->stats);

		if (!locked)
			Lck_Unlock(&pp->mtx);
	}

	return (tp != NULL);
}

void
WRK_AddStat(const struct worker *wrk)
{

	(void)wrk_addstat(wrk, wrk->task, 0);
	wrk->stats->summs++;
}

/*--------------------------------------------------------------------
 * Pool reserve calculation
 */

static unsigned
pool_reserve(void)
{
	unsigned lim;

	if (cache_param->wthread_reserve == 0) {
		lim = cache_param->wthread_min / 20 + 1;
	} else {
		lim = cache_param->wthread_min * 950 / 1000;
		if (cache_param->wthread_reserve < lim)
			lim = cache_param->wthread_reserve;
	}
	if (lim < TASK_QUEUE_RESERVE)
		return (TASK_QUEUE_RESERVE);
	return (lim);
}

/*--------------------------------------------------------------------*/

static struct worker *
pool_getidleworker(struct pool *pp, enum task_prio prio)
{
	struct pool_task *pt = NULL;
	struct worker *wrk;

	CHECK_OBJ_NOTNULL(pp, POOL_MAGIC);
	Lck_AssertHeld(&pp->mtx);
	if (pp->nidle > (pool_reserve() * prio / TASK_QUEUE_RESERVE)) {
		pt = VTAILQ_FIRST(&pp->idle_queue);
		if (pt == NULL)
			AZ(pp->nidle);
	}

	if (pt == NULL)
		return (NULL);

	AZ(pt->func);
	CAST_OBJ_NOTNULL(wrk, pt->priv, WORKER_MAGIC);

	AN(pp->nidle);
	VTAILQ_REMOVE(&pp->idle_queue, wrk->task, list);
	pp->nidle--;

	return (wrk);
}

/*--------------------------------------------------------------------
 * Special scheduling:  If no thread can be found, the current thread
 * will be prepared for rescheduling instead.
 * The selected threads workspace is reserved and the argument put there.
 * Return one if another thread was scheduled, otherwise zero.
 */

int
Pool_Task_Arg(struct worker *wrk, enum task_prio prio, task_func_t *func,
    const void *arg, size_t arg_len)
{
	struct pool *pp;
	struct worker *wrk2;
	int retval;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AN(arg);
	AN(arg_len);
	pp = wrk->pool;
	CHECK_OBJ_NOTNULL(pp, POOL_MAGIC);

	Lck_Lock(&pp->mtx);
	wrk2 = pool_getidleworker(pp, prio);
	if (wrk2 != NULL)
		retval = 1;
	else {
		wrk2 = wrk;
		retval = 0;
	}
	AZ(wrk2->task->func);
	assert(arg_len <= WS_ReserveSize(wrk2->aws, arg_len));
	memcpy(WS_Reservation(wrk2->aws), arg, arg_len);
	wrk2->task->func = func;
	wrk2->task->priv = WS_Reservation(wrk2->aws);
	Lck_Unlock(&pp->mtx);
	// see signaling_note at the top for explanation
	if (retval)
		PTOK(pthread_cond_signal(&wrk2->cond));
	return (retval);
}

/*--------------------------------------------------------------------
 * Enter a new task to be done
 */

int
Pool_Task(struct pool *pp, struct pool_task *task, enum task_prio prio)
{
	struct worker *wrk;
	int retval = 0;
	CHECK_OBJ_NOTNULL(pp, POOL_MAGIC);
	AN(task);
	AN(task->func);
	assert(prio < TASK_QUEUE__END);

	if (prio == TASK_QUEUE_REQ && reqpoolfail) {
		retval = reqpoolfail & 1;
		reqpoolfail >>= 1;
		if (retval) {
			VSL(SLT_Debug, NO_VXID,
			    "Failing due to reqpoolfail (next= 0x%jx)",
			    reqpoolfail);
			return (retval);
		}
	}

	Lck_Lock(&pp->mtx);

	/* The common case first:  Take an idle thread, do it. */

	wrk = pool_getidleworker(pp, prio);
	if (wrk != NULL) {
		AZ(wrk->task->func);
		wrk->task->func = task->func;
		wrk->task->priv = task->priv;
		Lck_Unlock(&pp->mtx);
		// see signaling_note at the top for explanation
		PTOK(pthread_cond_signal(&wrk->cond));
		return (0);
	}

	/* Vital work is always queued. Only priority classes that can
	 * fit under the reserve capacity are eligible to queuing.
	 */
	if (prio >= TASK_QUEUE_RESERVE) {
		retval = -1;
	} else if (!TASK_QUEUE_LIMITED(prio) ||
	    pp->lqueue + pp->nthr < cache_param->wthread_max +
	    cache_param->wthread_queue_limit) {
		pp->stats->sess_queued++;
		pp->lqueue++;
		VTAILQ_INSERT_TAIL(&pp->queues[prio], task, list);
		PTOK(pthread_cond_signal(&pp->herder_cond));
	} else {
		/* NB: This is counter-intuitive but when we drop a REQ
		 * task, it is an HTTP/1 request and we effectively drop
		 * the whole session. It is otherwise an h2 stream with
		 * STR priority in which case we are dropping a request.
		 */
		if (prio == TASK_QUEUE_REQ)
			pp->stats->sess_dropped++;
		else
			pp->stats->req_dropped++;
		retval = -1;
	}
	Lck_Unlock(&pp->mtx);
	return (retval);
}

/*--------------------------------------------------------------------
 * Empty function used as a pointer value for the thread exit condition.
 */

static void v_matchproto_(task_func_t)
pool_kiss_of_death(struct worker *wrk, void *priv)
{
	(void)wrk;
	(void)priv;
}


/*--------------------------------------------------------------------
 * This is the work function for worker threads in the pool.
 */

static void
Pool_Work_Thread(struct pool *pp, struct worker *wrk)
{
	struct pool_task *tp;
	struct pool_task tpx, tps;
	vtim_real tmo, now;
	unsigned i, reserve;

	CHECK_OBJ_NOTNULL(pp, POOL_MAGIC);
	wrk->pool = pp;
	while (1) {
		CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
		tp = NULL;

		WS_Rollback(wrk->aws, 0);
		AZ(wrk->vsl);

		Lck_Lock(&pp->mtx);
		reserve = pool_reserve();

		for (i = 0; i < TASK_QUEUE_RESERVE; i++) {
			if (pp->nidle < (reserve * i / TASK_QUEUE_RESERVE))
				break;
			tp = VTAILQ_FIRST(&pp->queues[i]);
			if (tp != NULL) {
				pp->lqueue--;
				pp->ndequeued--;
				VTAILQ_REMOVE(&pp->queues[i], tp, list);
				break;
			}
		}

		if (wrk_addstat(wrk, tp, 1)) {
			wrk->stats->summs++;
			AN(tp);
		} else if (pp->b_stat != NULL && pp->a_stat->summs) {
			/* Nothing to do, push pool stats into global pool */
			tps.func = pool_stat_summ;
			tps.priv = pp->a_stat;
			pp->a_stat = pp->b_stat;
			pp->b_stat = NULL;
			tp = &tps;
		} else {
			/* Nothing to do: To sleep, perchance to dream ... */
			if (isnan(wrk->lastused))
				wrk->lastused = VTIM_real();
			wrk->task->func = NULL;
			wrk->task->priv = wrk;
			VTAILQ_INSERT_HEAD(&pp->idle_queue, wrk->task, list);
			pp->nidle++;
			now = wrk->lastused;
			do {
				// see signaling_note at the top for explanation
				if (DO_DEBUG(DBG_VCLREL) &&
				    pp->b_stat == NULL && pp->a_stat->summs)
					/* We've released the VCL, but
					 * there are pool stats not pushed
					 * to the global stats and some
					 * thread is busy pushing
					 * stats. Set a 1 second timeout
					 * so that we'll wake up and get a
					 * chance to push stats. */
					tmo = now + 1.;
				else if (wrk->wpriv->vcl == NULL)
					tmo = INFINITY;
				else if (DO_DEBUG(DBG_VTC_MODE))
					tmo = now + 1.;
				else
					tmo = now + 60.;
				(void)Lck_CondWaitUntil(
				    &wrk->cond, &pp->mtx, tmo);
				if (wrk->task->func != NULL) {
					/* We have been handed a new task */
					tpx = *wrk->task;
					tp = &tpx;
					wrk->stats->summs++;
				} else if (pp->b_stat != NULL &&
				    pp->a_stat->summs) {
					/* Woken up to release the VCL,
					 * and noticing that there are
					 * pool stats not pushed to the
					 * global stats and no active
					 * thread currently doing
					 * it. Remove ourself from the
					 * idle queue and take on the
					 * task. */
					assert(pp->nidle > 0);
					VTAILQ_REMOVE(&pp->idle_queue,
					    wrk->task, list);
					pp->nidle--;
					tps.func = pool_stat_summ;
					tps.priv = pp->a_stat;
					pp->a_stat = pp->b_stat;
					pp->b_stat = NULL;
					tp = &tps;
				} else {
					// Presumably ETIMEDOUT but we do not
					// assert this because pthread condvars
					// are not airtight.
					if (wrk->wpriv->vcl)
						VCL_Rel(&wrk->wpriv->vcl);
					now = VTIM_real();
				}
			} while (tp == NULL);
		}
		Lck_Unlock(&pp->mtx);

		if (tp->func == pool_kiss_of_death)
			break;

		do {
			memset(wrk->task, 0, sizeof wrk->task);
			assert(wrk->pool == pp);
			AN(tp->func);
			tp->func(wrk, tp->priv);
			if (DO_DEBUG(DBG_VCLREL) && wrk->wpriv->vcl != NULL)
				VCL_Rel(&wrk->wpriv->vcl);
			tpx = *wrk->task;
			tp = &tpx;
		} while (tp->func != NULL);

		if (WS_Overflowed(wrk->aws))
			wrk->stats->ws_thread_overflow++;
		/* cleanup for next task */
		wrk->seen_methods = 0;
	}
	wrk->pool = NULL;
}

/*--------------------------------------------------------------------
 * Create another worker thread.
 */

struct pool_info {
	unsigned		magic;
#define POOL_INFO_MAGIC		0x4e4442d3
	size_t			stacksize;
	struct pool		*qp;
};

static void *
pool_thread(void *priv)
{
	struct pool_info *pi;

	CAST_OBJ_NOTNULL(pi, priv, POOL_INFO_MAGIC);
	THR_Init();
	WRK_Thread(pi->qp, pi->stacksize, cache_param->workspace_thread);
	FREE_OBJ(pi);
	return (NULL);
}

static void
pool_breed(struct pool *qp)
{
	pthread_t tp;
	pthread_attr_t tp_attr;
	struct pool_info *pi;

	PTOK(pthread_attr_init(&tp_attr));
	PTOK(pthread_attr_setdetachstate(&tp_attr, PTHREAD_CREATE_DETACHED));

	/* Set the stacksize for worker threads we create */
	if (cache_param->wthread_stacksize != UINT_MAX)
		PTOK(pthread_attr_setstacksize(&tp_attr, cache_param->wthread_stacksize));

	ALLOC_OBJ(pi, POOL_INFO_MAGIC);
	AN(pi);
	PTOK(pthread_attr_getstacksize(&tp_attr, &pi->stacksize));
	pi->qp = qp;

	errno = pthread_create(&tp, &tp_attr, pool_thread, pi);
	if (errno) {
		FREE_OBJ(pi);
		VSL(SLT_Debug, NO_VXID, "Create worker thread failed %d %s",
		    errno, VAS_errtxt(errno));
		Lck_Lock(&pool_mtx);
		VSC_C_main->threads_failed++;
		Lck_Unlock(&pool_mtx);
		VTIM_sleep(cache_param->wthread_fail_delay);
	} else {
		qp->nthr++;
		Lck_Lock(&pool_mtx);
		VSC_C_main->threads++;
		VSC_C_main->threads_created++;
		Lck_Unlock(&pool_mtx);
		if (cache_param->wthread_add_delay > 0.0)
			VTIM_sleep(cache_param->wthread_add_delay);
		else
			(void)sched_yield();
	}

	PTOK(pthread_attr_destroy(&tp_attr));
}

/*--------------------------------------------------------------------
 * Herd a single pool
 *
 * This thread wakes up every thread_pool_timeout seconds, whenever a pool
 * queues and when threads need to be destroyed
 *
 * The trick here is to not be too aggressive about creating threads.  In
 * pool_breed(), we sleep whenever we create a thread and a little while longer
 * whenever we fail to, hopefully missing a lot of cond_signals in the meantime.
 *
 * Idle threads are destroyed at a rate determined by wthread_destroy_delay
 *
 * XXX: probably need a lot more work.
 *
 */

void*
pool_herder(void *priv)
{
	struct pool *pp;
	struct pool_task *pt;
	double t_idle;
	struct worker *wrk;
	double delay;
	unsigned wthread_min;
	uintmax_t dq = (1ULL << 31);
	vtim_mono dqt = 0;
	int r = 0;

	CAST_OBJ_NOTNULL(pp, priv, POOL_MAGIC);

	THR_SetName("pool_herder");
	THR_Init();

	while (!pp->die || pp->nthr > 0) {
		/*
		 * If the worker pool is configured too small, we can
		 * end up deadlocking it (see #2418 for details).
		 *
		 * Recovering from this would require a lot of complicated
		 * code, and fundamentally, either people configured their
		 * pools wrong, in which case we want them to notice, or
		 * they are under DoS, in which case recovering gracefully
		 * is unlikely be a major improvement.
		 *
		 * Instead we implement a watchdog and kill the worker if
		 * nothing has been dequeued for that long.
		 */
		if (VTAILQ_EMPTY(&pp->queues[TASK_QUEUE_HIGHEST_PRIORITY])) {
			/* Watchdog only applies to no movement on the
			 * highest priority queue (TASK_QUEUE_BO) */
			dq = pp->ndequeued + 1;
		} else if (dq != pp->ndequeued) {
			dq = pp->ndequeued;
			dqt = VTIM_mono();
		} else if (VTIM_mono() - dqt > cache_param->wthread_watchdog) {
			VSL(SLT_Error, NO_VXID,
			    "Pool Herder: Queue does not move ql=%u dt=%f",
			    pp->lqueue, VTIM_mono() - dqt);
			WRONG("Worker Pool Queue does not move"
			      " - see thread_pool_watchdog parameter");
		}
		wthread_min = cache_param->wthread_min;
		if (pp->die)
			wthread_min = 0;

		/* Make more threads if needed and allowed */
		if (pp->nthr < wthread_min ||
		    (pp->lqueue > 0 && pp->nthr < cache_param->wthread_max)) {
			pool_breed(pp);
			continue;
		}

		delay = cache_param->wthread_timeout;
		assert(pp->nthr >= wthread_min);

		if (pp->nthr > wthread_min) {

			t_idle = VTIM_real() - cache_param->wthread_timeout;

			Lck_Lock(&pp->mtx);
			wrk = NULL;
			pt = VTAILQ_LAST(&pp->idle_queue, taskhead);
			if (pt != NULL) {
				AN(pp->nidle);
				AZ(pt->func);
				CAST_OBJ_NOTNULL(wrk, pt->priv, WORKER_MAGIC);

				if (pp->die || wrk->lastused < t_idle ||
				    pp->nthr > cache_param->wthread_max) {
					/* Give it a kiss on the cheek... */
					VTAILQ_REMOVE(&pp->idle_queue,
					    wrk->task, list);
					pp->nidle--;
					wrk->task->func = pool_kiss_of_death;
					PTOK(pthread_cond_signal(&wrk->cond));
				} else {
					delay = wrk->lastused - t_idle;
					wrk = NULL;
				}
			}
			Lck_Unlock(&pp->mtx);

			if (wrk != NULL) {
				pp->nthr--;
				Lck_Lock(&pool_mtx);
				VSC_C_main->threads--;
				VSC_C_main->threads_destroyed++;
				Lck_Unlock(&pool_mtx);
				delay = cache_param->wthread_destroy_delay;
			} else
				delay = vmax(delay,
				    cache_param->wthread_destroy_delay);
		}

		if (pp->die) {
			if (delay < 2)
				delay = .01;
			else
				delay = 1;
			VTIM_sleep(delay);
			continue;
		}
		Lck_Lock(&pp->mtx);
		if (pp->lqueue == 0) {
			if (DO_DEBUG(DBG_VTC_MODE))
				delay = 0.5;
			r = Lck_CondWaitTimeout(
			    &pp->herder_cond, &pp->mtx, delay);
		} else if (pp->nthr >= cache_param->wthread_max) {
			/* XXX: unsafe counters */
			if (r != ETIMEDOUT)
				VSC_C_main->threads_limited++;
			r = Lck_CondWaitTimeout(
			    &pp->herder_cond, &pp->mtx, 1.0);
		}
		Lck_Unlock(&pp->mtx);
	}
	return (NULL);
}

/*--------------------------------------------------------------------
 * Debugging aids
 */

static void v_matchproto_(cli_func_t)
debug_reqpoolfail(struct cli *cli, const char * const *av, void *priv)
{
	uintmax_t u = 1;
	const char *p;

	(void)priv;
	(void)cli;
	reqpoolfail = 0;
	for (p = av[2]; *p != '\0'; p++) {
		if (*p == 'F' || *p == 'f')
			reqpoolfail |= u;
		u <<= 1;
	}
}

static struct cli_proto debug_cmds[] = {
	{ CLICMD_DEBUG_REQPOOLFAIL,		"d", debug_reqpoolfail },
	{ NULL }
};

void
WRK_Log(enum VSL_tag_e tag, const char *fmt, ...)
{
	struct worker *wrk;
	va_list ap;

	AN(fmt);

	wrk = THR_GetWorker();
	CHECK_OBJ_ORNULL(wrk, WORKER_MAGIC);

	va_start(ap, fmt);
	if (wrk != NULL && wrk->vsl != NULL)
		VSLbv(wrk->vsl, tag, fmt, ap);
	else
		VSLv(tag, NO_VXID, fmt, ap);
	va_end(ap);
}

/*--------------------------------------------------------------------
 *
 */

void
WRK_Init(void)
{
	assert(cache_param->wthread_min >= TASK_QUEUE_RESERVE);
	CLI_AddFuncs(debug_cmds);
}
