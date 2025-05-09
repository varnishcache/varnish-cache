/*-
 * Copyright (c) 2013-2016 Varnish Software AS
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
 * Lifetime of an objcore:
 *	phase 0	- nonexistent
 *	phase 1	- created, but no stevedore associated
 *	phase 2	- stevedore associated, being filled out
 *	phase 3	- stable, no changes happening
 *	phase 4	- unavailable, being dismantled
 *	phase 5	- stevedore disassociated
 *	phase 6	- nonexistent
 *
 * 0->1	ObjNew()	creates objcore
 *
 * 1->2	STV_NewObject()	associates a stevedore
 *
 * 2	ObjSetState()	sets state
 * 2	ObjWaitState()	waits for particular state
 *			INVALID->REQ_DONE->STREAM->FINISHED->FAILED
 *
 * 2	ObjGetSpace()	allocates space
 * 2	ObjExtend()	commits content
 * 2	ObjWaitExtend()	waits for content - used to implement ObjIterate())
 *
 * 2	ObjSetAttr()
 * 2	  ObjCopyAttr()
 * 2	  ObjSetFlag()
 * 2	  ObjSetDouble()
 * 2	  ObjSetU32()
 * 2	  ObjSetU64()
 *
 * 2->3	ObjBocDone()	Boc removed from OC, clean it up
 *
 * 23	ObjHasAttr()
 * 23	ObjGetAttr()
 * 23	  ObjCheckFlag()
 * 23	  ObjGetDouble()
 * 23	  ObjGetU32()
 * 23	  ObjGetU64()
 * 23	  ObjGetLen()
 * 23	  ObjGetXID()
 *
 * 23	ObjIterate()	... over body
 *
 * 23	ObjTouch()	Signal to LRU(-like) facilities
 *
 * 3->4	HSH_Snipe()	kill if not in use
 * 3->4	HSH_Kill()	make unavailable
 *
 * 234	ObjSlim()	Release body storage (but retain attribute storage)
 *
 * 4->5	ObjFreeObj()	disassociates stevedore
 *
 * 5->6 FREE_OBJ()	...in HSH_DerefObjCore()
 */

#include "config.h"

#include <stdlib.h>

#include "cache_varnishd.h"
#include "cache_obj.h"
#include "cache_objhead.h"
#include "vend.h"
#include "storage/storage.h"

static const struct obj_methods *
obj_getmethods(const struct objcore *oc)
{

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(oc->stobj->stevedore, STEVEDORE_MAGIC);
	AN(oc->stobj->stevedore->methods);
	return (oc->stobj->stevedore->methods);
}

static struct boc *
obj_newboc(void)
{
	struct boc *boc;

	ALLOC_OBJ(boc, BOC_MAGIC);
	AN(boc);
	Lck_New(&boc->mtx, lck_busyobj);
	PTOK(pthread_cond_init(&boc->cond, NULL));
	boc->refcount = 1;
	boc->transit_buffer = cache_param->transit_buffer;
	return (boc);
}

static void
obj_deleteboc(struct boc **p)
{
	struct boc *boc;

	TAKE_OBJ_NOTNULL(boc, p, BOC_MAGIC);
	Lck_Delete(&boc->mtx);
	PTOK(pthread_cond_destroy(&boc->cond));
	free(boc->vary);
	FREE_OBJ(boc);
}

/*====================================================================
 * ObjNew()
 *
 */

struct objcore *
ObjNew(const struct worker *wrk)
{
	struct objcore *oc;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	ALLOC_OBJ(oc, OBJCORE_MAGIC);
	AN(oc);
	wrk->stats->n_objectcore++;
	oc->last_lru = NAN;
	oc->boc = obj_newboc();

	return (oc);
}

/*====================================================================
 * ObjDestroy()
 *
 */

void
ObjDestroy(const struct worker *wrk, struct objcore **p)
{
	struct objcore *oc;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	TAKE_OBJ_NOTNULL(oc, p, OBJCORE_MAGIC);
	if (oc->boc != NULL)
		obj_deleteboc(&oc->boc);
	FREE_OBJ(oc);
	wrk->stats->n_objectcore--;
}

/*====================================================================
 * ObjIterate()
 *
 */

int
ObjIterate(struct worker *wrk, struct objcore *oc,
    void *priv, objiterate_f *func, int final)
{
	const struct obj_methods *om = obj_getmethods(oc);

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AN(func);
	AN(om->objiterator);
	return (om->objiterator(wrk, oc, priv, func, final));
}

/*====================================================================
 * ObjGetSpace()
 *
 * This function returns a pointer and length of free space.  If there
 * is no free space, some will be added first.
 *
 * The "sz" argument is an input hint of how much space is desired.
 * 0 means "unknown", return some default size (maybe fetch_chunksize)
 */

int
ObjGetSpace(struct worker *wrk, struct objcore *oc, ssize_t *sz, uint8_t **ptr)
{
	const struct obj_methods *om = obj_getmethods(oc);

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc->boc, BOC_MAGIC);
	AN(sz);
	AN(ptr);
	assert(*sz >= 0);

	AN(om->objgetspace);
	return (om->objgetspace(wrk, oc, sz, ptr));
}

/*====================================================================
 * ObjExtend()
 *
 * This function extends the used part of the object a number of bytes
 * into the last space returned by ObjGetSpace()
 *
 * The final flag must be set on the last call, and it will release any
 * surplus space allocated.
 */

static void
obj_extend_condwait(const struct objcore *oc)
{

	if (oc->boc->transit_buffer == 0)
		return;

	assert(oc->flags & OC_F_TRANSIENT);
	while (!(oc->flags & OC_F_CANCEL) && oc->boc->fetched_so_far >
	    oc->boc->delivered_so_far + oc->boc->transit_buffer)
		(void)Lck_CondWait(&oc->boc->cond, &oc->boc->mtx);
}

void
ObjExtend(struct worker *wrk, struct objcore *oc, ssize_t l, int final)
{
	const struct obj_methods *om = obj_getmethods(oc);

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc->boc, BOC_MAGIC);
	AN(om->objextend);
	assert(l >= 0);

	Lck_Lock(&oc->boc->mtx);
	if (l > 0) {
		obj_extend_condwait(oc);
		om->objextend(wrk, oc, l);
		oc->boc->fetched_so_far += l;
		PTOK(pthread_cond_broadcast(&oc->boc->cond));
	}
	Lck_Unlock(&oc->boc->mtx);

	assert(oc->boc->state < BOS_FINISHED);
	if (final && om->objtrimstore != NULL)
		om->objtrimstore(wrk, oc);
}

/*====================================================================
 */

uint64_t
ObjWaitExtend(const struct worker *wrk, const struct objcore *oc, uint64_t l,
    enum boc_state_e *statep)
{
	enum boc_state_e state;
	uint64_t rv;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(oc->boc, BOC_MAGIC);
	Lck_Lock(&oc->boc->mtx);
	while (1) {
		rv = oc->boc->fetched_so_far;
		assert(l <= rv || oc->boc->state == BOS_FAILED);
		if (oc->boc->transit_buffer > 0) {
			assert(oc->flags & OC_F_TRANSIENT);
			/* Signal the new client position */
			oc->boc->delivered_so_far = l;
			PTOK(pthread_cond_signal(&oc->boc->cond));
		}
		state = oc->boc->state;
		if (rv > l || state >= BOS_FINISHED)
			break;
		(void)Lck_CondWait(&oc->boc->cond, &oc->boc->mtx);
	}
	Lck_Unlock(&oc->boc->mtx);
	if (statep != NULL)
		*statep = state;
	return (rv);
}
/*====================================================================
 */

void
ObjSetState(struct worker *wrk, struct objcore *oc, enum boc_state_e next,
    unsigned broadcast)
{
	const struct obj_methods *om;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(oc->boc, BOC_MAGIC);
	assert(next > oc->boc->state);

	CHECK_OBJ_ORNULL(oc->stobj->stevedore, STEVEDORE_MAGIC);
	assert(next != BOS_FINISHED || (oc->oa_present & (1 << OA_LEN)));

	if (oc->stobj->stevedore != NULL) {
		om = oc->stobj->stevedore->methods;
		if (om->objsetstate != NULL)
			om->objsetstate(wrk, oc, next);
	}

	if (next == BOS_FAILED)
		HSH_Fail(wrk, oc);
	else if (oc->boc->state < BOS_STREAM && next >= BOS_STREAM)
		HSH_Unbusy(wrk, oc);

	Lck_Lock(&oc->boc->mtx);
	oc->boc->state = next;
	if (broadcast)
		PTOK(pthread_cond_broadcast(&oc->boc->cond));
	Lck_Unlock(&oc->boc->mtx);
}

/*====================================================================
 */

void
ObjWaitState(const struct objcore *oc, enum boc_state_e want)
{

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(oc->boc, BOC_MAGIC);

	Lck_Lock(&oc->boc->mtx);
	/* wake up obj_extend_condwait() */
	if (oc->flags & OC_F_CANCEL)
		PTOK(pthread_cond_signal(&oc->boc->cond));
	while (1) {
		if (oc->boc->state >= want)
			break;
		(void)Lck_CondWait(&oc->boc->cond, &oc->boc->mtx);
	}
	Lck_Unlock(&oc->boc->mtx);
}

/*====================================================================
 * ObjGetlen()
 *
 * This is a separate function because it may need locking
 */

uint64_t
ObjGetLen(struct worker *wrk, struct objcore *oc)
{
	uint64_t len;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	AZ(ObjGetU64(wrk, oc, OA_LEN, &len));
	return (len);
}

/*====================================================================
 * ObjSlim()
 *
 * Free the whatever storage can be freed, without freeing the actual
 * object yet.
 */

void
ObjSlim(struct worker *wrk, struct objcore *oc)
{
	const struct obj_methods *om = obj_getmethods(oc);

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	if (om->objslim != NULL)
		om->objslim(wrk, oc);
}

/*====================================================================
 * Called when the boc used to populate the objcore is going away.
 * Useful for releasing any leftovers from Trim.
 */

void
ObjBocDone(struct worker *wrk, struct objcore *oc, struct boc **boc)
{
	const struct obj_methods *m;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AN(boc);
	CHECK_OBJ_NOTNULL(*boc, BOC_MAGIC);
	CHECK_OBJ_ORNULL(oc->stobj->stevedore, STEVEDORE_MAGIC);
	if (oc->stobj->stevedore != NULL) {
		m = obj_getmethods(oc);
		if (m->objbocdone != NULL)
			m->objbocdone(wrk, oc, *boc);
	}
	obj_deleteboc(boc);
}

/*====================================================================
 */
void
ObjFreeObj(struct worker *wrk, struct objcore *oc)
{
	const struct obj_methods *m = obj_getmethods(oc);

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	AN(m->objfree);
	m->objfree(wrk, oc);
	AZ(oc->stobj->stevedore);
}

/*====================================================================
 * ObjHasAttr()
 *
 * Check if object has this attribute
 */

int
ObjHasAttr(struct worker *wrk, struct objcore *oc, enum obj_attr attr)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	if (oc->oa_present)
		return (oc->oa_present & (1 << attr));

	/* resurrected persistent objects don't have oa_present set */
	return (ObjGetAttr(wrk, oc, attr, NULL) != NULL ? 1 : 0);
}

/*====================================================================
 * ObjGetAttr()
 *
 * Get an attribute of the object.
 *
 * Returns NULL on unset or zero length attributes and len set to
 * zero. Returns Non-NULL otherwise and len is updated with the attributes
 * length.
 */

const void *
ObjGetAttr(struct worker *wrk, struct objcore *oc, enum obj_attr attr,
   ssize_t *len)
{
	const struct obj_methods *om = obj_getmethods(oc);

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	AN(om->objgetattr);
	return (om->objgetattr(wrk, oc, attr, len));
}

/*====================================================================
 * ObjSetAttr()
 *
 * Setting fixed size attributes always succeeds.
 *
 * Setting a variable size attribute asserts if the combined size of the
 * variable attributes exceeds the total variable attribute space set at
 * object creation. If there is space it always succeeds.
 *
 * Setting an auxiliary attribute can fail.
 *
 * Resetting any variable asserts if the new length does not match the
 * previous length exactly.
 *
 * If ptr is Non-NULL, it points to the new content which is copied into
 * the attribute.  Otherwise the caller will have to do the copying.
 *
 * Return value is non-NULL on success and NULL on failure. If ptr was
 * non-NULL, it is an error to use the returned pointer to set the
 * attribute data, it is only a success indicator in that case.
 */

void *
ObjSetAttr(struct worker *wrk, struct objcore *oc, enum obj_attr attr,
    ssize_t len, const void *ptr)
{
	const struct obj_methods *om = obj_getmethods(oc);
	void *r;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc->boc, BOC_MAGIC);

	AN(om->objsetattr);
	assert((int)attr < 16);
	r = om->objsetattr(wrk, oc, attr, len, ptr);
	if (r)
		oc->oa_present |= (1 << attr);
	return (r);
}

/*====================================================================
 * ObjTouch()
 */

void
ObjTouch(struct worker *wrk, struct objcore *oc, vtim_real now)
{
	const struct obj_methods *om = obj_getmethods(oc);

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	if (om->objtouch != NULL)
		om->objtouch(wrk, oc, now);
}

/*====================================================================
 * Utility functions which work on top of the previous ones
 */

int
ObjCopyAttr(struct worker *wrk, struct objcore *oc, struct objcore *ocs,
    enum obj_attr attr)
{
	const void *vps;
	void *vpd;
	ssize_t l;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(oc->boc, BOC_MAGIC);
	CHECK_OBJ_NOTNULL(ocs, OBJCORE_MAGIC);

	vps = ObjGetAttr(wrk, ocs, attr, &l);
	// XXX: later we want to have zero-length OA's too
	if (vps == NULL || l <= 0)
		return (-1);
	vpd = ObjSetAttr(wrk, oc, attr, l, vps);
	if (vpd == NULL)
		return (-1);
	return (0);
}

int
ObjSetXID(struct worker *wrk, struct objcore *oc, vxid_t xid)
{
	uint64_t u;

	u = VXID(xid);
	AZ(ObjSetU64(wrk, oc, OA_VXID, u));
	return (0);
}


vxid_t
ObjGetXID(struct worker *wrk, struct objcore *oc)
{
	vxid_t u;

	AZ(ObjGetU64(wrk, oc, OA_VXID, &u.vxid));
	return (u);
}

/*--------------------------------------------------------------------
 * There is no well-defined byteorder for IEEE-754 double and the
 * correct solution (frexp(3) and manual encoding) is more work
 * than our (weak) goal of being endian-agnostic requires at this point.
 * We give it a shot by memcpy'ing doubles over a uint64_t and then
 * BE encode that.
 */

int
ObjSetDouble(struct worker *wrk, struct objcore *oc, enum obj_attr a, double t)
{
	void *vp;
	uint64_t u;

	assert(sizeof t == sizeof u);
	memcpy(&u, &t, sizeof u);
	vp = ObjSetAttr(wrk, oc, a, sizeof u, NULL);
	if (vp == NULL)
		return (-1);
	vbe64enc(vp, u);
	return (0);
}

int
ObjGetDouble(struct worker *wrk, struct objcore *oc, enum obj_attr a, double *d)
{
	const void *vp;
	uint64_t u;
	ssize_t l;

	assert(sizeof *d == sizeof u);
	vp = ObjGetAttr(wrk, oc, a, &l);
	if (vp == NULL)
		return (-1);
	if (d != NULL) {
		assert(l == sizeof u);
		u = vbe64dec(vp);
		memcpy(d, &u, sizeof *d);
	}
	return (0);
}

/*--------------------------------------------------------------------
 */

int
ObjSetU64(struct worker *wrk, struct objcore *oc, enum obj_attr a, uint64_t t)
{
	void *vp;

	vp = ObjSetAttr(wrk, oc, a, sizeof t, NULL);
	if (vp == NULL)
		return (-1);
	vbe64enc(vp, t);
	return (0);
}

int
ObjGetU64(struct worker *wrk, struct objcore *oc, enum obj_attr a, uint64_t *d)
{
	const void *vp;
	ssize_t l;

	vp = ObjGetAttr(wrk, oc, a, &l);
	if (vp == NULL || l != sizeof *d)
		return (-1);
	if (d != NULL)
		*d = vbe64dec(vp);
	return (0);
}

/*--------------------------------------------------------------------
 */

int
ObjCheckFlag(struct worker *wrk, struct objcore *oc, enum obj_flags of)
{
	const uint8_t *fp;

	fp = ObjGetAttr(wrk, oc, OA_FLAGS, NULL);
	AN(fp);
	return ((*fp) & of);
}

void
ObjSetFlag(struct worker *wrk, struct objcore *oc, enum obj_flags of, int val)
{
	uint8_t *fp;

	fp = ObjSetAttr(wrk, oc, OA_FLAGS, 1, NULL);
	AN(fp);
	if (val)
		(*fp) |= of;
	else
		(*fp) &= ~of;
}

/*====================================================================
 * Object event subscription mechanism.
 *
 * XXX: it is extremely unclear what the locking circumstances are here.
 */

struct oev_entry {
	unsigned			magic;
#define OEV_MAGIC			0xb0b7c5a1
	unsigned			mask;
	obj_event_f			*func;
	void				*priv;
	VTAILQ_ENTRY(oev_entry)		list;
};

static VTAILQ_HEAD(,oev_entry)		oev_list;
static pthread_rwlock_t			oev_rwl;
static unsigned				oev_mask;

/*
 * NB: ObjSubscribeEvents() is not atomic:
 * oev_mask is checked optimistically in ObjSendEvent()
 */
uintptr_t
ObjSubscribeEvents(obj_event_f *func, void *priv, unsigned mask)
{
	struct oev_entry *oev;

	AN(func);
	AZ(mask & ~OEV_MASK);

	ALLOC_OBJ(oev, OEV_MAGIC);
	AN(oev);
	oev->func = func;
	oev->priv = priv;
	oev->mask = mask;
	PTOK(pthread_rwlock_wrlock(&oev_rwl));
	VTAILQ_INSERT_TAIL(&oev_list, oev, list);
	oev_mask |= mask;
	PTOK(pthread_rwlock_unlock(&oev_rwl));
	return ((uintptr_t)oev);
}

void
ObjUnsubscribeEvents(uintptr_t *handle)
{
	struct oev_entry *oev, *oev2 = NULL;
	unsigned newmask = 0;

	AN(handle);
	AN(*handle);
	PTOK(pthread_rwlock_wrlock(&oev_rwl));
	VTAILQ_FOREACH(oev, &oev_list, list) {
		CHECK_OBJ_NOTNULL(oev, OEV_MAGIC);
		if ((uintptr_t)oev == *handle)
			oev2 = oev;
		else
			newmask |= oev->mask;
	}
	AN(oev2);
	VTAILQ_REMOVE(&oev_list, oev2, list);
	oev_mask = newmask;
	AZ(newmask & ~OEV_MASK);
	PTOK(pthread_rwlock_unlock(&oev_rwl));
	FREE_OBJ(oev2);
	*handle = 0;
}

void
ObjSendEvent(struct worker *wrk, struct objcore *oc, unsigned event)
{
	struct oev_entry *oev;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AN(event & OEV_MASK);
	AZ(event & ~OEV_MASK);
	if (!(event & oev_mask))
		return;

	PTOK(pthread_rwlock_rdlock(&oev_rwl));
	VTAILQ_FOREACH(oev, &oev_list, list) {
		CHECK_OBJ_NOTNULL(oev, OEV_MAGIC);
		if (event & oev->mask)
			oev->func(wrk, oev->priv, oc, event);
	}
	PTOK(pthread_rwlock_unlock(&oev_rwl));

}

void
ObjInit(void)
{
	VTAILQ_INIT(&oev_list);
	PTOK(pthread_rwlock_init(&oev_rwl, NULL));
}
