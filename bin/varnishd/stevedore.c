/*-
 * Copyright (c) 2007-2009 Linpro AS
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
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "cache.h"
#include "stevedore.h"

static VTAILQ_HEAD(, stevedore)	stevedores =
    VTAILQ_HEAD_INITIALIZER(stevedores);

static const struct stevedore * volatile stv_next;

/*********************************************************************
 * NB! Dirty trick alert:
 *
 * We use a captive objcore as tail senteniel for LRU lists, but to
 * make sure it does not get into play by accident, we do _not_ 
 * initialize its magic with OBJCORE_MAGIC.
 *
 */

struct lru *
LRU_Alloc(void)
{
	struct lru *l;

	ALLOC_OBJ(l, LRU_MAGIC);
	AN(l);
	VLIST_INIT(&l->lru_head);
	VLIST_INSERT_HEAD(&l->lru_head, &l->senteniel, lru_list);
	return (l);
}

/*********************************************************************
 * XXX: trust pointer writes to be atomic
 */

static struct stevedore *
stv_pick_stevedore(void)
{
	struct stevedore *stv;

	/* pick a stevedore and bump the head along */
	stv = VTAILQ_NEXT(stv_next, list);
	if (stv == NULL)
		stv = VTAILQ_FIRST(&stevedores);
	AN(stv);
	AN(stv->name);
	stv_next = stv;
	return (stv);
}


/*********************************************************************/

static void
STV_InitObj(struct sess *sp, struct object *o, unsigned wsl)
{

	memset(o, 0, sizeof *o);
	o->magic = OBJECT_MAGIC;

	WS_Init(o->ws_o, "obj", (o + 1), wsl);
	WS_Assert(o->ws_o);

	http_Setup(o->http, o->ws_o);
	o->http->magic = HTTP_MAGIC;
	o->refcnt = 1;
	o->grace = NAN;
	o->entered = NAN;
	VTAILQ_INIT(&o->store);
	VTAILQ_INIT(&o->esibits);
	sp->wrk->stats.n_object++;
}

/*********************************************************************/

struct object *
STV_NewObject(struct sess *sp, unsigned l, double ttl)
{
	struct object *o;
	struct storage *st;

	(void)ttl;
	if (l == 0)
		l = 1024;
	if (params->obj_workspace > 0 && params->obj_workspace > l)
		l =  params->obj_workspace;

	if (!sp->wrk->cacheable) {
		o = malloc(sizeof *o + l);
		XXXAN(o);
		STV_InitObj(sp, o, l);
		return (o);
	} 
	st = STV_alloc(sp, sizeof *o + l);
	XXXAN(st);
	xxxassert(st->space >= (sizeof *o + l));

	st->len = st->space;

	o = (void *)st->ptr; /* XXX: align ? */

	STV_InitObj(sp, o, st->space - sizeof *o);
	o->objstore = st;
	return (o);
}

/*********************************************************************/

struct storage *
STV_alloc(struct sess *sp, size_t size)
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
		if (sp->obj->objstore != NULL) {
			stv = sp->obj->objstore->stevedore;
			CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
		}
	}

	for (;;) {
		if (stv == NULL) {
			stv = stv_pick_stevedore();
			fail = 0;
		}

		/* try to allocate from it */
		AN(stv->alloc);
		st = stv->alloc(stv, size);
		if (st != NULL)
			break;

		/* no luck; try to free some space and keep trying */
		if (EXP_NukeOne(sp, stv->lru) == -1)
			break;

		/* Enough is enough: try another if we have one */
		if (++fail == 50)
			stv = NULL;
	}
	CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
	return (st);
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
STV_add(const struct stevedore *stv2, int ac, char * const *av)
{
	struct stevedore *stv;

	CHECK_OBJ_NOTNULL(stv2, STEVEDORE_MAGIC);
	ALLOC_OBJ(stv, STEVEDORE_MAGIC);
	AN(stv);

	*stv = *stv2;
	AN(stv->name);
	AN(stv->alloc);
	stv->lru = LRU_Alloc();

	if (stv->init != NULL)
		stv->init(stv, ac, av);
	else if (ac != 0)
		ARGV_ERR("(-s%s) too many arguments\n", stv->name);

	VTAILQ_INSERT_TAIL(&stevedores, stv, list);

	if (!stv_next)
		stv_next = VTAILQ_FIRST(&stevedores);
}

void
STV_open(void)
{
	struct stevedore *stv;

	VTAILQ_FOREACH(stv, &stevedores, list) {
		if (stv->open != NULL)
			stv->open(stv);
	}
}

void
STV_close(void)
{
	struct stevedore *stv;

	VTAILQ_FOREACH(stv, &stevedores, list) {
		if (stv->close != NULL)
			stv->close(stv);
	}
}

struct lru *
STV_lru(const struct storage *st)
{
	CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);

	return (st->stevedore->lru);
}

const struct choice STV_choice[] = {
	{ "file",	&smf_stevedore },
	{ "malloc",	&sma_stevedore },
	{ "persistent",	&smp_stevedore },
#ifdef HAVE_LIBUMEM
	{ "umem",	&smu_stevedore },
#endif
	{ NULL,		NULL }
};
