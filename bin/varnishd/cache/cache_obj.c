/*-
 * Copyright (c) 2013 Varnish Software AS
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
 *	ObjIter		Iterate over committed space
 *	ObjReserveAttr	Attr will be set later
 *	ObjSetAttr	Set attr now
 *	ObjGetAttr	Get attr no
 *	ObjRelease	Done with attr ptr
 */

#include "config.h"

#include <stdlib.h>

#include "cache.h"
#include "vend.h"
#include "storage/storage.h"
#include "hash/hash_slinger.h"

static const struct storeobj_methods *
obj_getmethods(const struct objcore *oc)
{

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(oc->stobj->stevedore, STEVEDORE_MAGIC);
	AN(oc->stobj->stevedore->methods);
	return (oc->stobj->stevedore->methods);
}

static struct object *
obj_getobj(struct worker *wrk, struct objcore *oc)
{
	const struct storeobj_methods *m = obj_getmethods(oc);

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AN(m->getobj);
	return (m->getobj(wrk, oc));
}

/*====================================================================
 * ObjIterBegin()
 * ObjIter()
 * ObjIterEnd()
 *
 * These three allow iteration over the body of an object.
 * The ObjIterBegin() returns a magic cookie which must be passed to
 * ObjIter() and which ObjIterEnd() will obliterate again.
 *
 * These functions get slightly complicated due to unbusy but not
 * yet completed objects (ie: when streaming).  Exactly how they
 * interact with ObjExtend(), especially with respect to locking,
 * is entirely up to the implementation.
 */

struct objiter {
	unsigned			magic;
#define OBJITER_MAGIC			0x745fb151
	struct busyobj			*bo;
	struct objcore			*oc;
	struct object			*obj;
	struct storage			*st;
	struct worker			*wrk;
	ssize_t				len;
};

void *
ObjIterBegin(struct worker *wrk, struct objcore *oc)
{
	struct objiter *oi;
	struct object *obj;
	const struct storeobj_methods *om = obj_getmethods(oc);

	if (om->objiterbegin != NULL)
		return (om->objiterbegin(wrk, oc));

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	obj = obj_getobj(wrk, oc);
	CHECK_OBJ_NOTNULL(obj, OBJECT_MAGIC);
	ALLOC_OBJ(oi, OBJITER_MAGIC);
	if (oi == NULL)
		return (oi);
	oi->oc = oc;
	oi->obj = obj;
	oi->wrk = wrk;
	if (oc->objhead != NULL)
		oi->bo = HSH_RefBusy(oc);
	return (oi);
}

enum objiter_status
ObjIter(struct objcore *oc, void *oix, void **p, ssize_t *l)
{
	struct objiter *oi;
	ssize_t ol;
	ssize_t nl;
	const struct storeobj_methods *om = obj_getmethods(oc);

	if (om->objiter != NULL)
		return (om->objiter(oc, oix, p, l));

	CAST_OBJ_NOTNULL(oi, oix, OBJITER_MAGIC);
	CHECK_OBJ_NOTNULL(oi->obj, OBJECT_MAGIC);
	AN(p);
	AN(l);
	*p = NULL;
	*l = 0;

	if (oi->bo == NULL) {
		if (oi->st == NULL)
			oi->st = VTAILQ_FIRST(&oi->obj->list);
		else
			oi->st = VTAILQ_NEXT(oi->st, list);
		while(oi->st != NULL && oi->st->len == 0)
			oi->st = VTAILQ_NEXT(oi->st, list);
		if (oi->st != NULL) {
			*p = oi->st->ptr;
			*l = oi->st->len;
			assert(*l > 0);
			return (OIS_DATA);
		}
		return (OIS_DONE);
	} else {
		ol = oi->len;
		while (1) {
			nl = VBO_waitlen(oi->wrk, oi->bo, ol);
			if (nl != ol)
				break;
			if (oi->bo->state == BOS_FINISHED)
				return (OIS_DONE);
			if (oi->bo->state == BOS_FAILED)
				return (OIS_ERROR);
		}
		Lck_Lock(&oi->bo->mtx);
		AZ(VTAILQ_EMPTY(&oi->obj->list));
		VTAILQ_FOREACH(oi->st, &oi->obj->list, list) {
			if (oi->st->len > ol) {
				*p = oi->st->ptr + ol;
				*l = oi->st->len - ol;
				oi->len += *l;
				break;
			}
			ol -= oi->st->len;
			assert(ol >= 0);
			nl -= oi->st->len;
			assert(nl > 0);
		}
		CHECK_OBJ_NOTNULL(oi->obj, OBJECT_MAGIC);
		CHECK_OBJ_NOTNULL(oi->st, STORAGE_MAGIC);
		oi->st = VTAILQ_NEXT(oi->st, list);
		if (oi->st != NULL && oi->st->len == 0)
			oi->st = NULL;
		Lck_Unlock(&oi->bo->mtx);
		assert(*l > 0 || oi->bo->state == BOS_FINISHED);
		return (oi->st != NULL ? OIS_DATA : OIS_STREAM);
	}
}

void
ObjIterEnd(struct objcore *oc, void **oix)
{
	struct objiter *oi;
	const struct storeobj_methods *om = obj_getmethods(oc);

	if (om->objiterend != NULL) {
		om->objiterend(oc, oix);
		return;
	}

	AN(oc);
	AN(oix);
	CAST_OBJ_NOTNULL(oi, (*oix), OBJITER_MAGIC);
	*oix = NULL;
	CHECK_OBJ_NOTNULL(oi->obj, OBJECT_MAGIC);
	if (oi->bo != NULL) {
		if (oi->oc->flags & OC_F_PASS)
			oi->bo->abandon = 1;
		VBO_DerefBusyObj(oi->wrk, &oi->bo);
	}
	FREE_OBJ(oi);
}

/*--------------------------------------------------------------------
 */

static struct storage *
objallocwithnuke(struct stevedore *stv, struct worker *wrk, size_t size)
{
	struct storage *st = NULL;
	unsigned fail;

	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);

	if (size > cache_param->fetch_maxchunksize)
		size = cache_param->fetch_maxchunksize;

	assert(size <= UINT_MAX);	/* field limit in struct storage */

	for (fail = 0; fail <= cache_param->nuke_limit; fail++) {
		/* try to allocate from it */
		AN(stv->alloc);
		st = STV_alloc(stv, size);
		if (st != NULL)
			break;

		/* no luck; try to free some space and keep trying */
		if (fail < cache_param->nuke_limit &&
		    EXP_NukeOne(wrk, stv->lru) == -1)
			break;
	}
	CHECK_OBJ_ORNULL(st, STORAGE_MAGIC);
	return (st);
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
	struct object *o;
	struct storage *st;
	const struct storeobj_methods *om = obj_getmethods(oc);

	if (om->objgetspace != NULL)
		return (om->objgetspace(wrk, oc, sz, ptr));

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AN(sz);
	AN(ptr);
	assert(*sz > 0);
	o = obj_getobj(wrk, oc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);

	st = VTAILQ_LAST(&o->list, storagehead);
	if (st != NULL && st->len < st->space) {
		*sz = st->space - st->len;
		*ptr = st->ptr + st->len;
		assert (*sz > 0);
		return (1);
	}

	st = objallocwithnuke(oc->stobj->stevedore, wrk, *sz);
	if (st == NULL)
		return (0);

	if (oc->busyobj != NULL) {
		CHECK_OBJ_NOTNULL(oc->busyobj, BUSYOBJ_MAGIC);
		Lck_Lock(&oc->busyobj->mtx);
		VTAILQ_INSERT_TAIL(&o->list, st, list);
		Lck_Unlock(&oc->busyobj->mtx);
	} else {
		VTAILQ_INSERT_TAIL(&o->list, st, list);
	}
	*sz = st->space - st->len;
	assert (*sz > 0);
	*ptr = st->ptr + st->len;
	return (1);
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
	struct object *o;
	struct storage *st;
	const struct storeobj_methods *om = obj_getmethods(oc);

	if (om->objextend != NULL) {
		om->objextend(wrk, oc, l);
		return;
	}

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	o = obj_getobj(wrk, oc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	st = VTAILQ_LAST(&o->list, storagehead);
	CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
	assert(st->len + l <= st->space);
	st->len += l;
	o->len += l;
}

/*====================================================================
 * ObjGetlen()
 *
 * This is a separate function because it may need locking
 */

uint64_t
ObjGetLen(struct worker *wrk, struct objcore *oc)
{
	struct object *o;
	const struct storeobj_methods *om = obj_getmethods(oc);

	if (om->objgetlen != NULL)
		return (om->objgetlen(wrk, oc));

	o = obj_getobj(wrk, oc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	return (o->len);
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
	const struct stevedore *stv;
	struct storage *st;
	struct object *o;
	const struct storeobj_methods *om = obj_getmethods(oc);

	if (om->objtrimstore != NULL) {
		om->objtrimstore(wrk, oc);
		return;
	}

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	stv = oc->stobj->stevedore;
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
	o = obj_getobj(wrk, oc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	st = VTAILQ_LAST(&o->list, storagehead);
	if (st == NULL)
		return;
	if (st->len == 0) {
		VTAILQ_REMOVE(&o->list, st, list);
		STV_free(st);
	} else if (st->len < st->space) {
		STV_trim(st, st->len, 1);
	}
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
	struct object *o;
	struct storage *st, *stn;
	const struct storeobj_methods *om = obj_getmethods(oc);

	if (om->objslim != NULL) {
		om->objslim(wrk, oc);
		return;
	}

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	o = obj_getobj(wrk, oc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);

	if (o->esidata != NULL) {
		STV_free(o->esidata);
		o->esidata = NULL;
	}
	VTAILQ_FOREACH_SAFE(st, &o->list, list, stn) {
		CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
		VTAILQ_REMOVE(&o->list, st, list);
		STV_free(st);
	}
}

/*====================================================================
 */
void
ObjUpdateMeta(struct worker *wrk, struct objcore *oc)
{
	const struct storeobj_methods *m = obj_getmethods(oc);

	if (m->updatemeta != NULL)
		m->updatemeta(wrk, oc);
}

/*====================================================================
 */
void
ObjFreeObj(struct worker *wrk, struct objcore *oc)
{
	const struct storeobj_methods *m = obj_getmethods(oc);

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AN(m->freeobj);
	m->freeobj(wrk, oc);
}

/*====================================================================
 */
struct lru *
ObjGetLRU(const struct objcore *oc)
{
	const struct storeobj_methods *m = obj_getmethods(oc);

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AN(m->getlru);
	return (m->getlru(oc));
}

/*====================================================================
 * ObjGetattr()
 *
 * Get an attribute of the object.
 */

void *
ObjGetattr(struct worker *wrk, struct objcore *oc, enum obj_attr attr,
   ssize_t *len)
{
	struct object *o;
	ssize_t dummy;
	const struct storeobj_methods *om = obj_getmethods(oc);

	if (om->objgetattr != NULL)
		return (om->objgetattr(wrk, oc, attr, len));

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	if (len == NULL)
		len = &dummy;
	o = obj_getobj(wrk, oc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	switch (attr) {
	case OA_ESIDATA:
		if (o->esidata == NULL)
			return (NULL);
		*len = o->esidata->len;
		return (o->esidata->ptr);
	case OA_FLAGS:
		*len = sizeof o->oa_flags;
		return (o->oa_flags);
	case OA_GZIPBITS:
		*len = sizeof o->oa_gzipbits;
		return (o->oa_gzipbits);
	case OA_HEADERS:
		*len = 0;			// XXX: hack
		return (o->oa_http);
	case OA_LASTMODIFIED:
		*len = sizeof o->oa_lastmodified;
		return (o->oa_lastmodified);
	case OA_VARY:
		*len = 4;			// XXX: hack
		return (o->oa_vary);
	case OA_VXID:
		*len = sizeof o->oa_vxid;
		return (o->oa_vxid);
	default:
		break;
	}
	WRONG("Unsupported OBJ_ATTR");
}

/*====================================================================
 * ObjSetattr()
 *
 * If ptr is Non-NULL, it points to the new content which is copied into
 * the attribute.  Otherwise the caller will have to do the copying.
 */

void *
ObjSetattr(struct worker *wrk, struct objcore *oc, enum obj_attr attr,
    ssize_t len, const void *ptr)
{
	struct object *o;
	void *retval = NULL;
	struct storage *st;
	const struct storeobj_methods *om = obj_getmethods(oc);

	if (om->objsetattr != NULL)
		return (om->objsetattr(wrk, oc, attr, len, ptr));

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	o = obj_getobj(wrk, oc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	st = o->objstore;
	switch (attr) {
	case OA_ESIDATA:
		o->esidata = objallocwithnuke(oc->stobj->stevedore, wrk, len);
		if (o->esidata == NULL)
			return (NULL);
		o->esidata->len = len;
		retval  = o->esidata->ptr;
		break;
	case OA_FLAGS:
		assert(len == sizeof o->oa_flags);
		retval = o->oa_flags;
		break;
	case OA_GZIPBITS:
		assert(len == sizeof o->oa_gzipbits);
		retval = o->oa_gzipbits;
		break;
	case OA_HEADERS:
		len = PRNDUP(len);
		assert(st->len + len <= st->space);
		o->oa_http = (void*)(st->ptr + st->len);
		st->len += len;
		retval = o->oa_http;
		break;
	case OA_LASTMODIFIED:
		assert(len == sizeof o->oa_lastmodified);
		retval = o->oa_lastmodified;
		break;
	case OA_VARY:
		len = PRNDUP(len);
		assert(st->len + len <= st->space);
		o->oa_vary = (void*)(st->ptr + st->len);
		st->len += len;
		retval = o->oa_vary;
		break;
	case OA_VXID:
		assert(len == sizeof o->oa_vxid);
		retval = o->oa_vxid;
		break;
	default:
		WRONG("Unsupported OBJ_ATTR");
		break;
	}
	if (ptr != NULL)
		memcpy(retval, ptr, len);
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

	vps = ObjGetattr(wrk, ocs, attr, &l);
	// XXX: later we want to have zero-length OA's too
	if (vps == NULL || l <= 0)
		return (-1);
	vpd = ObjSetattr(wrk, oc, attr, l, vps);
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
	vp = ObjSetattr(wrk, oc, a, sizeof u, NULL);
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
	vp = ObjGetattr(wrk, oc, a, &l);
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

	vp = ObjSetattr(wrk, oc, a, sizeof t, NULL);
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

	vp = ObjGetattr(wrk, oc, a, &l);
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

	vp = ObjSetattr(wrk, oc, a, sizeof t, NULL);
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

	vp = ObjGetattr(wrk, oc, a, &l);
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

	fp = ObjGetattr(wrk, oc, OA_FLAGS, NULL);
	AN(fp);
	return ((*fp) & of);
}

void
ObjSetFlag(struct worker *wrk, struct objcore *oc, enum obj_flags of, int val)
{
	uint8_t *fp;

	fp = ObjSetattr(wrk, oc, OA_FLAGS, 1, NULL);
	AN(fp);
	if (val)
		(*fp) |= of;
	else
		(*fp) &= ~of;
}
