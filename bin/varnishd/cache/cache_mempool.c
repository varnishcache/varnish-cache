/*-
 * Copyright (c) 2011 Varnish Software AS
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
 * Generic memory pool
 */

#include "config.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>


#include "cache.h"

#include "vtim.h"

struct memitem {
	unsigned			magic;
#define MEMITEM_MAGIC			0x42e55401
	unsigned			size;
	VTAILQ_ENTRY(memitem)		list;
	double				touched;
};

VTAILQ_HEAD(memhead_s, memitem);

struct mempool {
	unsigned			magic;
#define MEMPOOL_MAGIC			0x37a75a8d
	char				name[12];
	struct memhead_s		list;
	struct memhead_s		surplus;
	struct lock			mtx;
	volatile struct poolparam	*param;
	volatile unsigned		*cur_size;
	uint64_t			live;
	struct VSC_C_mempool		*vsc;
	unsigned			n_pool;
	pthread_t			thread;
	double				t_now;
	int				self_destruct;
};

/*---------------------------------------------------------------------
 */

static struct memitem *
mpl_alloc(const struct mempool *mpl)
{
	unsigned tsz;
	struct memitem *mi;

	CHECK_OBJ_NOTNULL(mpl, MEMPOOL_MAGIC);
	tsz = *mpl->cur_size;
	mi = calloc(sizeof *mi + tsz, 1);
	AN(mi);
	mi->magic = MEMITEM_MAGIC;
	mi->size = tsz;
	mpl->vsc->sz_wanted = tsz;
	mpl->vsc->sz_needed = tsz + sizeof *mi;
	return (mi);
}

/*---------------------------------------------------------------------
 * Pool-guard
 *   Attempt to keep number of free items in pool inside bounds with
 *   minimum locking activity, and keep an eye on items at the tail
 *   of the list not getting too old.
 */

static void *
mpl_guard(void *priv)
{
	struct mempool *mpl;
	struct memitem *mi = NULL;
	double mpl_slp __state_variable__(mpl_slp);
	double last = 0;

	CAST_OBJ_NOTNULL(mpl, priv, MEMPOOL_MAGIC);
	THR_SetName(mpl->name);
	mpl_slp = 0.15;	// random
	while (1) {
		VTIM_sleep(mpl_slp);
		mpl_slp = 0.814;	// random
		mpl->t_now = VTIM_real();

		if (mi != NULL && (mpl->n_pool > mpl->param->max_pool ||
		    mi->size < *mpl->cur_size)) {
			FREE_OBJ(mi);
			mi = NULL;
		}

		if (mi == NULL && mpl->n_pool < mpl->param->min_pool)
			mi = mpl_alloc(mpl);


		if (mpl->n_pool < mpl->param->min_pool && mi != NULL) {
			/* can do */
		} else if (mpl->n_pool > mpl->param->max_pool && mi == NULL) {
			/* can do */
		} else if (!VTAILQ_EMPTY(&mpl->surplus)) {
			/* can do */
		} else if (last + .1 * mpl->param->max_age < mpl->t_now) {
			/* should do */
		} else if (mpl->self_destruct) {
			/* can do */
		} else {
			continue;	/* nothing to do */
		}

		mpl_slp = 0.314;	// random

		if (Lck_Trylock(&mpl->mtx))
			continue;

		if (mpl->self_destruct) {
			AZ(mpl->live);
			while (1) {
				if (mi == NULL) {
					mi = VTAILQ_FIRST(&mpl->list);
					if (mi != NULL) {
						mpl->vsc->pool = --mpl->n_pool;
						VTAILQ_REMOVE(&mpl->list,
						    mi, list);
					}
				}
				if (mi == NULL) {
					mi = VTAILQ_FIRST(&mpl->surplus);
					if (mi != NULL)
						VTAILQ_REMOVE(&mpl->surplus,
						    mi, list);
				}
				if (mi == NULL)
					break;
				FREE_OBJ(mi);
				mi = NULL;
			}
			VSM_Free(mpl->vsc);
			Lck_Unlock(&mpl->mtx);
			Lck_Delete(&mpl->mtx);
			FREE_OBJ(mpl);
			break;
		}

		if (mpl->n_pool < mpl->param->min_pool &&
		    mi != NULL && mi->size >= *mpl->cur_size) {
			CHECK_OBJ_NOTNULL(mi, MEMITEM_MAGIC);
			mpl->vsc->pool = ++mpl->n_pool;
			mi->touched = mpl->t_now;
			VTAILQ_INSERT_HEAD(&mpl->list, mi, list);
			mi = NULL;
			mpl_slp = .01;	// random

		}
		if (mpl->n_pool > mpl->param->max_pool && mi == NULL) {
			mi = VTAILQ_FIRST(&mpl->list);
			CHECK_OBJ_NOTNULL(mi, MEMITEM_MAGIC);
			mpl->vsc->pool = --mpl->n_pool;
			mpl->vsc->surplus++;
			VTAILQ_REMOVE(&mpl->list, mi, list);
			mpl_slp = .01;	// random
		}
		if (mi == NULL) {
			mi = VTAILQ_FIRST(&mpl->surplus);
			if (mi != NULL) {
				CHECK_OBJ_NOTNULL(mi, MEMITEM_MAGIC);
				VTAILQ_REMOVE(&mpl->surplus, mi, list);
				mpl_slp = .01;	// random
			}
		}
		if (mi == NULL && mpl->n_pool > mpl->param->min_pool) {
			mi = VTAILQ_LAST(&mpl->list, memhead_s);
			CHECK_OBJ_NOTNULL(mi, MEMITEM_MAGIC);
			if (mi->touched + mpl->param->max_age < mpl->t_now) {
				mpl->vsc->pool = --mpl->n_pool;
				mpl->vsc->timeout++;
				VTAILQ_REMOVE(&mpl->list, mi, list);
				mpl_slp = .01;	// random
			} else {
				mi = NULL;
				last = mpl->t_now;
			}
		} else if (mpl->n_pool <= mpl->param->min_pool) {
			last = mpl->t_now;
		}

		Lck_Unlock(&mpl->mtx);

		if (mi != NULL) {
			FREE_OBJ(mi);
			mi = NULL;
		}
	}
	return (NULL);
}

/*---------------------------------------------------------------------
 * Create a new memory pool, and start the guard thread for it.
 */

struct mempool *
MPL_New(const char *name,
    volatile struct poolparam *pp, volatile unsigned *cur_size)
{
	struct mempool *mpl;

	ALLOC_OBJ(mpl, MEMPOOL_MAGIC);
	AN(mpl);
	bprintf(mpl->name, "MPL_%s", name);
	mpl->param = pp;
	mpl->cur_size = cur_size;
	VTAILQ_INIT(&mpl->list);
	VTAILQ_INIT(&mpl->surplus);
	Lck_New(&mpl->mtx, lck_mempool);
	/* XXX: prealloc min_pool */
	mpl->vsc = VSM_Alloc(sizeof *mpl->vsc,
	    VSC_CLASS, VSC_type_mempool, mpl->name + 4);
	AN(mpl->vsc);
	AZ(pthread_create(&mpl->thread, NULL, mpl_guard, mpl));
	AZ(pthread_detach(mpl->thread));
	return (mpl);
}

/*---------------------------------------------------------------------
 * Destroy a memory pool.  There must be no live items, and we cheat
 * and leave all the hard work to the guard thread.
 */

void
MPL_Destroy(struct mempool **mpp)
{
	struct mempool *mpl;

	AN(mpp);
	mpl = *mpp;
	*mpp = NULL;
	CHECK_OBJ_NOTNULL(mpl, MEMPOOL_MAGIC);
	Lck_Lock(&mpl->mtx);
	AZ(mpl->live);
	mpl->self_destruct = 1;
	Lck_Unlock(&mpl->mtx);
}

/*---------------------------------------------------------------------
 */

void *
MPL_Get(struct mempool *mpl, unsigned *size)
{
	struct memitem *mi;

	CHECK_OBJ_NOTNULL(mpl, MEMPOOL_MAGIC);

	Lck_Lock(&mpl->mtx);

	mpl->vsc->allocs++;
	mpl->vsc->live = ++mpl->live;

	do {
		mi = VTAILQ_FIRST(&mpl->list);
		if (mi == NULL) {
			mpl->vsc->randry++;
			break;
		}
		mpl->vsc->pool = --mpl->n_pool;
		CHECK_OBJ_NOTNULL(mi, MEMITEM_MAGIC);
		VTAILQ_REMOVE(&mpl->list, mi, list);
		if (mi->size < *mpl->cur_size) {
			mpl->vsc->toosmall++;
			VTAILQ_INSERT_HEAD(&mpl->surplus, mi, list);
			mi = NULL;
		} else {
			mpl->vsc->recycle++;
		}
	} while (mi == NULL);

	Lck_Unlock(&mpl->mtx);

	if (mi == NULL)
		mi = mpl_alloc(mpl);
	if (size != NULL)
		*size = mi->size;

	CHECK_OBJ_NOTNULL(mi, MEMITEM_MAGIC);
	/* Throw away sizeof info for FlexeLint: */
	return ((void*)(uintptr_t)(mi+1));
}

void
MPL_Free(struct mempool *mpl, void *item)
{
	struct memitem *mi;

	CHECK_OBJ_NOTNULL(mpl, MEMPOOL_MAGIC);
	AN(item);

	mi = (void*)((uintptr_t)item - sizeof(*mi));
	CHECK_OBJ_NOTNULL(mi, MEMITEM_MAGIC);
	memset(item, 0, mi->size);

	Lck_Lock(&mpl->mtx);

	mpl->vsc->frees++;
	mpl->vsc->live = --mpl->live;

	if (mi->size < *mpl->cur_size) {
		mpl->vsc->toosmall++;
		VTAILQ_INSERT_HEAD(&mpl->surplus, mi, list);
	} else {
		mpl->vsc->pool = ++mpl->n_pool;
		mi->touched = mpl->t_now;
		VTAILQ_INSERT_HEAD(&mpl->list, mi, list);
	}

	Lck_Unlock(&mpl->mtx);
}

void
MPL_AssertSane(void *item)
{
	struct memitem *mi;
	mi = (void*)((uintptr_t)item - sizeof(*mi));
	CHECK_OBJ_NOTNULL(mi, MEMITEM_MAGIC);
}
