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
 */

#include "config.h"

#include <stdlib.h>

#include "cache.h"
#include "cache_pool.h"
#include "cache_dstat.h"

static pthread_t		thr_pool_herder;

struct lock			pool_mtx;
static VTAILQ_HEAD(,pool)	pools = VTAILQ_HEAD_INITIALIZER(pools);

/*--------------------------------------------------------------------
 * Facility for scheduling a task on any convenient pool.
 */

int
Pool_Task_Any(struct pool_task *task, enum task_prio prio)
{
	struct pool *pp;

	Lck_Lock(&pool_mtx);
	pp = VTAILQ_FIRST(&pools);
	if (pp != NULL) {
		VTAILQ_REMOVE(&pools, pp, list);
		VTAILQ_INSERT_TAIL(&pools, pp, list);
	}
	Lck_Unlock(&pool_mtx);
	if (pp == NULL)
		return (-1);
	// NB: When we remove pools, is there a race here ?
	return (Pool_Task(pp, task, prio));
}

/*--------------------------------------------------------------------
 * Add a thread pool
 */

static struct pool *
pool_mkpool(unsigned pool_no)
{
	struct pool *pp;
	int i;

	ALLOC_OBJ(pp, POOL_MAGIC);
	if (pp == NULL)
		return (NULL);
	Lck_New(&pp->mtx, lck_wq);

	VTAILQ_INIT(&pp->idle_queue);
	VTAILQ_INIT(&pp->poolsocks);
	for (i = 0; i < TASK_QUEUE_END; i++)
		VTAILQ_INIT(&pp->queues[i]);
	AZ(pthread_cond_init(&pp->herder_cond, NULL));
	AZ(pthread_create(&pp->herder_thr, NULL, pool_herder, pp));

	while (VTAILQ_EMPTY(&pp->idle_queue))
		(void)usleep(10000);

	SES_NewPool(pp, pool_no);
	VCA_NewPool(pp);

	return (pp);
}

/*--------------------------------------------------------------------
 * This thread adjusts the number of pools to match the parameter.
 *
 * NB: This is quite silly.  The master should tell the child through
 * NB: CLI when parameters change and an appropriate call-out table
 * NB: be maintained for params which require action.
 */

static void *
pool_poolherder(void *priv)
{
	unsigned nwq;
	struct pool *pp, *ppx;
	uint64_t u;
	void *rvp;

	THR_SetName("pool_poolherder");
	(void)priv;

	nwq = 0;
	while (1) {
		if (nwq < cache_param->wthread_pools) {
			pp = pool_mkpool(nwq);
			if (pp != NULL) {
				Lck_Lock(&pool_mtx);
				VTAILQ_INSERT_TAIL(&pools, pp, list);
				Lck_Unlock(&pool_mtx);
				VSC_C_main->pools++;
				nwq++;
				continue;
			}
		} else if (nwq > cache_param->wthread_pools &&
				DO_DEBUG(DBG_DROP_POOLS)) {
			Lck_Lock(&pool_mtx);
			pp = VTAILQ_FIRST(&pools);
			AN(pp);
			VTAILQ_REMOVE(&pools, pp, list);
			VTAILQ_INSERT_TAIL(&pools, pp, list);
			if (!pp->die)
				nwq--;
			Lck_Unlock(&pool_mtx);
			if (!pp->die) {
				VSL(SLT_Debug, 0, "XXX Kill Pool %p", pp);
				pp->die = 1;
				VCA_DestroyPool(pp);
				AZ(pthread_cond_signal(&pp->herder_cond));
			}
		}
		(void)sleep(1);
		u = 0;
		ppx = NULL;
		Lck_Lock(&pool_mtx);
		VTAILQ_FOREACH(pp, &pools, list) {
			if (pp->die && pp->nthr == 0)
				ppx = pp;
			u += pp->lqueue;
		}
		if (ppx != NULL) {
			VTAILQ_REMOVE(&pools, ppx, list);
			AZ(pthread_join(ppx->herder_thr, &rvp));
			AZ(pthread_cond_destroy(&ppx->herder_cond));
			SES_DestroyPool(ppx);
			FREE_OBJ(ppx);
			VSC_C_main->pools--;
		}
		Lck_Unlock(&pool_mtx);
		VSC_C_main->thread_queue_len = u;
	}
	NEEDLESS(return NULL);
}

/*--------------------------------------------------------------------*/

void
Pool_Init(void)
{
	Lck_New(&pool_mtx, lck_wq);
	AZ(pthread_create(&thr_pool_herder, NULL, pool_poolherder, NULL));
	while (!VSC_C_main->pools)
		(void)usleep(10000);
}
