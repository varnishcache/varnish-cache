/*-
 * Copyright (c) 2007-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Dag-Erling Smørgav <des@des.no>
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
 * Least-Recently-Used logic for freeing space in stevedores.
 */

#include "config.h"

#include <stdlib.h>

#include "cache/cache_varnishd.h"
#include "cache/cache_objhead.h"

#include "storage/storage.h"

struct lru {
	unsigned		magic;
#define LRU_MAGIC		0x3fec7bb0
	VTAILQ_HEAD(,objcore)	lru_head;
	struct lock		mtx;
};

static struct lru *
lru_get(const struct objcore *oc)
{
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(oc->stobj->stevedore, STEVEDORE_MAGIC);
	CHECK_OBJ_NOTNULL(oc->stobj->stevedore->lru, LRU_MAGIC);
	return (oc->stobj->stevedore->lru);
}

struct lru *
LRU_Alloc(void)
{
	struct lru *lru;

	ALLOC_OBJ(lru, LRU_MAGIC);
	AN(lru);
	VTAILQ_INIT(&lru->lru_head);
	Lck_New(&lru->mtx, lck_lru);
	return (lru);
}

void
LRU_Free(struct lru **pp)
{
	struct lru *lru;

	TAKE_OBJ_NOTNULL(lru, pp, LRU_MAGIC);
	Lck_Lock(&lru->mtx);
	AN(VTAILQ_EMPTY(&lru->lru_head));
	Lck_Unlock(&lru->mtx);
	Lck_Delete(&lru->mtx);
	FREE_OBJ(lru);
}

void
LRU_Add(struct objcore *oc, vtim_real now)
{
	struct lru *lru;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	if (oc->flags & OC_F_PRIVATE)
		return;

	AZ(oc->boc);
	AN(isnan(oc->last_lru));
	AZ(isnan(now));
	lru = lru_get(oc);
	CHECK_OBJ_NOTNULL(lru, LRU_MAGIC);
	Lck_Lock(&lru->mtx);
	VTAILQ_INSERT_TAIL(&lru->lru_head, oc, lru_list);
	oc->last_lru = now;
	AZ(isnan(oc->last_lru));
	Lck_Unlock(&lru->mtx);
}

void
LRU_Remove(struct objcore *oc)
{
	struct lru *lru;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	if (oc->flags & OC_F_PRIVATE)
		return;

	AZ(oc->boc);
	lru = lru_get(oc);
	CHECK_OBJ_NOTNULL(lru, LRU_MAGIC);
	Lck_Lock(&lru->mtx);
	AZ(isnan(oc->last_lru));
	VTAILQ_REMOVE(&lru->lru_head, oc, lru_list);
	oc->last_lru = NAN;
	Lck_Unlock(&lru->mtx);
}

void v_matchproto_(objtouch_f)
LRU_Touch(struct worker *wrk, struct objcore *oc, vtim_real now)
{
	struct lru *lru;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	if (oc->flags & OC_F_PRIVATE || isnan(oc->last_lru))
		return;

	/*
	 * To avoid the exphdl->mtx becoming a hotspot, we only
	 * attempt to move objects if they have not been moved
	 * recently and if the lock is available.  This optimization
	 * obviously leaves the LRU list imperfectly sorted.
	 */

	if (now - oc->last_lru < cache_param->lru_interval)
		return;

	lru = lru_get(oc);
	CHECK_OBJ_NOTNULL(lru, LRU_MAGIC);

	if (Lck_Trylock(&lru->mtx))
		return;

	if (!isnan(oc->last_lru)) {
		VTAILQ_REMOVE(&lru->lru_head, oc, lru_list);
		VTAILQ_INSERT_TAIL(&lru->lru_head, oc, lru_list);
		VSC_C_main->n_lru_moved++;
		oc->last_lru = now;
	}
	Lck_Unlock(&lru->mtx);
}

/*--------------------------------------------------------------------
 * Attempt to make space by nuking the oldest object on the LRU list
 * which isn't in use.
 * Returns: 1: did, 0: didn't;
 */

int
LRU_NukeOne(struct worker *wrk, struct lru *lru)
{
	struct objcore *oc, *oc2;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(lru, LRU_MAGIC);

	if (wrk->strangelove-- <= 0) {
		VSLb(wrk->vsl, SLT_ExpKill, "LRU reached nuke_limit");
		VSC_C_main->n_lru_limited++;
		return (0);
	}

	/* Find the first currently unused object on the LRU.  */
	Lck_Lock(&lru->mtx);
	VTAILQ_FOREACH_SAFE(oc, &lru->lru_head, lru_list, oc2) {
		CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
		AZ(isnan(oc->last_lru));

		VSLb(wrk->vsl, SLT_ExpKill, "LRU_Cand p=%p f=0x%x r=%d",
		    oc, oc->flags, oc->refcnt);

		if (HSH_Snipe(wrk, oc)) {
			VSC_C_main->n_lru_nuked++; // XXX per lru ?
			VTAILQ_REMOVE(&lru->lru_head, oc, lru_list);
			VTAILQ_INSERT_TAIL(&lru->lru_head, oc, lru_list);
			break;
		}
	}
	Lck_Unlock(&lru->mtx);

	if (oc == NULL) {
		VSLb(wrk->vsl, SLT_ExpKill, "LRU_Fail");
		return (0);
	}

	/* XXX: We could grab and return one storage segment to our caller */
	ObjSlim(wrk, oc);

	VSLb(wrk->vsl, SLT_ExpKill, "LRU xid=%ju", VXID(ObjGetXID(wrk, oc)));
	(void)HSH_DerefObjCore(wrk, &oc);	// Ref from HSH_Snipe
	return (1);
}
