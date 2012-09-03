/*-
 * Copyright (c) 2007-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Dag-Erling Smørgav <des@des.no>
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
 * STEVEDORE: one who works at or is responsible for loading and
 * unloading ships in port.  Example: "on the wharves, stevedores were
 * unloading cargo from the far corners of the world." Origin: Spanish
 * estibador, from estibar to pack.  First Known Use: 1788
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include "cache/cache.h"

#include "storage/storage.h"
#include "vrt.h"
#include "vrt_obj.h"

static const struct stevedore * volatile stv_next;

/*---------------------------------------------------------------------
 * Default objcore methods
 */

static unsigned __match_proto__(getxid_f)
default_oc_getxid(struct dstat *ds, struct objcore *oc)
{
	struct object *o;

	o = oc_getobj(ds, oc);
	return (o->vxid);
}

static struct object * __match_proto__(getobj_f)
default_oc_getobj(struct dstat *ds, struct objcore *oc)
{
	struct object *o;

	(void)ds;
	if (oc->priv == NULL)
		return (NULL);
	CAST_OBJ_NOTNULL(o, oc->priv, OBJECT_MAGIC);
	return (o);
}

static void
default_oc_freeobj(struct objcore *oc)
{
	struct object *o;

	CAST_OBJ_NOTNULL(o, oc->priv, OBJECT_MAGIC);
	oc->priv = NULL;
	oc->methods = NULL;

	STV_Freestore(o);
	STV_free(o->objstore);
}

static struct lru *
default_oc_getlru(const struct objcore *oc)
{
	struct stevedore *stv;

	CAST_OBJ_NOTNULL(stv, (void *)oc->priv2, STEVEDORE_MAGIC);
	return (stv->lru);
}

static struct objcore_methods default_oc_methods = {
	.getobj = default_oc_getobj,
	.getxid = default_oc_getxid,
	.freeobj = default_oc_freeobj,
	.getlru = default_oc_getlru,
};


/*--------------------------------------------------------------------
 */

struct lru *
LRU_Alloc(void)
{
	struct lru *l;

	ALLOC_OBJ(l, LRU_MAGIC);
	AN(l);
	VTAILQ_INIT(&l->lru_head);
	Lck_New(&l->mtx, lck_lru);
	return (l);
}

void
LRU_Free(struct lru *lru)
{
	CHECK_OBJ_NOTNULL(lru, LRU_MAGIC);
	Lck_Delete(&lru->mtx);
	FREE_OBJ(lru);
}

/*--------------------------------------------------------------------
 * XXX: trust pointer writes to be atomic
 */

static struct stevedore *
stv_pick_stevedore(struct vsl_log *vsl, const char **hint)
{
	struct stevedore *stv;

	AN(hint);
	if (*hint != NULL && **hint != '\0') {
		VTAILQ_FOREACH(stv, &stv_stevedores, list) {
			if (!strcmp(stv->ident, *hint))
				return (stv);
		}
		if (!strcmp(TRANSIENT_STORAGE, *hint))
			return (stv_transient);

		/* Hint was not valid, nuke it */
		VSLb(vsl, SLT_Debug, "Storage hint not usable");
		*hint = NULL;
	}
	if (stv_next == NULL)
		return (stv_transient);
	/* pick a stevedore and bump the head along */
	stv = VTAILQ_NEXT(stv_next, list);
	if (stv == NULL)
		stv = VTAILQ_FIRST(&stv_stevedores);
	AN(stv);
	AN(stv->name);
	stv_next = stv;
	return (stv);
}

/*-------------------------------------------------------------------*/

static struct storage *
stv_alloc(struct busyobj *bo, size_t size)
{
	struct storage *st;
	struct stevedore *stv;
	unsigned fail = 0;
	struct object *obj;

	/*
	 * Always use the stevedore which allocated the object in order to
	 * keep an object inside the same stevedore.
	 */
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	obj = bo->fetch_obj;
	CHECK_OBJ_NOTNULL(obj, OBJECT_MAGIC);
	stv = obj->objstore->stevedore;
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

		if (size > cache_param->fetch_chunksize) {
			size >>= 1;
			continue;
		}

		/* no luck; try to free some space and keep trying */
		if (EXP_NukeOne(bo, stv->lru) == -1)
			break;

		/* Enough is enough: try another if we have one */
		if (++fail >= cache_param->nuke_limit)
			break;
	}
	if (st != NULL)
		CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
	return (st);
}


/*-------------------------------------------------------------------*
 * Structure used to transport internal knowledge from STV_NewObject()
 * to STV_MkObject().  Nobody else should mess with this struct.
 */

struct stv_objsecrets {
	unsigned	magic;
#define STV_OBJ_SECRETES_MAGIC	0x78c87247
	uint16_t	nhttp;
	unsigned	lhttp;
	unsigned	wsl;
};

/*--------------------------------------------------------------------
 * This function is called by stevedores ->allocobj() method, which
 * very often will be stv_default_allocobj() below, to convert a slab
 * of storage into object which the stevedore can then register in its
 * internal state, before returning it to STV_NewObject().
 * As you probably guessed: All this for persistence.
 */

struct object *
STV_MkObject(struct stevedore *stv, struct busyobj *bo, struct objcore **ocp,
    void *ptr, unsigned ltot, const struct stv_objsecrets *soc)
{
	struct object *o;
	unsigned l;

	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(soc, STV_OBJ_SECRETES_MAGIC);
	AN(ocp);
	CHECK_OBJ_NOTNULL((*ocp), OBJCORE_MAGIC);

	assert(PAOK(ptr));
	assert(PAOK(soc->wsl));
	assert(PAOK(soc->lhttp));

	assert(ltot >= sizeof *o + soc->lhttp + soc->wsl);

	o = ptr;
	memset(o, 0, sizeof *o);
	o->magic = OBJECT_MAGIC;

	l = PRNDDN(ltot - (sizeof *o + soc->lhttp));
	assert(l >= soc->wsl);

	o->http = HTTP_create(o + 1, soc->nhttp);
	WS_Init(o->ws_o, "obj", (char *)(o + 1) + soc->lhttp, soc->wsl);
	WS_Assert(o->ws_o);
	assert(o->ws_o->e <= (char*)ptr + ltot);

	HTTP_Setup(o->http, o->ws_o, bo->vsl, HTTP_Obj);
	o->http->magic = HTTP_MAGIC;
	o->exp = bo->exp;
	VTAILQ_INIT(&o->store);
	bo->stats->n_object++;

	o->objcore = *ocp;
	*ocp = NULL;     /* refcnt follows pointer. */
	if (o->objcore->objhead != NULL)
		BAN_NewObjCore(o->objcore);

	o->objcore->methods = &default_oc_methods;
	o->objcore->priv = o;
	o->objcore->priv2 = (uintptr_t)stv;
	return (o);
}

/*--------------------------------------------------------------------
 * This is the default ->allocobj() which all stevedores who do not
 * implement persistent storage can rely on.
 */

struct object *
stv_default_allocobj(struct stevedore *stv, struct busyobj *bo,
    struct objcore **ocp, unsigned ltot, const struct stv_objsecrets *soc)
{
	struct object *o;
	struct storage *st;

	CHECK_OBJ_NOTNULL(soc, STV_OBJ_SECRETES_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	AN(ocp);
	st = stv->alloc(stv, ltot);
	if (st == NULL)
		return (NULL);
	if (st->space < ltot) {
		stv->free(st);
		return (NULL);
	}
	ltot = st->len = st->space;
	o = STV_MkObject(stv, bo, ocp, st->ptr, ltot, soc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	o->objstore = st;
	return (o);
}

/*-------------------------------------------------------------------
 * Allocate storage for an object, based on the header information.
 * XXX: If we know (a hint of) the length, we could allocate space
 * XXX: for the body in the same allocation while we are at it.
 */

struct object *
STV_NewObject(struct busyobj *bo, struct objcore **ocp, const char *hint,
    unsigned wsl, uint16_t nhttp)
{
	struct object *o;
	struct stevedore *stv, *stv0;
	unsigned lhttp, ltot;
	struct stv_objsecrets soc;
	int i;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	AN(ocp);
	assert(wsl > 0);
	wsl = PRNDUP(wsl);

	lhttp = HTTP_estimate(nhttp);
	lhttp = PRNDUP(lhttp);

	memset(&soc, 0, sizeof soc);
	soc.magic = STV_OBJ_SECRETES_MAGIC;
	soc.nhttp = nhttp;
	soc.lhttp = lhttp;
	soc.wsl = wsl;

	ltot = sizeof *o + wsl + lhttp;

	stv = stv0 = stv_pick_stevedore(bo->vsl, &hint);
	AN(stv->allocobj);
	o = stv->allocobj(stv, bo, ocp, ltot, &soc);
	if (o == NULL && hint == NULL) {
		do {
			stv = stv_pick_stevedore(bo->vsl, &hint);
			AN(stv->allocobj);
			o = stv->allocobj(stv, bo, ocp, ltot, &soc);
		} while (o == NULL && stv != stv0);
	}
	if (o == NULL) {
		/* no luck; try to free some space and keep trying */
		for (i = 0; o == NULL && i < cache_param->nuke_limit; i++) {
			if (EXP_NukeOne(bo, stv->lru) == -1)
				break;
			o = stv->allocobj(stv, bo, ocp, ltot, &soc);
		}
	}

	if (o == NULL) {
		AN(*ocp);
		return (NULL);
	}
	AZ(*ocp);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	CHECK_OBJ_NOTNULL(o->objstore, STORAGE_MAGIC);
	return (o);
}

/*-------------------------------------------------------------------*/

void
STV_Freestore(struct object *o)
{
	struct storage *st, *stn;

	if (o->esidata != NULL) {
		STV_free(o->esidata);
		o->esidata = NULL;
	}
	VTAILQ_FOREACH_SAFE(st, &o->store, list, stn) {
		CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
		VTAILQ_REMOVE(&o->store, st, list);
		STV_free(st);
	}
}

/*-------------------------------------------------------------------*/

struct storage *
STV_alloc(struct busyobj *bo, size_t size)
{
	return (stv_alloc(bo, size));
}

void
STV_trim(struct storage *st, size_t size, int move_ok)
{

	CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
	AN(st->stevedore);
	if (st->stevedore->trim)
		st->stevedore->trim(st, size, move_ok);
}

/*
 * Duplicate the object storage (HTML body) from src into target, using a
 * stevedore-specific dup method for src's stevedore.
 *
 * Currently, every method just copies storage from one object to the other,
 * but this method of encapsulation opens the path to future techniques of
 * sharing storage together with reference counting.
 */
void
STV_dup(struct busyobj *bo, struct object *src, struct object *target)
{
        struct stevedore *stv;

        CHECK_OBJ_NOTNULL(src, OBJECT_MAGIC);
        CHECK_OBJ_NOTNULL(target, OBJECT_MAGIC);
        CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
        CHECK_OBJ_NOTNULL(bo->fetch_obj, OBJECT_MAGIC);
        stv = src->objstore->stevedore;
        CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);

        AN(stv->dup);
        
        stv->dup(bo, src, target);
}

void
STV_free(struct storage *st)
{

	CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
	AN(st->stevedore);
	AN(st->stevedore->free);
	st->stevedore->free(st);
}

void
STV_open(void)
{
	struct stevedore *stv;

	VTAILQ_FOREACH(stv, &stv_stevedores, list) {
		stv->lru = LRU_Alloc();
		if (stv->open != NULL)
			stv->open(stv);
	}
	stv = stv_transient;
	if (stv->open != NULL) {
		stv->lru = LRU_Alloc();
		stv->open(stv);
	}
	stv_next = VTAILQ_FIRST(&stv_stevedores);
}

void
STV_close(void)
{
	struct stevedore *stv;

	VTAILQ_FOREACH(stv, &stv_stevedores, list)
		if (stv->close != NULL)
			stv->close(stv);
	stv = stv_transient;
	if (stv->close != NULL)
		stv->close(stv);
}


/*--------------------------------------------------------------------
 * VRT functions for stevedores
 */

static const struct stevedore *
stv_find(const char *nm)
{
	const struct stevedore *stv;

	VTAILQ_FOREACH(stv, &stv_stevedores, list)
		if (!strcmp(stv->ident, nm))
			return (stv);
	if (!strcmp(TRANSIENT_STORAGE, nm))
		return (stv_transient);
	return (NULL);
}

int
VRT_Stv(const char *nm)
{

	if (stv_find(nm) != NULL)
		return (1);
	return (0);
}

#define VRTSTVVAR(nm, vtype, ctype, dval)	\
ctype						\
VRT_Stv_##nm(const char *nm)			\
{						\
	const struct stevedore *stv;		\
						\
	stv = stv_find(nm);			\
	if (stv == NULL)			\
		return (dval);			\
	if (stv->var_##nm == NULL)		\
		return (dval);			\
	return (stv->var_##nm(stv));		\
}

#include "tbl/vrt_stv_var.h"
#undef VRTSTVVAR

/*
 * Default object store dup just copies the storage.
 */
void
default_dup(struct busyobj *bo, struct object *src, struct object *target)
{
        struct storage *st, *st2;
        unsigned cl;
	/*struct stevedore *stv = target->objstore->stevedore;

	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
	AN(stv->alloc);*/
        
        CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
        CHECK_OBJ_NOTNULL(src, OBJECT_MAGIC);
        CHECK_OBJ_NOTNULL(target, OBJECT_MAGIC);

	/*
	 * XXX: This should use the logic of STV_alloc() instead of just
	 * stv->alloc, to check if space is exhausted, nuke if necessary,
	 * etc. This is *not* safe; it risks running out of storage with
	 * no recovery. But STV_alloc() now assumes that allocs are only
	 * for busyobj->fetchobj.
	 */
        AN(target->len); /* XXX: !!! */
        VTAILQ_FOREACH(st2, &src->store, list) {
		cl = st2->len;
		/*st = stv->alloc(stv, cl);*/
                st = STV_alloc(bo, cl);
		XXXAN(st);
                assert(st->space >= cl);
		VTAILQ_INSERT_TAIL(&target->store, st, list);
		st->len = cl;
		target->len += cl;
		memcpy(st->ptr, st2->ptr, cl);
	}
}
