/*-
 * Copyright (c) 2008-2011 Varnish Software AS
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
 * Persistent storage method
 *
 * XXX: Before we start the client or maybe after it stops, we should give the
 * XXX: stevedores a chance to examine their storage for consistency.
 *
 */

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache/cache.h"
#include "storage/storage.h"

#include "hash/hash_slinger.h"
#include "vsha256.h"
#include "vtim.h"

#include "persistent.h"
#include "storage/storage_persistent.h"

/*--------------------------------------------------------------------
 * Write the segmentlist back to the silo.
 *
 * We write the first copy, sync it synchronously, then write the
 * second copy and sync it synchronously.
 *
 * Provided the kernel doesn't lie, that means we will always have
 * at least one valid copy on in the silo.
 */

static void
smp_save_seg(const struct smp_sc *sc, struct smp_signspace *spc)
{
	struct smp_segptr *ss;
	struct smp_seg *sg;
	uint64_t length;

	Lck_AssertHeld(&sc->mtx);
	smp_reset_signspace(spc);
	ss = SIGNSPACE_DATA(spc);
	length = 0;
	VTAILQ_FOREACH(sg, &sc->segments, list) {
		assert(sg->p.offset < sc->mediasize);
		assert(sg->p.offset + sg->p.length <= sc->mediasize);
		*ss = sg->p;
		ss++;
		length += sizeof *ss;
	}
	smp_append_signspace(spc, length);
	smp_sync_sign(&spc->ctx);
}

void
smp_save_segs(struct smp_sc *sc)
{
	struct smp_seg *sg, *sg2;

	Lck_AssertHeld(&sc->mtx);

	/*
	 * Remove empty segments from the front of the list
	 * before we write the segments to disk.
	 */
	VTAILQ_FOREACH_SAFE(sg, &sc->segments, list, sg2) {
		if (sg->nobj > 0)
			break;
		if (sg == sc->cur_seg)
			continue;
		VTAILQ_REMOVE(&sc->segments, sg, list);
		LRU_Free(sg->lru);
		FREE_OBJ(sg);
	}
	smp_save_seg(sc, &sc->seg1);
	smp_save_seg(sc, &sc->seg2);
}

/*--------------------------------------------------------------------
 * Load segments
 *
 * The overall objective is to register the existence of an object, based
 * only on the minimally sized struct smp_object, without causing the
 * main object to be faulted in.
 *
 * XXX: We can test this by mprotecting the main body of the segment
 * XXX: until the first fixup happens, or even just over this loop,
 * XXX: However: the requires that the smp_objects starter further
 * XXX: into the segment than a page so that they do not get hit
 * XXX: by the protection.
 */

void
smp_load_seg(struct worker *wrk, const struct smp_sc *sc,
    struct smp_seg *sg)
{
	struct smp_object *so;
	struct objcore *oc;
	uint32_t no;
	double t_now = VTIM_real();
	struct smp_signctx ctx[1];

	ASSERT_SILO_THREAD(sc);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(sg, SMP_SEG_MAGIC);
	CHECK_OBJ_NOTNULL(sg->lru, LRU_MAGIC);
	assert(sg->flags & SMP_SEG_MUSTLOAD);
	sg->flags &= ~SMP_SEG_MUSTLOAD;
	AN(sg->p.offset);
	if (sg->p.objlist == 0)
		return;
	smp_def_sign(sc, ctx, sg->p.offset, "SEGHEAD");
	if (smp_chk_sign(ctx))
		return;

	/* test SEGTAIL */
	/* test OBJIDX */
	so = (void*)(sc->base + sg->p.objlist);
	sg->objs = so;
	no = sg->p.lobjlist;
	/* Clear the bogus "hold" count */
	sg->nobj = 0;
	for (;no > 0; so++,no--) {
		if (so->ttl == 0 || so->ttl < t_now)
			continue;
		ALLOC_OBJ(oc, OBJCORE_MAGIC);
		AN(oc);
		oc->flags |= OC_F_NEEDFIXUP;
		oc->flags &= ~OC_F_BUSY;
		smp_init_oc(oc, sg, no);
		oc->ban = BAN_RefBan(oc, so->ban, sc->tailban);
		HSH_Insert(wrk, so->hash, oc);
		EXP_Inject(oc, sg->lru, so->ttl);
		sg->nobj++;
	}
	WRK_SumStat(wrk);
	sg->flags |= SMP_SEG_LOADED;
}

/*--------------------------------------------------------------------
 * Create a new segment
 */

void
smp_new_seg(struct smp_sc *sc)
{
	struct smp_seg tmpsg;
	struct smp_seg *sg;

	AZ(sc->cur_seg);
	Lck_AssertHeld(&sc->mtx);

	/* XXX: find where it goes in silo */

	memset(&tmpsg, 0, sizeof tmpsg);
	tmpsg.magic = SMP_SEG_MAGIC;
	tmpsg.sc = sc;
	tmpsg.p.offset = sc->free_offset;
	/* XXX: align */
	assert(tmpsg.p.offset >= sc->ident->stuff[SMP_SPC_STUFF]);
	assert(tmpsg.p.offset < sc->mediasize);

	tmpsg.p.length = sc->aim_segl;
	tmpsg.p.length = RDN2(tmpsg.p.length, 8);

	if (smp_segend(&tmpsg) > sc->mediasize)
		/* XXX: Consider truncation in this case */
		tmpsg.p.offset = sc->ident->stuff[SMP_SPC_STUFF];

	assert(smp_segend(&tmpsg) <= sc->mediasize);

	sg = VTAILQ_FIRST(&sc->segments);
	if (sg != NULL && tmpsg.p.offset <= sg->p.offset) {
		if (smp_segend(&tmpsg) > sg->p.offset)
			/* No more space, return (cur_seg will be NULL) */
			/* XXX: Consider truncation instead of failing */
			return;
		assert(smp_segend(&tmpsg) <= sg->p.offset);
	}

	if (tmpsg.p.offset == sc->ident->stuff[SMP_SPC_STUFF])
		printf("Wrapped silo\n");

	ALLOC_OBJ(sg, SMP_SEG_MAGIC);
	if (sg == NULL)
		/* Failed allocation */
		return;
	*sg = tmpsg;
	sg->lru = LRU_Alloc();
	CHECK_OBJ_NOTNULL(sg->lru, LRU_MAGIC);
	sg->lru->flags |= LRU_F_DONTMOVE;

	sg->p.offset = IRNUP(sc, sg->p.offset);
	sg->p.length -= sg->p.offset - tmpsg.p.offset;
	sg->p.length = IRNDN(sc, sg->p.length);
	assert(sg->p.offset + sg->p.length <= tmpsg.p.offset + tmpsg.p.length);
	sc->free_offset = sg->p.offset + sg->p.length;

	VTAILQ_INSERT_TAIL(&sc->segments, sg, list);

	/* Neuter the new segment in case there is an old one there */
	AN(sg->p.offset);
	smp_def_sign(sc, sg->ctx, sg->p.offset, "SEGHEAD");
	smp_reset_sign(sg->ctx);
	smp_sync_sign(sg->ctx);

	/* Set up our allocation points */
	sc->cur_seg = sg;
	sc->next_bot = sg->p.offset + IRNUP(sc, SMP_SIGN_SPACE);
	sc->next_top = smp_segend(sg);
	sc->next_top -= IRNUP(sc, SMP_SIGN_SPACE);
	IASSERTALIGN(sc, sc->next_bot);
	IASSERTALIGN(sc, sc->next_top);
	sg->objs = (void*)(sc->base + sc->next_top);
}

/*--------------------------------------------------------------------
 * Close a segment
 */

void
smp_close_seg(struct smp_sc *sc, struct smp_seg *sg)
{
	uint64_t left, dst, len;
	void *dp;

	Lck_AssertHeld(&sc->mtx);

	CHECK_OBJ_NOTNULL(sg, SMP_SEG_MAGIC);
	assert(sg == sc->cur_seg);
	AN(sg->p.offset);
	sc->cur_seg = NULL;

	if (sg->nalloc == 0) {
		/* If segment is empty, delete instead */
		VTAILQ_REMOVE(&sc->segments, sg, list);
		assert(sg->p.offset >= sc->ident->stuff[SMP_SPC_STUFF]);
		assert(sg->p.offset < sc->mediasize);
		sc->free_offset = sg->p.offset;
		LRU_Free(sg->lru);
		FREE_OBJ(sg);
		return;
	}

	/*
	 * If there is enough space left, that we can move the smp_objects
	 * down without overwriting the present copy, we will do so to
	 * compact the segment.
	 */
	left = smp_spaceleft(sc, sg);
	len = sizeof(struct smp_object) * sg->p.lobjlist;
	if (len < left) {
		dst = sc->next_bot + IRNUP(sc, SMP_SIGN_SPACE);
		dp = sc->base + dst;
		assert((uintptr_t)dp + len < (uintptr_t)sg->objs);
		memcpy(dp, sg->objs, len);
		sc->next_top = dst;
		sg->objs = dp;
		sg->p.length = (sc->next_top - sg->p.offset)
		     + len + IRNUP(sc, SMP_SIGN_SPACE);
		(void)smp_spaceleft(sc, sg);	/* for the asserts */

	}

	/* Update the segment header */
	sg->p.objlist = sc->next_top;

	/* Write the (empty) OBJIDX signature */
	sc->next_top -= IRNUP(sc, SMP_SIGN_SPACE);
	assert(sc->next_top >= sc->next_bot);
	smp_def_sign(sc, sg->ctx, sc->next_top, "OBJIDX");
	smp_reset_sign(sg->ctx);
	smp_sync_sign(sg->ctx);

	/* Write the (empty) SEGTAIL signature */
	smp_def_sign(sc, sg->ctx,
	    sg->p.offset + sg->p.length - IRNUP(sc, SMP_SIGN_SPACE), "SEGTAIL");
	smp_reset_sign(sg->ctx);
	smp_sync_sign(sg->ctx);

	/* Save segment list */
	smp_save_segs(sc);
	sc->free_offset = smp_segend(sg);
}


/*---------------------------------------------------------------------
 */

static struct smp_object *
smp_find_so(const struct smp_seg *sg, unsigned priv2)
{
	struct smp_object *so;

	assert(priv2 > 0);
	assert(priv2 <= sg->p.lobjlist);
	so = &sg->objs[sg->p.lobjlist - priv2];
	return (so);
}

/*---------------------------------------------------------------------
 * Check if a given storage structure is valid to use
 */

static int
smp_loaded_st(const struct smp_sc *sc, const struct smp_seg *sg,
    const struct storage *st)
{
	struct smp_seg *sg2;
	const uint8_t *pst;
	uint64_t o;

	(void)sg;		/* XXX: faster: Start search from here */
	pst = (const void *)st;

	if (pst < (sc->base + sc->ident->stuff[SMP_SPC_STUFF]))
		return (0x01);		/* Before silo payload start */
	if (pst > (sc->base + sc->ident->stuff[SMP_END_STUFF]))
		return (0x02);		/* After silo end */

	o = pst - sc->base;

	/* Find which segment contains the storage structure */
	VTAILQ_FOREACH(sg2, &sc->segments, list)
		if (o > sg2->p.offset && (o + sizeof(*st)) < sg2->p.objlist)
			break;
	if (sg2 == NULL)
		return (0x04);		/* No claiming segment */
	if (!(sg2->flags & SMP_SEG_LOADED))
		return (0x08);		/* Claiming segment not loaded */

	/* It is now safe to access the storage structure */
	if (st->magic != STORAGE_MAGIC)
		return (0x10);		/* Not enough magic */

	if (o + st->space >= sg2->p.objlist)
		return (0x20);		/* Allocation not inside segment */

	if (st->len > st->space)
		return (0x40);		/* Plain bad... */

	/*
	 * XXX: We could patch up st->stevedore and st->priv here
	 * XXX: but if things go right, we will never need them.
	 */
	return (0);
}

/*---------------------------------------------------------------------
 * objcore methods for persistent objects
 */

static unsigned __match_proto__(getxid_f)
smp_oc_getxid(struct dstat *ds, struct objcore *oc)
{
	struct object *o;
	struct smp_seg *sg;
	struct smp_object *so;

	(void)ds;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	CAST_OBJ_NOTNULL(sg, oc->priv, SMP_SEG_MAGIC);
	so = smp_find_so(sg, oc->priv2);

	o = (void*)(sg->sc->base + so->ptr);
	/*
	 * The object may not be in this segment since we allocate it
	 * In a separate operation than the smp_object.  We could check
	 * that it is in a later segment, but that would be complicated.
	 * XXX: For now, be happy if it is inside th silo
	 */
	ASSERT_PTR_IN_SILO(sg->sc, o);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	return (o->vxid);
}

/*---------------------------------------------------------------------
 * objcore methods for persistent objects
 */

static struct object *
smp_oc_getobj(struct dstat *ds, struct objcore *oc)
{
	struct object *o;
	struct smp_seg *sg;
	struct smp_object *so;
	struct storage *st;
	uint64_t l;
	int bad;

	/* Some calls are direct, but they should match anyway */
	assert(oc->methods->getobj == smp_oc_getobj);

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	if (ds == NULL)
		AZ(oc->flags & OC_F_NEEDFIXUP);

	CAST_OBJ_NOTNULL(sg, oc->priv, SMP_SEG_MAGIC);
	so = smp_find_so(sg, oc->priv2);

	o = (void*)(sg->sc->base + so->ptr);
	/*
	 * The object may not be in this segment since we allocate it
	 * In a separate operation than the smp_object.  We could check
	 * that it is in a later segment, but that would be complicated.
	 * XXX: For now, be happy if it is inside th silo
	 */
	ASSERT_PTR_IN_SILO(sg->sc, o);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);

	/*
	 * If this flag is not set, it will not be, and the lock is not
	 * needed to test it.
	 */
	if (!(oc->flags & OC_F_NEEDFIXUP))
		return (o);

	AN(ds);
	Lck_Lock(&sg->sc->mtx);
	/* Check again, we might have raced. */
	if (oc->flags & OC_F_NEEDFIXUP) {
		/* We trust caller to have a refcnt for us */
		o->objcore = oc;

		bad = 0;
		l = 0;
		VTAILQ_FOREACH(st, &o->store, list) {
			bad |= smp_loaded_st(sg->sc, sg, st);
			if (bad)
				break;
			l += st->len;
		}
		if (l != o->len)
			bad |= 0x100;

		if(bad) {
			o->exp.ttl = -1;
			so->ttl = 0;
		}

		sg->nfixed++;
		ds->n_object++;
		ds->n_vampireobject--;
		oc->flags &= ~OC_F_NEEDFIXUP;
	}
	Lck_Unlock(&sg->sc->mtx);
	EXP_Rearm(o, NAN, NAN, NAN, NAN);	// XXX: Shouldn't be needed
	return (o);
}

static void
smp_oc_updatemeta(struct objcore *oc)
{
	struct object *o;
	struct smp_seg *sg;
	struct smp_object *so;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	o = smp_oc_getobj(NULL, oc);
	AN(o);

	CAST_OBJ_NOTNULL(sg, oc->priv, SMP_SEG_MAGIC);
	CHECK_OBJ_NOTNULL(sg->sc, SMP_SC_MAGIC);
	so = smp_find_so(sg, oc->priv2);

	if (sg == sg->sc->cur_seg) {
		/* Lock necessary, we might race close_seg */
		Lck_Lock(&sg->sc->mtx);
		so->ban = BAN_Time(oc->ban);
		so->ttl = oc->timer_when;
		Lck_Unlock(&sg->sc->mtx);
	} else {
		so->ban = BAN_Time(oc->ban);
		so->ttl = oc->timer_when;
	}
}

static void __match_proto__()
smp_oc_freeobj(struct objcore *oc)
{
	struct smp_seg *sg;
	struct smp_object *so;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	CAST_OBJ_NOTNULL(sg, oc->priv, SMP_SEG_MAGIC);
	so = smp_find_so(sg, oc->priv2);

	Lck_Lock(&sg->sc->mtx);
	so->ttl = 0;
	so->ptr = 0;

	assert(sg->nobj > 0);
	assert(sg->nfixed > 0);
	sg->nobj--;
	sg->nfixed--;

	Lck_Unlock(&sg->sc->mtx);
}

/*--------------------------------------------------------------------
 * Find the per-segment lru list for this object
 */

static struct lru *
smp_oc_getlru(const struct objcore *oc)
{
	struct smp_seg *sg;

	CAST_OBJ_NOTNULL(sg, oc->priv, SMP_SEG_MAGIC);
	return (sg->lru);
}

static struct objcore_methods smp_oc_methods = {
	.getxid =		smp_oc_getxid,
	.getobj =		smp_oc_getobj,
	.updatemeta =		smp_oc_updatemeta,
	.freeobj =		smp_oc_freeobj,
	.getlru =		smp_oc_getlru,
};

/*--------------------------------------------------------------------*/

void
smp_init_oc(struct objcore *oc, struct smp_seg *sg, unsigned objidx)
{

	oc->priv = sg;
	oc->priv2 = objidx;
	oc->methods = &smp_oc_methods;
}
