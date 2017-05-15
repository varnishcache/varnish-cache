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

#include "cache/cache.h"

#include "cache/cache_obj.h"
#include "hash/hash_slinger.h"

#include "storage/storage.h"
#include "storage/storage_simple.h"

#include "vtim.h"

/* Flags for allocating memory in sml_stv_alloc */
#define LESS_MEM_ALLOCED_IS_OK	1

/*-------------------------------------------------------------------*/

static struct storage *
sml_stv_alloc(const struct stevedore *stv, size_t size, int flags)
{
	struct storage *st;

	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
	AN(stv->sml_alloc);

	if (!(flags & LESS_MEM_ALLOCED_IS_OK)) {
		if (size > cache_param->fetch_maxchunksize)
			return (NULL);
		else
			return (stv->sml_alloc(stv, size));
	}

	if (size > cache_param->fetch_maxchunksize)
		size = cache_param->fetch_maxchunksize;

	assert(size <= UINT_MAX);	/* field limit in struct storage */

	for (;;) {
		/* try to allocate from it */
		assert(size > 0);
		st = stv->sml_alloc(stv, size);
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
	if (stv->sml_free != NULL)
		stv->sml_free(st);
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
    struct objcore *oc, unsigned wsl)
{
	struct object *o;
	struct storage *st = NULL;
	unsigned ltot;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	AN(stv->sml_alloc);

	ltot = sizeof(struct object) + PRNDUP(wsl);

	do {
		st = stv->sml_alloc(stv, ltot);
		if (st != NULL && st->space < ltot) {
			stv->sml_free(st);
			st = NULL;
		}
	} while (st == NULL && LRU_NukeOne(wrk, stv->lru));
	if (st == NULL)
		return (0);

	CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
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
	const struct stevedore *stv;
	struct object *o;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	stv = oc->stobj->stevedore;
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
	if (stv->sml_getobj != NULL)
		return (stv->sml_getobj(wrk, oc));
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

#define OBJ_AUXATTR(U, l)						\
	if (o->aa_##l != NULL) {					\
		sml_stv_free(stv, o->aa_##l);				\
		o->aa_##l = NULL;					\
	}
#include "tbl/obj_attr.h"

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

	if (oc->boc == NULL && oc->stobj->stevedore->lru != NULL)
		LRU_Remove(oc);

	sml_stv_free(oc->stobj->stevedore, o->objstore);

	memset(oc->stobj, 0, sizeof oc->stobj);

	wrk->stats->n_object--;
}

static int __match_proto__(objiterate_f)
sml_iterator(struct worker *wrk, struct objcore *oc,
    void *priv, objiterate_f *func, int final)
{
	struct boc *boc;
	struct object *obj;
	struct storage *st;
	struct storage *checkpoint = NULL;
	const struct stevedore *stv;
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
	stv = oc->stobj->stevedore;
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);

	boc = HSH_RefBoc(oc);

	if (boc == NULL) {
		VTAILQ_FOREACH_SAFE(st, &obj->list, list, checkpoint) {
			if (ret == 0 && st->len > 0)
				ret = func(priv, 1, st->ptr, st->len);
			if (final) {
				VTAILQ_REMOVE(&obj->list, st, list);
				sml_stv_free(stv, st);
			} else if (ret)
				break;
		}
		return (ret);
	}

	p = NULL;
	l = 0;

	while (1) {
		ol = len;
		nl = ObjWaitExtend(wrk, oc, ol);
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
				if (final && checkpoint != NULL) {
					VTAILQ_REMOVE(&obj->list,
					    checkpoint, list);
					sml_stv_free(stv, checkpoint);
				}
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
		ret = func(priv, st != NULL ? final : 1, p, l);
		if (ret)
			break;
	}
	HSH_DerefBoc(wrk, oc);
	return (ret);
}

/*--------------------------------------------------------------------
 */

static struct storage *
objallocwithnuke(struct worker *wrk, const struct stevedore *stv, size_t size,
    int flags)
{
	struct storage *st = NULL;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);

	if (size > cache_param->fetch_maxchunksize) {
		if (!(flags & LESS_MEM_ALLOCED_IS_OK))
			return (NULL);
		size = cache_param->fetch_maxchunksize;
	}

	assert(size <= UINT_MAX);	/* field limit in struct storage */

	do {
		/* try to allocate from it */
		st = sml_stv_alloc(stv, size, flags);
		if (st != NULL)
			break;

		/* no luck; try to free some space and keep trying */
		if (stv->lru == NULL)
			break;
	} while (LRU_NukeOne(wrk, stv->lru));

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
	CHECK_OBJ_NOTNULL(oc->boc, BOC_MAGIC);

	st = VTAILQ_LAST(&o->list, storagehead);
	if (st != NULL && st->len < st->space) {
		*sz = st->space - st->len;
		*ptr = st->ptr + st->len;
		assert (*sz > 0);
		return (1);
	}

	st = objallocwithnuke(wrk, oc->stobj->stevedore, *sz,
	    LESS_MEM_ALLOCED_IS_OK);
	if (st == NULL)
		return (0);

	CHECK_OBJ_NOTNULL(oc->boc, BOC_MAGIC);
	Lck_Lock(&oc->boc->mtx);
	VTAILQ_INSERT_TAIL(&o->list, st, list);
	Lck_Unlock(&oc->boc->mtx);

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
}

static void __match_proto__(objtrimstore_f)
sml_trimstore(struct worker *wrk, struct objcore *oc)
{
	const struct stevedore *stv;
	struct storage *st, *st1;
	struct object *o;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(oc->boc, BOC_MAGIC);

	stv = oc->stobj->stevedore;
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);

	if (stv->sml_free == NULL)
		return;

	o = sml_getobj(wrk, oc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	st = VTAILQ_LAST(&o->list, storagehead);

	if (st == NULL)
		return;

	if (st->len == 0) {
		Lck_Lock(&oc->boc->mtx);
		VTAILQ_REMOVE(&o->list, st, list);
		Lck_Unlock(&oc->boc->mtx);
		sml_stv_free(stv, st);
		return;
	}

	if (st->space - st->len < 512)
		return;

	st1 = sml_stv_alloc(stv, st->len, 0);
	if (st1 == NULL)
		return;
	assert(st1->space >= st->len);

	memcpy(st1->ptr, st->ptr, st->len);
	st1->len = st->len;
	Lck_Lock(&oc->boc->mtx);
	VTAILQ_REMOVE(&o->list, st, list);
	VTAILQ_INSERT_TAIL(&o->list, st1, list);
	Lck_Unlock(&oc->boc->mtx);
	/* sml_bocdone frees this */
	AZ(oc->boc->stevedore_priv);
	oc->boc->stevedore_priv = st;
}

static void __match_proto__(objbocdone_f)
sml_bocdone(struct worker *wrk, struct objcore *oc, struct boc *boc)
{
	const struct stevedore *stv;
	struct storage *st;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(boc, BOC_MAGIC);
	stv = oc->stobj->stevedore;
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);

	if (boc->stevedore_priv != NULL) {
		/* Free any leftovers from Trim */
		CAST_OBJ_NOTNULL(st, boc->stevedore_priv, STORAGE_MAGIC);
		boc->stevedore_priv = 0;
		CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
		sml_stv_free(stv, st);
	}

	if (stv->lru != NULL) {
		if (isnan(wrk->lastused))
			wrk->lastused = VTIM_real();
		LRU_Add(oc, wrk->lastused);	// approx timestamp is OK
	}
}

static const void * __match_proto__(objgetattr_f)
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
		/* Fixed size attributes */
#define OBJ_FIXATTR(U, l, s)						\
	case OA_##U:							\
		*len = sizeof o->fa_##l;				\
		return (o->fa_##l);
#include "tbl/obj_attr.h"

		/* Variable size attributes */
#define OBJ_VARATTR(U, l)						\
	case OA_##U:							\
		if (o->va_##l == NULL)					\
			return (NULL);					\
		*len = o->va_##l##_len;					\
		return (o->va_##l);
#include "tbl/obj_attr.h"

		/* Auxiliary attributes */
#define OBJ_AUXATTR(U, l)						\
	case OA_##U:							\
		if (o->aa_##l == NULL)					\
			return (NULL);					\
		CHECK_OBJ_NOTNULL(o->aa_##l, STORAGE_MAGIC);		\
		*len = o->aa_##l->len;					\
		return (o->aa_##l->ptr);
#include "tbl/obj_attr.h"

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
		/* Fixed size attributes */
#define OBJ_FIXATTR(U, l, s)						\
	case OA_##U:							\
		assert(len == sizeof o->fa_##l);			\
		retval = o->fa_##l;					\
		break;
#include "tbl/obj_attr.h"

		/* Variable size attributes */
#define OBJ_VARATTR(U, l)						\
	case OA_##U:							\
		if (o->va_##l##_len > 0) {				\
			AN(o->va_##l);					\
			assert(len == o->va_##l##_len);			\
			retval = o->va_##l;				\
		} else if (len > 0) {					\
			assert(len <= UINT_MAX);			\
			assert(st->len + len <= st->space);		\
			o->va_##l = st->ptr + st->len;			\
			st->len += len;					\
			o->va_##l##_len = len;				\
			retval = o->va_##l;				\
		}							\
		break;
#include "tbl/obj_attr.h"

		/* Auxiliary attributes */
#define OBJ_AUXATTR(U, l)						\
	case OA_##U:							\
		if (o->aa_##l != NULL) {				\
			CHECK_OBJ_NOTNULL(o->aa_##l, STORAGE_MAGIC);	\
			assert(len == o->aa_##l->len);			\
			retval = o->aa_##l->ptr;			\
			break;						\
		}							\
		if (len == 0)						\
			break;						\
		o->aa_##l = objallocwithnuke(wrk, oc->stobj->stevedore,	\
		    len, 0);						\
		if (o->aa_##l == NULL)					\
			break;						\
		CHECK_OBJ_NOTNULL(o->aa_##l, STORAGE_MAGIC);		\
		assert(len <= o->aa_##l->space);			\
		o->aa_##l->len = len;					\
		retval = o->aa_##l->ptr;				\
		break;
#include "tbl/obj_attr.h"

	default:
		WRONG("Unsupported OBJ_ATTR");
		break;
	}

	if (retval != NULL && ptr != NULL)
		memcpy(retval, ptr, len);
	return (retval);
}

const struct obj_methods SML_methods = {
	.objfree	= sml_objfree,
	.objiterator	= sml_iterator,
	.objgetspace	= sml_getspace,
	.objextend	= sml_extend,
	.objtrimstore	= sml_trimstore,
	.objbocdone	= sml_bocdone,
	.objslim	= sml_slim,
	.objgetattr	= sml_getattr,
	.objsetattr	= sml_setattr,
	.objtouch	= LRU_Touch,
};

static void
sml_panic_st(struct vsb *vsb, const char *hd, const struct storage *st)
{
	VSB_printf(vsb, "%s = %p {priv=%p, ptr=%p, len=%u, space=%u},\n",
	    hd, st, st->priv, st->ptr, st->len, st->space);
}

void
SML_panic(struct vsb *vsb, const struct objcore *oc)
{
	struct object *o;
	struct storage *st;

	VSB_printf(vsb, "Simple = %p,\n", oc->stobj->priv);
	if (oc->stobj->priv == NULL)
		return;
	CAST_OBJ_NOTNULL(o, oc->stobj->priv, OBJECT_MAGIC);
	sml_panic_st(vsb, "Obj", o->objstore);

#define OBJ_FIXATTR(U, l, sz) \
	VSB_printf(vsb, "%s = ", #U); \
	VSB_quote(vsb, (const void*)o->fa_##l, sz, VSB_QUOTE_HEX); \
	VSB_printf(vsb, ",\n");

#define OBJ_VARATTR(U, l) \
	VSB_printf(vsb, "%s = {len=%u, ptr=%p},\n", \
	    #U, o->va_##l##_len, o->va_##l);

#define OBJ_AUXATTR(U, l) \
	if (o->aa_##l != NULL) sml_panic_st(vsb, #U, o->aa_##l);

#include "tbl/obj_attr.h"

	VTAILQ_FOREACH(st, &o->list, list) {
		sml_panic_st(vsb, "Body", st);
	}
}
