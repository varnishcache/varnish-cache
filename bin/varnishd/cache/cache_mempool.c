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

#include <stddef.h>
#include <stdlib.h>

#include "config.h"

#include "cache.h"

#include "vtim.h"

struct memitem {
	unsigned			magic;
#define MEMITEM_MAGIC			0x42e55401
	VTAILQ_ENTRY(memitem)		list;
	unsigned			size;
	double				payload;
};

struct mempool {
	unsigned			magic;
#define MEMPOOL_MAGIC			0x37a75a8d
	VTAILQ_HEAD(,memitem)		list;
	VTAILQ_HEAD(,memitem)		surplus;
	struct lock			*mtx;
	struct lock			imtx;
	const char			*name;
	volatile struct poolparam	*param;
	volatile unsigned		*cur_size;
	struct VSC_C_mempool		*vsc;
	unsigned			n_pool;
	pthread_t			thread;
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
	return (mi);
}

/*---------------------------------------------------------------------
 * Pool-guard
 *   Attempt to keep number of free items in pool inside bounds with
 *   minimum locking activity.
 */

static void *
mpl_guard(void *priv)
{
	struct mempool *mpl;
	struct memitem *mi = NULL;
	double mpl_slp __state_variable__(mpl_slp);

	CAST_OBJ_NOTNULL(mpl, priv, MEMPOOL_MAGIC);
	mpl_slp = 0.15;	// random
	while (1) {
		VTIM_sleep(mpl_slp);
		mpl_slp = 0.814;	// random

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
		} else {
			continue;	/* cannot do */
		}

		mpl_slp = 0.314;

		if (Lck_Trylock(mpl->mtx))
			continue;

		mpl_slp = .01;

		if (mpl->n_pool < mpl->param->min_pool &&
		    mi != NULL && mi->size >= *mpl->cur_size) {
			CHECK_OBJ_NOTNULL(mi, MEMITEM_MAGIC);
			mpl->vsc->pool++;
			mpl->n_pool++;
			VTAILQ_INSERT_HEAD(&mpl->list, mi, list);
			mi = NULL;
		}
		if (mpl->n_pool > mpl->param->max_pool && mi == NULL) {
			mi = VTAILQ_FIRST(&mpl->list);
			CHECK_OBJ_NOTNULL(mi, MEMITEM_MAGIC);
			mpl->vsc->pool--;
			mpl->n_pool--;
			VTAILQ_REMOVE(&mpl->list, mi, list);
		}
		if (mi == NULL) {
			mi = VTAILQ_FIRST(&mpl->surplus);
			if (mi != NULL) {
				CHECK_OBJ_NOTNULL(mi, MEMITEM_MAGIC);
				VTAILQ_REMOVE(&mpl->surplus, mi, list);
			}
		}
		Lck_Unlock(mpl->mtx);

		if (mi != NULL) {
			FREE_OBJ(mi);
			mi = NULL;
		}
	}
}

/*---------------------------------------------------------------------
 */

struct mempool *
MPL_New(const char *name,
	struct lock *mtx,
	volatile struct poolparam *pp,
	volatile unsigned *cur_size)
{
	struct mempool *mpl;

	ALLOC_OBJ(mpl, MEMPOOL_MAGIC);
	AN(mpl);
	mpl->name = name;
	mpl->param = pp;
	mpl->cur_size = cur_size;
	mpl->mtx = mtx;
	VTAILQ_INIT(&mpl->list);
	VTAILQ_INIT(&mpl->surplus);
	Lck_New(&mpl->imtx, lck_mempool);
	if (mpl->mtx == NULL)
		mpl->mtx = &mpl->imtx;
	/* XXX: prealloc min_pool */
	mpl->vsc = VSM_Alloc(sizeof *mpl->vsc,
	    VSC_CLASS, VSC_TYPE_MEMPOOL, name);
	AN(mpl->vsc);
	AZ(pthread_create(&mpl->thread, NULL, mpl_guard, mpl));
	return (mpl);
}

void *
MPL_Get(struct mempool *mpl, unsigned *size)
{
	struct memitem *mi;

	CHECK_OBJ_NOTNULL(mpl, MEMPOOL_MAGIC);

	Lck_Lock(mpl->mtx);

	mpl->vsc->allocs++;
	mpl->vsc->live++;

	do {
		mi = VTAILQ_FIRST(&mpl->list);
		if (mi == NULL)
			break;
		mpl->vsc->pool--;
		mpl->n_pool--;
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

	Lck_Unlock(mpl->mtx);

	if (mi == NULL)
		mi = mpl_alloc(mpl);
	if (size != NULL)
		*size = mi->size;

	/* Throw away sizeof info for FlexeLint: */
	return ((void*)(uintptr_t)&mi->payload);
}

void
MPL_Free(struct mempool *mpl, void *item)
{
	struct memitem *mi;

	CHECK_OBJ_NOTNULL(mpl, MEMPOOL_MAGIC);

	mi = (void*)((uintptr_t)item - offsetof(struct memitem, payload));
	CHECK_OBJ_NOTNULL(mi, MEMITEM_MAGIC);
	memset(item, 0, mi->size);

	Lck_Lock(mpl->mtx);

	mpl->vsc->frees++;
	mpl->vsc->live--;

	if (mi->size < *mpl->cur_size) {
		mpl->vsc->toosmall++;
		VTAILQ_INSERT_HEAD(&mpl->surplus, mi, list);
	} else if (mpl->n_pool >= mpl->param->max_pool) {
		mpl->vsc->surplus++;
		VTAILQ_INSERT_HEAD(&mpl->surplus, mi, list);
	} else {
		mpl->vsc->pool++;
		mpl->n_pool++;
		VTAILQ_INSERT_HEAD(&mpl->list, mi, list);
	}

	Lck_Unlock(mpl->mtx);
}
