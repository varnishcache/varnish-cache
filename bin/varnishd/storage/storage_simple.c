/*-
 * Copyright (c) 2007-2015 Varnish Software AS
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
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include "cache/cache.h"
#include "cache/cache_obj.h"
#include "hash/hash_slinger.h"

#include "storage/storage.h"
#include "storage/storage_simple.h"

/*-------------------------------------------------------------------*/

static struct storage *
sml_stv_alloc(const struct stevedore *stv, size_t size)
{
	struct storage *st;

	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);

	if (size > cache_param->fetch_maxchunksize)
		size = cache_param->fetch_maxchunksize;

	assert(size <= UINT_MAX);	/* field limit in struct storage */

	for (;;) {
		/* try to allocate from it */
		AN(stv->alloc);
		st = stv->alloc(stv, size);
		if (st != NULL)
			break;

		if (size <= cache_param->fetch_chunksize)
			break;

		size >>= 1;
	}
	CHECK_OBJ_ORNULL(st, STORAGE_MAGIC);
	return (st);
}

static void
sml_stv_free(const struct stevedore *stv, struct storage *st)
{

	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
	CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
	if (stv->free != NULL)
		stv->free(st);
}

/*--------------------------------------------------------------------
 * This function is called by stevedores ->allocobj() method, which
 * very often will be SML_allocobj() below, to convert a slab
 * of storage into object which the stevedore can then register in its
 * internal state, before returning it to STV_NewObject().
 * As you probably guessed: All this for persistence.
 */

struct object *
SML_MkObject(const struct stevedore *stv, struct objcore *oc, void *ptr)
{
	struct object *o;

	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
	AN(stv->methods);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	assert(PAOK(ptr));

	o = ptr;
	INIT_OBJ(o, OBJECT_MAGIC);

	VTAILQ_INIT(&o->list);

	oc->stobj->stevedore = stv;
	oc->stobj->priv = o;
	oc->stobj->priv2 = 0;
	return (o);
}

/*--------------------------------------------------------------------
 * This is the default ->allocobj() which all stevedores who do not
 * implement persistent storage can rely on.
 */

int __match_proto__(storage_allocobj_f)
SML_allocobj(struct worker *wrk, const struct stevedore *stv,
    struct objcore *oc, unsigned wsl, int really)
{
	struct object *o;
	struct storage *st;
	unsigned ltot;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	assert(really >= 0);
	ltot = sizeof(struct object) + PRNDUP(wsl);
	while (1) {
		if (really > 0) {
			if (EXP_NukeOne(wrk, stv->lru) == -1)
				return (0);
			really--;
		}
		st = stv->alloc(stv, ltot);
		if (st != NULL && st->space < ltot) {
			stv->free(st);
			st = NULL;
		}
		if (st != NULL)
			break;
		if (!really)
			return (0);
	}
	AN(st);
	o = SML_MkObject(stv, oc, st->ptr);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	st->len = sizeof(*o);
	o->objstore = st;
	return (1);
}

/*---------------------------------------------------------------------
 */

static struct object *
sml_getobj(struct worker *wrk, struct objcore *oc)
{
	const struct obj_methods *m;
	struct object *o;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	m = oc->stobj->stevedore->methods;
	if (m->sml_getobj != NULL)
		return (m->sml_getobj(wrk, oc));
	if (oc->stobj->priv == NULL)
		return (NULL);
	CAST_OBJ_NOTNULL(o, oc->stobj->priv, OBJECT_MAGIC);
	return (o);
}

static void __match_proto__(objslim_f)
sml_slim(struct worker *wrk, struct objcore *oc)
{
	const struct stevedore *stv;
	struct object *o;
	struct storage *st, *stn;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	stv = oc->stobj->stevedore;
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
	o = sml_getobj(wrk, oc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);

	if (o->esidata != NULL) {
		sml_stv_free(stv, o->esidata);
		o->esidata = NULL;
	}
	VTAILQ_FOREACH_SAFE(st, &o->list, list, stn) {
		CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
		VTAILQ_REMOVE(&o->list, st, list);
		sml_stv_free(stv, st);
	}
}

static void __match_proto__(objfree_f)
sml_objfree(struct worker *wrk, struct objcore *oc)
{
	struct object *o;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	sml_slim(wrk, oc);
	CAST_OBJ_NOTNULL(o, oc->stobj->priv, OBJECT_MAGIC);
	o->magic = 0;

	sml_stv_free(oc->stobj->stevedore, o->objstore);

	memset(oc->stobj, 0, sizeof oc->stobj);

	wrk->stats->n_object--;
}

static struct lru * __match_proto__(objgetlru_f)
sml_objgetlru(const struct objcore *oc)
{
	const struct stevedore *stv;

	stv = oc->stobj->stevedore;
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
	return (stv->lru);
}

static int __match_proto__(objiterate_f)
sml_iterator(struct worker *wrk, struct objcore *oc,
    void *priv, objiterate_f *func)
{
	struct boc *boc;
	struct object *obj;
	struct storage *st;
	struct storage *checkpoint = NULL;
	ssize_t checkpoint_len = 0;
	ssize_t len = 0;
	int ret = 0;
	ssize_t ol;
	ssize_t nl;
	ssize_t sl;
	void *p;
	ssize_t l;

	obj = sml_getobj(wrk, oc);
	CHECK_OBJ_NOTNULL(obj, OBJECT_MAGIC);

	boc = HSH_RefBusy(oc);

	if (boc == NULL) {
		VTAILQ_FOREACH(st, &obj->list, list)
			if (func(priv, 0, st->ptr, st->len))
				return (-1);
		return (0);
	}

	p = NULL;
	l = 0;

	while (1) {
		ol = len;
		nl = ObjWaitExtend(wrk, oc, boc, ol);
		if (boc->state == BOS_FAILED) {
			ret = -1;
			break;
		}
		if (nl == ol) {
			if (boc->state == BOS_FINISHED)
				break;
			continue;
		}
		Lck_Lock(&boc->mtx);
		AZ(VTAILQ_EMPTY(&obj->list));
		if (checkpoint == NULL) {
			st = VTAILQ_FIRST(&obj->list);
			sl = 0;
		} else {
			st = checkpoint;
			sl = checkpoint_len;
			ol -= checkpoint_len;
		}
		while (st != NULL) {
			if (st->len > ol) {
				p = st->ptr + ol;
				l = st->len - ol;
				len += l;
				break;
			}
			ol -= st->len;
			assert(ol >= 0);
			nl -= st->len;
			assert(nl > 0);
			sl += st->len;
			st = VTAILQ_NEXT(st, list);
			if (VTAILQ_NEXT(st, list) != NULL) {
				checkpoint = st;
				checkpoint_len = sl;
			}
		}
		CHECK_OBJ_NOTNULL(obj, OBJECT_MAGIC);
		CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
		st = VTAILQ_NEXT(st, list);
		if (st != NULL && st->len == 0)
			st = NULL;
		Lck_Unlock(&boc->mtx);
		assert(l > 0 || boc->state == BOS_FINISHED);
		if (func(priv, st != NULL ? 0 : 1, p, l)) {
			ret = -1;
			break;
		}
	}
	HSH_DerefBusy(wrk, &boc);
	return (ret);
}

/*--------------------------------------------------------------------
 */

static struct storage *
objallocwithnuke(struct worker *wrk, const struct stevedore *stv, size_t size)
{
	struct storage *st = NULL;
	unsigned fail;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);

	if (size > cache_param->fetch_maxchunksize)
		size = cache_param->fetch_maxchunksize;

	assert(size <= UINT_MAX);	/* field limit in struct storage */

	for (fail = 0; fail <= cache_param->nuke_limit; fail++) {
		/* try to allocate from it */
		AN(stv->alloc);
		st = sml_stv_alloc(stv, size);
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

static int __match_proto__(objgetspace_f)
sml_getspace(struct worker *wrk, struct objcore *oc, ssize_t *sz,
    uint8_t **ptr)
{
	struct object *o;
	struct storage *st;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AN(sz);
	AN(ptr);
	assert(*sz > 0);

	o = sml_getobj(wrk, oc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);

	st = VTAILQ_LAST(&o->list, storagehead);
	if (st != NULL && st->len < st->space) {
		*sz = st->space - st->len;
		*ptr = st->ptr + st->len;
		assert (*sz > 0);
		return (1);
	}

	st = objallocwithnuke(wrk, oc->stobj->stevedore, *sz);
	if (st == NULL)
		return (0);

	if (oc->busyobj != NULL) {
		CHECK_OBJ_NOTNULL(oc->busyobj, BUSYOBJ_MAGIC);
		Lck_Lock(&oc->busyobj->boc->mtx);
		VTAILQ_INSERT_TAIL(&o->list, st, list);
		Lck_Unlock(&oc->busyobj->boc->mtx);
	} else {
		AN(oc->flags & (OC_F_PRIVATE));
		VTAILQ_INSERT_TAIL(&o->list, st, list);
	}
	*sz = st->space - st->len;
	assert (*sz > 0);
	*ptr = st->ptr + st->len;
	return (1);
}

static void __match_proto__(objextend_f)
sml_extend(struct worker *wrk, struct objcore *oc, ssize_t l)
{
	struct object *o;
	struct storage *st;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	assert(l > 0);

	o = sml_getobj(wrk, oc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	st = VTAILQ_LAST(&o->list, storagehead);
	CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
	assert(st->len + l <= st->space);
	st->len += l;
	o->len += l;
}

static uint64_t __match_proto__(objgetlen_f)
sml_getlen(struct worker *wrk, struct objcore *oc)
{
	struct object *o;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	o = sml_getobj(wrk, oc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	return (o->len);
}

static void __match_proto__(objtrimstore_f)
sml_trimstore(struct worker *wrk, struct objcore *oc)
{
	const struct stevedore *stv;
	struct storage *st, *st1;
	struct object *o;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	stv = oc->stobj->stevedore;
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);

	if (stv->free == NULL)
		return;

	o = sml_getobj(wrk, oc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	st = VTAILQ_LAST(&o->list, storagehead);

	if (st == NULL)
		return;

	if (st->len == 0) {
		if (oc->busyobj != NULL) {
			Lck_Lock(&oc->busyobj->boc->mtx);
			VTAILQ_REMOVE(&o->list, st, list);
			Lck_Unlock(&oc->busyobj->boc->mtx);
		} else {
			VTAILQ_REMOVE(&o->list, st, list);
		}
		sml_stv_free(stv, st);
		return;
	}

	if (st->space - st->len < 512)
		return;

	st1 = sml_stv_alloc(stv, st->len);
	if (st1 == NULL)
		return;

	if (st1->space < st->len) {
		sml_stv_free(stv, st1);
		return;
	}

	memcpy(st1->ptr, st->ptr, st->len);
	st1->len = st->len;
	if (oc->busyobj != NULL) {
		Lck_Lock(&oc->busyobj->boc->mtx);
		VTAILQ_REMOVE(&o->list, st, list);
		VTAILQ_INSERT_TAIL(&o->list, st1, list);
		Lck_Unlock(&oc->busyobj->boc->mtx);
	} else {
		VTAILQ_REMOVE(&o->list, st, list);
		VTAILQ_INSERT_TAIL(&o->list, st1, list);
	}
	if (oc->busyobj == NULL) {
		sml_stv_free(stv, st);
	} else {
		/* sml_stable frees this */
		AZ(oc->busyobj->boc->stevedore_priv);
		oc->busyobj->boc->stevedore_priv = st;
	}
}

static void __match_proto__(objstable_f)
sml_stable(struct worker *wrk, struct objcore *oc, struct busyobj *bo)
{
	const struct stevedore *stv;
	struct storage *st;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	if (bo->boc->stevedore_priv == NULL)
		return;
	CAST_OBJ_NOTNULL(st, bo->boc->stevedore_priv, STORAGE_MAGIC);
	bo->boc->stevedore_priv = 0;
	CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
	stv = oc->stobj->stevedore;
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
	sml_stv_free(stv, st);
}

static void * __match_proto__(objgetattr_f)
sml_getattr(struct worker *wrk, struct objcore *oc, enum obj_attr attr,
   ssize_t *len)
{
	struct object *o;
	ssize_t dummy;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	if (len == NULL)
		len = &dummy;
	o = sml_getobj(wrk, oc);
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

static void * __match_proto__(objsetattr_f)
sml_setattr(struct worker *wrk, struct objcore *oc, enum obj_attr attr,
    ssize_t len, const void *ptr)
{
	struct object *o;
	void *retval = NULL;
	struct storage *st;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	o = sml_getobj(wrk, oc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	st = o->objstore;
	switch (attr) {
	case OA_ESIDATA:
		o->esidata = objallocwithnuke(wrk, oc->stobj->stevedore, len);
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

static void __match_proto__(objtouch_f)
sml_touch(struct worker *wrk, struct objcore *oc, double now)
{
	struct lru *lru;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	/*
	 * To avoid the exphdl->mtx becoming a hotspot, we only
	 * attempt to move objects if they have not been moved
	 * recently and if the lock is available.  This optimization
	 * obviously leaves the LRU list imperfectly sorted.
	 */

	if (oc->flags & OC_F_INCOMPLETE)
		return;

	if (now - oc->last_lru < cache_param->lru_interval)
		return;

	lru = ObjGetLRU(oc);
	CHECK_OBJ_NOTNULL(lru, LRU_MAGIC);

	if (Lck_Trylock(&lru->mtx))
		return;

	AN(oc->exp_flags & OC_EF_EXP);

	if (!(oc->exp_flags & OC_EF_OFFLRU)) {
		/* Can only touch it while it's actually on the LRU list */
		VTAILQ_REMOVE(&lru->lru_head, oc, lru_list);
		VTAILQ_INSERT_TAIL(&lru->lru_head, oc, lru_list);
		VSC_C_main->n_lru_moved++;
	}
	oc->last_lru = now;
	Lck_Unlock(&lru->mtx);
}

const struct obj_methods SML_methods = {
	.objfree	= sml_objfree,
	.objgetlru	= sml_objgetlru,
	.objiterator	= sml_iterator,
	.objgetspace	= sml_getspace,
	.objextend	= sml_extend,
	.objgetlen	= sml_getlen,
	.objtrimstore	= sml_trimstore,
	.objstable	= sml_stable,
	.objslim	= sml_slim,
	.objgetattr	= sml_getattr,
	.objsetattr	= sml_setattr,
	.objtouch	= sml_touch,
};
