/*-
 * Copyright (c) 2007-2010 Redpill Linpro AS
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
#include <string.h>
#include <math.h>

#include "cache.h"
#include "stevedore.h"
#include "hash_slinger.h"
#include "cli_priv.h"
#include "vrt_obj.h"

static VTAILQ_HEAD(, stevedore)	stevedores =
    VTAILQ_HEAD_INITIALIZER(stevedores);

static const struct stevedore * volatile stv_next;

static struct stevedore *stv_transient;

/*---------------------------------------------------------------------
 * Default objcore methods
 */

static struct object * __match_proto__(getobj_f)
default_oc_getobj(struct worker *wrk, struct objcore *oc)
{
	struct object *o;

	(void)wrk;
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
	struct object *o;

	CAST_OBJ_NOTNULL(o, oc->priv, OBJECT_MAGIC);
	return (o->objstore->stevedore->lru);
}

static struct objcore_methods default_oc_methods = {
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
stv_pick_stevedore(const char *hint)
{
	struct stevedore *stv;

	if (hint != NULL && *hint != '\0') {
		VTAILQ_FOREACH(stv, &stevedores, list) {
			if (!strcmp(stv->ident, hint))
				return (stv);
		}
		if (!strcmp(TRANSIENT_STORAGE, hint))
			return (stv_transient);
	}
	/* pick a stevedore and bump the head along */
	stv = VTAILQ_NEXT(stv_next, list);
	if (stv == NULL)
		stv = VTAILQ_FIRST(&stevedores);
	AN(stv);
	AN(stv->name);
	stv_next = stv;
	return (stv);
}

/*-------------------------------------------------------------------*/

static struct storage *
stv_alloc(const struct sess *sp, size_t size)
{
	struct storage *st;
	struct stevedore *stv = NULL;
	unsigned fail = 0;

	/*
	 * Always try the stevedore which allocated the object in order to
	 * not needlessly split an object across multiple stevedores.
	 */
	if (sp->obj != NULL) {
		CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);
		stv = sp->obj->objstore->stevedore;
	} else {
		INCOMPL();
		stv = stv_transient;
	}
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);

	if (size > (size_t)(params->fetch_maxchunksize) << 10)
		size = (size_t)(params->fetch_maxchunksize) << 10;

	for (;;) {
		/* try to allocate from it */
		AN(stv->alloc);
		st = stv->alloc(stv, size);
		if (st != NULL)
			break;

		if (size > params->fetch_chunksize) {
			size >>= 1;
			continue;
		}

		/* no luck; try to free some space and keep trying */
		if (EXP_NukeOne(sp, stv->lru) == -1)
			break;

		/* Enough is enough: try another if we have one */
		if (++fail == 50)	/* XXX Param */
			break;
	}
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
	unsigned	nhttp;
	unsigned	lhttp;
	unsigned	wsl;
	struct exp	*exp;
};

/*--------------------------------------------------------------------
 * This function is called by stevedores ->allocobj() method, which
 * very often will be stv_default_allocobj() below, to convert a slab
 * of storage into object which the stevedore can then register in its
 * internal state, before returning it to STV_NewObject().
 * As you probably guessed: All this for persistence.
 */

struct object *
STV_MkObject(struct sess *sp, void *ptr, unsigned ltot,
    const struct stv_objsecrets *soc)
{
	struct object *o;
	unsigned l;

	CHECK_OBJ_NOTNULL(soc, STV_OBJ_SECRETES_MAGIC);

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

	http_Setup(o->http, o->ws_o);
	o->http->magic = HTTP_MAGIC;
	o->entered = NAN;
	o->exp = *soc->exp;
	VTAILQ_INIT(&o->store);
	sp->wrk->stats.n_object++;

	if (sp->objcore != NULL) {
		CHECK_OBJ_NOTNULL(sp->objcore, OBJCORE_MAGIC);

		o->objcore = sp->objcore;
		sp->objcore = NULL;     /* refcnt follows pointer. */
		BAN_NewObjCore(o->objcore);

		o->objcore->methods = &default_oc_methods;
		o->objcore->priv = o;
	}
	return (o);
}

/*--------------------------------------------------------------------
 * This is the default ->allocobj() which all stevedores who do not
 * implement persistent storage can rely on.
 */

static struct object *
stv_default_allocobj(struct stevedore *stv, struct sess *sp, unsigned ltot,
    const struct stv_objsecrets *soc)
{
	struct object *o;
	struct storage *st;

	CHECK_OBJ_NOTNULL(soc, STV_OBJ_SECRETES_MAGIC);
	st = stv->alloc(stv, ltot);
	if (st == NULL)
		return (NULL);
	if (st->space < ltot) {
		stv->free(st);
		return (NULL);
	}
	ltot = st->len = st->space;
	o = STV_MkObject(sp, st->ptr, ltot, soc);
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
STV_NewObject(struct sess *sp, const char *hint, unsigned wsl, struct exp *ep,
    unsigned nhttp)
{
	struct object *o;
	struct stevedore *stv;
	unsigned lhttp, ltot;
	struct stv_objsecrets soc;

	assert(wsl > 0);
	wsl = PRNDUP(wsl);

	lhttp = HTTP_estimate(nhttp);
	lhttp = PRNDUP(lhttp);

	memset(&soc, 0, sizeof soc);
	soc.magic = STV_OBJ_SECRETES_MAGIC;
	soc.nhttp = nhttp;
	soc.lhttp = lhttp;
	soc.wsl = wsl;
	soc.exp = ep;

	ltot = sizeof *o + wsl + lhttp;

	stv = stv_pick_stevedore(hint);
	AN(stv->allocobj);
	o = stv->allocobj(stv, sp, ltot, &soc);
	if (o == NULL)
		return (NULL);
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
STV_alloc(const struct sess *sp, size_t size)
{

	return (stv_alloc(sp, size));
}

void
STV_trim(struct storage *st, size_t size)
{

	CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
	AN(st->stevedore);
	if (st->stevedore->trim)
		st->stevedore->trim(st, size);
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

	VTAILQ_FOREACH(stv, &stevedores, list) {
		stv->lru = LRU_Alloc();
		if (stv->open != NULL)
			stv->open(stv);
	}
	stv = stv_transient;
	if (stv->open != NULL) {
		stv->lru = LRU_Alloc();
		stv->open(stv);
	}
}

void
STV_close(void)
{
	struct stevedore *stv;

	VTAILQ_FOREACH(stv, &stevedores, list)
		if (stv->close != NULL)
			stv->close(stv);
	stv = stv_transient;
	if (stv->close != NULL)
		stv->close(stv);
}

/*--------------------------------------------------------------------
 * Parse a stevedore argument on the form:
 *	[ name '=' ] strategy [ ',' arg ] *
 */

static const struct choice STV_choice[] = {
	{ "file",	&smf_stevedore },
	{ "malloc",	&sma_stevedore },
	{ "persistent",	&smp_stevedore },
#ifdef HAVE_LIBUMEM
	{ "umem",	&smu_stevedore },
#endif
	{ NULL,		NULL }
};

void
STV_Config(const char *spec)
{
	char **av;
	const char *p, *q;
	struct stevedore *stv;
	const struct stevedore *stv2;
	int ac, l;
	static unsigned seq = 0;

	ASSERT_MGT();
	p = strchr(spec, '=');
	q = strchr(spec, ',');
	if (p != NULL && (q == NULL || q > p)) {
		av = ParseArgv(p + 1, ARGV_COMMA);
	} else {
		av = ParseArgv(spec, ARGV_COMMA);
		p = NULL;
	}
	AN(av);

	if (av[0] != NULL)
		ARGV_ERR("%s\n", av[0]);

	if (av[1] == NULL)
		ARGV_ERR("-s argument lacks strategy {malloc, file, ...}\n");

	for (ac = 0; av[ac + 2] != NULL; ac++)
		continue;

	stv2 = pick(STV_choice, av[1], "storage");
	AN(stv2);

	/* Append strategy to ident string */
	vsb_printf(vident, ",-s%s", av[1]);

	av += 2;

	CHECK_OBJ_NOTNULL(stv2, STEVEDORE_MAGIC);
	ALLOC_OBJ(stv, STEVEDORE_MAGIC);
	AN(stv);

	*stv = *stv2;
	AN(stv->name);
	AN(stv->alloc);
	if (stv->allocobj == NULL)
		stv->allocobj = stv_default_allocobj;

	if (p == NULL)
		bprintf(stv->ident, "s%u", seq++);
	else {
		l = p - spec;
		if (l > sizeof stv->ident - 1)
			l = sizeof stv->ident - 1;
		bprintf(stv->ident, "%.*s", l, spec);
	}

	VTAILQ_FOREACH(stv2, &stevedores, list) {
		if (strcmp(stv2->ident, stv->ident))
			continue;
		ARGV_ERR("(-s%s=%s) already defined once\n",
		    stv->ident, stv->name);
	}

	if (stv->init != NULL)
		stv->init(stv, ac, av);
	else if (ac != 0)
		ARGV_ERR("(-s%s) too many arguments\n", stv->name);

	if (!strcmp(stv->ident, TRANSIENT_STORAGE)) {
		stv->transient = 1;
		AZ(stv_transient);
		stv_transient = stv;
	} else {
		VTAILQ_INSERT_TAIL(&stevedores, stv, list);
		if (!stv_next)
			stv_next = VTAILQ_FIRST(&stevedores);
	}
}

/*--------------------------------------------------------------------*/

void
STV_Config_Transient(void)
{
	const struct stevedore *stv;

	ASSERT_MGT();
	VTAILQ_FOREACH(stv, &stevedores, list)
		if (!strcmp(stv->ident, TRANSIENT_STORAGE))
			return;
	STV_Config(TRANSIENT_STORAGE "=malloc");
}

/*--------------------------------------------------------------------*/

static void
stv_cli_list(struct cli *cli, const char * const *av, void *priv)
{
	struct stevedore *stv;

	ASSERT_MGT();
	(void)av;
	(void)priv;
	cli_out(cli, "Storage devices:\n");
	VTAILQ_FOREACH(stv, &stevedores, list)
		cli_out(cli, "\tstorage.%s = %s\n", stv->ident, stv->name);
}

/*--------------------------------------------------------------------*/

struct cli_proto cli_stv[] = {
	{ "storage.list", "storage.list", "List storage devices\n",
	    0, 0, "", stv_cli_list },
	{ NULL}
};

/*--------------------------------------------------------------------
 * VRT functions for stevedores
 */

#include "vrt.h"

static const struct stevedore *
stv_find(const char *nm)
{
	const struct stevedore *stv;

	VTAILQ_FOREACH(stv, &stevedores, list)
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

#include "vrt_stv_var.h"
#undef VRTSTVVAR
