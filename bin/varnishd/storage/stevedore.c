/*-
 * Copyright (c) 2007-2014 Varnish Software AS
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

static struct object * __match_proto__(getobj_f)
default_oc_getobj(struct dstat *ds, struct objcore *oc)
{
	struct object *o;

	(void)ds;
	if (oc->stobj->priv == NULL)
		return (NULL);
	CAST_OBJ_NOTNULL(o, oc->stobj->priv, OBJECT_MAGIC);
	return (o);
}

static void __match_proto__(freeobj_f)
default_oc_freeobj(struct dstat *ds, struct objcore *oc)
{
	struct object *o;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AN(ds);
	ObjSlim(oc, ds);
	CAST_OBJ_NOTNULL(o, oc->stobj->priv, OBJECT_MAGIC);
	oc->stobj->priv = NULL;
	oc->stobj->stevedore = NULL;
	o->magic = 0;

	STV_free(o->objstore);

	ds->n_object--;
}

static struct lru * __match_proto__(getlru_f)
default_oc_getlru(const struct objcore *oc)
{
	struct stevedore *stv;

	CAST_OBJ_NOTNULL(stv, (void *)oc->stobj->priv2, STEVEDORE_MAGIC);
	return (stv->lru);
}

const struct storeobj_methods default_oc_methods = {
	.getobj = default_oc_getobj,
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

struct storage *
STV_alloc(struct stevedore *stv, size_t size)
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

/*--------------------------------------------------------------------
 * This function is called by stevedores ->allocobj() method, which
 * very often will be stv_default_allocobj() below, to convert a slab
 * of storage into object which the stevedore can then register in its
 * internal state, before returning it to STV_NewObject().
 * As you probably guessed: All this for persistence.
 */

struct object *
STV_MkObject(struct stevedore *stv, struct objcore *oc, void *ptr)
{
	struct object *o;

	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	assert(PAOK(ptr));

	o = ptr;
	memset(o, 0, sizeof *o);
	o->magic = OBJECT_MAGIC;

	VTAILQ_INIT(&o->body->list);

	oc->stobj->magic = STOREOBJ_MAGIC;
	oc->stobj->stevedore = stv;
	o->body->stevedore = stv;
	AN(stv->methods);
	oc->stobj->priv = o;
	oc->stobj->priv2 = (uintptr_t)stv;
	return (o);
}

/*--------------------------------------------------------------------
 * This is the default ->allocobj() which all stevedores who do not
 * implement persistent storage can rely on.
 */

int
stv_default_allocobj(struct stevedore *stv, struct objcore *oc, unsigned ltot)
{
	struct object *o;
	struct storage *st;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	st = stv->alloc(stv, ltot);
	if (st == NULL)
		return (0);
	if (st->space < ltot) {
		stv->free(st);
		return (0);
	}
	o = STV_MkObject(stv, oc, st->ptr);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	st->len = sizeof(*o);
	o->objstore = st;
	return (1);
}

/*-------------------------------------------------------------------
 * Allocate storage for an object, based on the header information.
 * XXX: If we know (a hint of) the length, we could allocate space
 * XXX: for the body in the same allocation while we are at it.
 */

int
STV_NewObject(struct busyobj *bo, const char *hint, unsigned wsl)
{
	struct objcore *oc;
	struct stevedore *stv, *stv0;
	unsigned ltot;
	int i, j;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	oc = bo->fetch_objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	assert(wsl > 0);
	wsl = PRNDUP(wsl);

	ltot = sizeof(struct object) + wsl;

	stv = stv0 = stv_pick_stevedore(bo->vsl, &hint);
	AN(stv->allocobj);
	j = stv->allocobj(stv, oc, ltot);
	if (j == 0 && hint == NULL) {
		do {
			stv = stv_pick_stevedore(bo->vsl, &hint);
			AN(stv->allocobj);
			j = stv->allocobj(stv, oc, ltot);
		} while (j == 0 && stv != stv0);
	}
	if (j == 0) {
		/* no luck; try to free some space and keep trying */
		for (i = 0; j == 0 && i < cache_param->nuke_limit; i++) {
			if (EXP_NukeOne(bo->vsl, bo->stats, stv->lru) == -1)
				break;
			j = stv->allocobj(stv, oc, ltot);
		}
	}

	if (j == 0)
		return (0);

	bo->stats->n_object++;
	VSLb(bo->vsl, SLT_Storage, "%s %s",
	    oc->stobj->stevedore->name, oc->stobj->stevedore->ident);
	return (1);
}

/*-------------------------------------------------------------------*/

struct storage *
STV_alloc_transient(size_t size)
{

	return (STV_alloc(stv_transient, size));
}

void
STV_trim(struct storage *st, size_t size, int move_ok)
{

	CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
	AN(st->stevedore);
	if (st->stevedore->trim)
		st->stevedore->trim(st, size, move_ok);
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

	/* Signal intent to close */
	VTAILQ_FOREACH(stv, &stv_stevedores, list)
		if (stv->signal_close != NULL)
			stv->signal_close(stv);
	stv = stv_transient;
	if (stv->signal_close != NULL)
		stv->signal_close(stv);

	/* Close each in turn */
	VTAILQ_FOREACH(stv, &stv_stevedores, list)
		if (stv->close != NULL)
			stv->close(stv);
	stv = stv_transient;
	if (stv->close != NULL)
		stv->close(stv);
}

/*-------------------------------------------------------------------
 * Notify the stevedores of BAN related events. A non-zero return
 * value indicates that the stevedore is unable to persist the
 * event.
 */

int
STV_BanInfo(enum baninfo event, const uint8_t *ban, unsigned len)
{
	struct stevedore *stv;
	int r = 0;

	VTAILQ_FOREACH(stv, &stv_stevedores, list)
		if (stv->baninfo != NULL)
			r |= stv->baninfo(stv, event, ban, len);

	return (r);
}

/*-------------------------------------------------------------------
 * Export a complete ban list to the stevedores for persistence.
 * The stevedores should clear any previous ban lists and replace
 * them with this list.
 */

void
STV_BanExport(const uint8_t *bans, unsigned len)
{
	struct stevedore *stv;

	VTAILQ_FOREACH(stv, &stv_stevedores, list)
		if (stv->banexport != NULL)
			stv->banexport(stv, bans, len);
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
