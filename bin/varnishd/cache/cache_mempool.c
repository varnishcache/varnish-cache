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
	struct lock			*mtx;
	struct lock			imtx;
	const char			*name;
	volatile struct poolparam	*param;
	volatile unsigned		*cur_size;
	struct VSC_C_mempool		*vsc;
};

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
	Lck_New(&mpl->imtx, lck_mempool);
	if (mpl->mtx == NULL)
		mpl->mtx = &mpl->imtx;
	/* XXX: prealloc min_pool */
	mpl->vsc = VSM_Alloc(sizeof *mpl->vsc,
	    VSC_CLASS, VSC_TYPE_MEMPOOL, name);
	AN(mpl->vsc);
	return (mpl);
}

void *
MPL_GetLocked(struct mempool *mpl, unsigned *size)
{
	struct memitem *mi;
	unsigned tsz;

	CHECK_OBJ_NOTNULL(mpl, MEMPOOL_MAGIC);
	Lck_AssertHeld(mpl->mtx);

	mpl->vsc->allocs++;
	mpl->vsc->live++;
	do {
		mi = VTAILQ_FIRST(&mpl->list);
		if (mi == NULL)
			break;
		mpl->vsc->pool--;
		CHECK_OBJ_NOTNULL(mi, MEMITEM_MAGIC);
		VTAILQ_REMOVE(&mpl->list, mi, list);
		if (mi->size < *mpl->cur_size) {
			mpl->vsc->toosmall++;
			FREE_OBJ(mi);
			mi = NULL;
		} else {
			mpl->vsc->recycle++;
		}
	} while (mi == NULL);
	if (mi == NULL) {
		tsz = *mpl->cur_size;
		mi = calloc(sizeof *mi + tsz, 1);
		AN(mi);
		mi->magic = MEMITEM_MAGIC;
		mi->size = tsz;
	}
	if (size != NULL)
		*size = mi->size;
	return (&mi->payload);
}

void
MPL_FreeLocked(struct mempool *mpl, void *item)
{
	struct memitem *mi;

	CHECK_OBJ_NOTNULL(mpl, MEMPOOL_MAGIC);
	Lck_AssertHeld(mpl->mtx);

	mpl->vsc->frees++;
	mpl->vsc->live--;

	mi = (void*)((uintptr_t)item - offsetof(struct memitem, payload));
	CHECK_OBJ_NOTNULL(mi, MEMITEM_MAGIC);

	if (mi->size < *mpl->cur_size) {
		mpl->vsc->toosmall++;
		FREE_OBJ(mi);
	} else {
		mpl->vsc->pool++;
		memset(item, 0, mi->size);
		VTAILQ_INSERT_HEAD(&mpl->list, mi, list);
	}
}

void *
MPL_Get(struct mempool *mpl, unsigned *size)
{
	void *p;

	CHECK_OBJ_NOTNULL(mpl, MEMPOOL_MAGIC);
	Lck_Lock(mpl->mtx);
	p = MPL_GetLocked(mpl, size);
	Lck_Unlock(mpl->mtx);
	return (p);
}

void
MPL_Free(struct mempool *mpl, void *item)
{
	CHECK_OBJ_NOTNULL(mpl, MEMPOOL_MAGIC);
	Lck_Lock(mpl->mtx);
	MPL_FreeLocked(mpl, item);
	Lck_Unlock(mpl->mtx);
}
