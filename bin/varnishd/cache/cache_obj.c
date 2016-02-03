/*-
 * Copyright (c) 2013-2015 Varnish Software AS
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
 * Primary API:
 *	ObjNew		Associate stevedore with oc
 *	ObjGetSpace	Add space
 *	ObjExtend	Commit space
 *	ObjDone		Object completed
 *	ObjGetLen	Len of committed space
 *	ObjIterate	Iterate over committed space
 *	ObjReserveAttr	Attr will be set later
 *	ObjSetAttr	Set attr now
 *	ObjGetAttr	Get attr no
 *	ObjRelease	Done with attr ptr
 *	ObjTouch	Object was used
 */

#include "config.h"

#include <stdlib.h>

#include "cache.h"
#include "cache_obj.h"
#include "vend.h"
#include "storage/storage.h"
#include "hash/hash_slinger.h"

static const struct obj_methods *
obj_getmethods(const struct objcore *oc)
{

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(oc->stobj->stevedore, STEVEDORE_MAGIC);
	AN(oc->stobj->stevedore->methods);
	return (oc->stobj->stevedore->methods);
}

/*====================================================================
 * ObjIterate()
 *
 */

int
ObjIterate(struct worker *wrk, struct objcore *oc,
    void *priv, objiterate_f *func)
{
	const struct obj_methods *om = obj_getmethods(oc);

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AN(func);
	AN(om->objiterator);
	return (om->objiterator(wrk, oc, priv, func));
}

/*====================================================================
 * ObjGetSpace()
 *
 * This function returns a pointer and length of free space.  If there
 * is no free space, some will be added first.
 *
 * The "sz" argument is an input hint of how much space is desired.
 */

int
ObjGetSpace(struct worker *wrk, struct objcore *oc, ssize_t *sz, uint8_t **ptr)
{
	const struct obj_methods *om = obj_getmethods(oc);

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AN(sz);
	AN(ptr);
	assert(*sz > 0);

	AN(om->objgetspace);
	return (om->objgetspace(wrk, oc, sz, ptr));
}

/*====================================================================
 * ObjExtend()
 *
 * This function extends the used part of the object a number of bytes
 * into the last space returned by ObjGetSpace()
 */

void
ObjExtend(struct worker *wrk, struct objcore *oc, ssize_t l)
{
	const struct obj_methods *om = obj_getmethods(oc);

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_ORNULL(oc->boc, BOC_MAGIC);
	AN(om->objextend);
	assert(l > 0);

	if (oc->boc != NULL) {
		Lck_Lock(&oc->boc->mtx);
		om->objextend(wrk, oc, l);
		AZ(pthread_cond_broadcast(&oc->boc->cond));
		Lck_Unlock(&oc->boc->mtx);
	} else {
		om->objextend(wrk, oc, l);
	}
}

/*====================================================================
 */

ssize_t
ObjWaitExtend(struct worker *wrk, struct objcore *oc, ssize_t l)
{
	ssize_t rv;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(oc->boc, BOC_MAGIC);
	Lck_Lock(&oc->boc->mtx);
	rv = ObjGetLen(wrk, oc);
	while (1) {
		assert(l <= rv || oc->boc->state == BOS_FAILED);
		if (rv > l || oc->boc->state >= BOS_FINISHED)
			break;
		(void)Lck_CondWait(&oc->boc->cond, &oc->boc->mtx, 0);
		rv = ObjGetLen(wrk, oc);
	}
	Lck_Unlock(&oc->boc->mtx);
	return (rv);
}

/*====================================================================
 */

void
ObjSetState(const struct objcore *oc, enum boc_state_e next)
{

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(oc->boc, BOC_MAGIC);

	assert(next > oc->boc->state);
	Lck_Lock(&oc->boc->mtx);
	oc->boc->state = next;
	AZ(pthread_cond_broadcast(&oc->boc->cond));
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
	while (1) {
		if (oc->boc->state >= want)
			break;
		(void)Lck_CondWait(&oc->boc->cond, &oc->boc->mtx, 0);
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
	const struct obj_methods *om = obj_getmethods(oc);

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	AN(om->objgetlen);
	return (om->objgetlen(wrk, oc));
}

/*====================================================================
 * ObjTrimStore()
 *
 * Release any surplus space allocated, we promise not to call ObjExtend()
 * any more.
 */

void
ObjTrimStore(struct worker *wrk, struct objcore *oc)
{
	const struct obj_methods *om = obj_getmethods(oc);

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	assert(oc->boc == NULL || oc->boc->state < BOS_FINISHED);

	if (om->objtrimstore != NULL)
		om->objtrimstore(wrk, oc);
	return;
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
 */
void
ObjUpdateMeta(struct worker *wrk, struct objcore *oc)
{
	const struct obj_methods *m = obj_getmethods(oc);

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	if (m->objupdatemeta != NULL)
		m->objupdatemeta(wrk, oc);
}

/*====================================================================
 * Called when the boc used to populate the objcore is going away.
 * Useful for releasing any leftovers from Trim.
 */

void
ObjStable(struct worker *wrk, struct objcore *oc, struct boc *boc)
{
	const struct obj_methods *m = obj_getmethods(oc);

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(boc, BOC_MAGIC);
	if (m->objstable != NULL)
		m->objstable(wrk, oc, boc);
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
 */
struct lru *
ObjGetLRU(const struct objcore *oc)
{
	const struct obj_methods *m = obj_getmethods(oc);

	AN(m->objgetlru);
	return (m->objgetlru(oc));
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
 */

void *
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
 * If ptr is Non-NULL, it points to the new content which is copied into
 * the attribute.  Otherwise the caller will have to do the copying.
 */

void *
ObjSetAttr(struct worker *wrk, struct objcore *oc, enum obj_attr attr,
    ssize_t len, const void *ptr)
{
	const struct obj_methods *om = obj_getmethods(oc);

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	AN(om->objsetattr);
	assert((int)attr < 16);
	oc->oa_present |= (1 << attr);
	return (om->objsetattr(wrk, oc, attr, len, ptr));
}

/*====================================================================
 * ObjTouch()
 */

void
ObjTouch(struct worker *wrk, struct objcore *oc, double now)
{
	const struct obj_methods *om = obj_getmethods(oc);

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	if (om->objtouch != NULL)
		om->objtouch(wrk, oc, now);
}

/*====================================================================
 * ObjSnipe()
 *
 * If objcore is idle, gain a ref and mark it dead.
 */

int
ObjSnipe(const struct worker *wrk, struct objcore *oc)
{
	int retval = 0;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(oc->objhead, OBJHEAD_MAGIC);

	AZ(oc->exp_flags & OC_EF_DYING);

	if (oc->refcnt == 1 && !Lck_Trylock(&oc->objhead->mtx)) {
		if (oc->refcnt == 1) {
			oc->exp_flags |= OC_EF_DYING;
			oc->refcnt++;
			retval = 1;
		}
		Lck_Unlock(&oc->objhead->mtx);
	}
	return (retval);
}

/*====================================================================
 * Utility functions which work on top of the previous ones
 */

int
ObjCopyAttr(struct worker *wrk, struct objcore *oc, struct objcore *ocs,
    enum obj_attr attr)
{
	void *vps, *vpd;
	ssize_t l;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
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

unsigned
ObjGetXID(struct worker *wrk, struct objcore *oc)
{
	uint32_t u;

	AZ(ObjGetU32(wrk, oc, OA_VXID, &u));
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
	void *vp;
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
	void *vp;
	ssize_t l;

	vp = ObjGetAttr(wrk, oc, a, &l);
	if (vp == NULL || l != sizeof *d)
		return (-1);
	if (d != NULL)
		*d = vbe64dec(vp);
	return (0);
}

int
ObjSetU32(struct worker *wrk, struct objcore *oc, enum obj_attr a, uint32_t t)
{
	void *vp;

	vp = ObjSetAttr(wrk, oc, a, sizeof t, NULL);
	if (vp == NULL)
		return (-1);
	vbe32enc(vp, t);
	return (0);
}

int
ObjGetU32(struct worker *wrk, struct objcore *oc, enum obj_attr a, uint32_t *d)
{
	void *vp;
	ssize_t l;

	vp = ObjGetAttr(wrk, oc, a, &l);
	if (vp == NULL || l != sizeof *d)
		return (-1);
	if (d != NULL)
		*d = vbe32dec(vp);
	return (0);
}

/*--------------------------------------------------------------------
 */

int
ObjCheckFlag(struct worker *wrk, struct objcore *oc, enum obj_flags of)
{
	uint8_t *fp;

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
