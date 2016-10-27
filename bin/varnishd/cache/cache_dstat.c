/*-
 * Copyright 2017 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Author: Nils Goroll <slink@uplex.de>
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
 * Statistics mailman
 */

#include "config.h"

#include "cache.h"
#include "cache_dstat.h"

struct dstat_mbx {
	unsigned		magic;
#define DSTAT_MBX_MAGIC	0x7a12f88e
	struct lock		mtx;
	VSLIST_HEAD(,dstat)	list;
};

struct dstat_mbx mbx;

struct mempool			*dstatpool;

static pthread_t		thr_dstat_mailman;


/*--------------------------------------------------------------------
 * Summing of stats into global stats counters
 */

static inline void
pool_sumstat(const struct dstat *s)
{

#define L0(n)
#define L1(n) (VSC_C_main->n += s->n)
#define VSC_FF(n,t,l,s,f,v,d,e)	L##l(n);
#include "tbl/vsc_f_main.h"
#undef L0
#undef L1
}

/*--------------------------------------------------------------------
 * MPL Get / Free isolation
 */

struct dstat *
Dstat_Get(void) {
	struct dstat *s;

	s = MPL_Get(dstatpool, NULL);
	AN(s);
	INIT_OBJ(s, DSTAT_MAGIC);
	return (s);
}

void
Dstat_Free(struct dstat **s) {
	AN(s);
	AN(*s);
	CHECK_OBJ_NOTNULL(*s, DSTAT_MAGIC);
	MPL_Free(dstatpool, *s);
	*s = NULL;
}

/*--------------------------------------------------------------------
 * submit stats to be added
 */

void
Dstat_Submit(struct dstat **s)
{
	AN(s);
	CHECK_OBJ_NOTNULL(*s, DSTAT_MAGIC);
	CHECK_OBJ(&mbx, DSTAT_MBX_MAGIC);
	Lck_Lock(&mbx.mtx);
	VSLIST_INSERT_HEAD(&mbx.list, *s, list);
	Lck_Unlock(&mbx.mtx);
	*s = NULL;
}

/*--------------------------------------------------------------------
 * Work incoming stats
 */

static void *
dstat_mailman(void *priv)
{
	struct dstat		*s, *t;
	VSLIST_HEAD(,dstat)	work;

	THR_SetName("dstat_mailman");
	(void) priv;

	while(1) {
		(void)usleep(cache_param->thread_stats_latency * 1e6 / 2);

		CHECK_OBJ(&mbx, DSTAT_MBX_MAGIC);

		if (VSLIST_EMPTY(&mbx.list))
			continue;

		VSLIST_INIT(&work);
		Lck_Lock(&mbx.mtx);
		VSLIST_SWAP(&mbx.list, &work, dstat);
		Lck_Unlock(&mbx.mtx);

		VSLIST_FOREACH_SAFE(s, &work, list, t) {
			CHECK_OBJ_NOTNULL(s, DSTAT_MAGIC);
			pool_sumstat(s);
			MPL_Free(dstatpool, s);
		}
	}
	NEEDLESS(return NULL);
}

void
Dstat_Init(void)
{
	unsigned dstat_sz = sizeof(struct dstat);

	dstatpool = MPL_New("dstat", &cache_param->wthread_dstat_pool,
	    MPL_F_SIZE_ISH, &dstat_sz);
	AN(dstatpool);
	INIT_OBJ(&mbx, DSTAT_MBX_MAGIC);
	Lck_New(&mbx.mtx, lck_dstat_mbx);
	VSLIST_INIT(&mbx.list);
	AZ(pthread_create(&thr_dstat_mailman, NULL, dstat_mailman, NULL));
}
