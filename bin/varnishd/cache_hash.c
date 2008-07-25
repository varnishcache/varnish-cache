/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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
 * $Id$
 *
 * This is the central hash-table code, it relies on a chosen hash
 * implementation only for the actual hashing, all the housekeeping
 * happens here.
 *
 * We have two kinds of structures, objecthead and object.  An objecthead
 * corresponds to a given (Host:, URL) tupple, and the objects hung from
 * the objecthead may represent various variations (ie: Vary: header,
 * different TTL etc) instances of that web-entity.
 *
 * Each objecthead has a mutex which locks both its own fields, the
 * list of objects and fields in the objects.
 *
 * The hash implementation must supply a reference count facility on
 * the objecthead, and return with a reference held after a lookup.
 *
 * Lookups in the hash implementation returns with a ref held and each
 * object hung from the objhead holds a ref as well.
 *
 * Objects have refcounts which are locked by the objecthead mutex.
 *
 * New objects are always marked busy, and they can go from busy to
 * not busy only once.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>

#include "shmlog.h"
#include "cache.h"
#include "stevedore.h"

static struct hash_slinger      *hash;

double
HSH_Grace(double g)
{
	if (isnan(g))
		return (double)(params->default_grace);
	return (g);
}

/* Precreate an objhead and object for later use */
void
HSH_Prealloc(struct sess *sp)
{
	struct worker *w;
	struct storage *st;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	w = sp->wrk;

	if (w->nobjhead == NULL) {
		w->nobjhead = calloc(sizeof *w->nobjhead, 1);
		XXXAN(w->nobjhead);
		w->nobjhead->magic = OBJHEAD_MAGIC;
		VTAILQ_INIT(&w->nobjhead->objects);
		VTAILQ_INIT(&w->nobjhead->waitinglist);
		MTX_INIT(&w->nobjhead->mtx);
		VSL_stats->n_objecthead++;
	} else
		CHECK_OBJ_NOTNULL(w->nobjhead, OBJHEAD_MAGIC);
	if (w->nobj == NULL) {
		st = STV_alloc(sp, params->obj_workspace);
		XXXAN(st);
		assert(st->space > sizeof *w->nobj);
		w->nobj = (void *)st->ptr; /* XXX: align ? */
		st->len = sizeof *w->nobj;
		memset(w->nobj, 0, sizeof *w->nobj);
		w->nobj->objstore = st;
		w->nobj->magic = OBJECT_MAGIC;
		w->nobj->http->magic = HTTP_MAGIC;
		w->nobj->busy = 1;
		w->nobj->refcnt = 1;
		w->nobj->grace = NAN;
		VTAILQ_INIT(&w->nobj->store);
		VTAILQ_INIT(&w->nobj->esibits);
		VSL_stats->n_object++;
	} else
		CHECK_OBJ_NOTNULL(w->nobj, OBJECT_MAGIC);
}

void
HSH_Freestore(struct object *o)
{
	struct storage *st, *stn;

	VTAILQ_FOREACH_SAFE(st, &o->store, list, stn) {
		CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
		VTAILQ_REMOVE(&o->store, st, list);
		STV_free(st);
	}
}

int
HSH_Compare(const struct sess *sp, const struct objhead *obj)
{
	int i;
	unsigned u, v;
	const char *b;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(obj, OBJHEAD_MAGIC);
	i = sp->lhashptr - obj->hashlen;
	if (i)
		return (i);
	b = obj->hash;
	for (u = 0; u < sp->ihashptr; u += 2) {
		v = pdiff(sp->hashptr[u], sp->hashptr[u + 1]);
		i = memcmp(sp->hashptr[u], b, v);
		if (i)
			return (i);
		b += v;
		i = '#' - *b++;
		if (i)
			return (i);
	}
	assert(*b == '\0');
	b++;
	assert(b == obj->hash + obj->hashlen);
	return (0);
}

void
HSH_Copy(const struct sess *sp, const struct objhead *obj)
{
	unsigned u, v;
	char *b;

	assert(obj->hashlen >= sp->lhashptr);
	b = obj->hash;
	for (u = 0; u < sp->ihashptr; u += 2) {
		v = pdiff(sp->hashptr[u], sp->hashptr[u + 1]);
		memcpy(b, sp->hashptr[u], v);
		b += v;
		*b++ = '#';
	}
	*b++ = '\0';
	assert(b <= obj->hash + obj->hashlen);
}

struct object *
HSH_Lookup(struct sess *sp)
{
	struct worker *w;
	struct http *h;
	struct objhead *oh;
	struct object *o, *busy_o, *grace_o;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(sp->http, HTTP_MAGIC);
	AN(hash);
	w = sp->wrk;
	h = sp->http;

	HSH_Prealloc(sp);
	if (sp->objhead != NULL) {
		CHECK_OBJ_NOTNULL(sp->objhead, OBJHEAD_MAGIC);
		oh = sp->objhead;
		sp->objhead = NULL;
		CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
		LOCK(&oh->mtx);
	} else {
		oh = hash->lookup(sp, w->nobjhead);
		CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
		if (oh == w->nobjhead)
			w->nobjhead = NULL;
		LOCK(&oh->mtx);
	}

	busy_o = NULL;
	grace_o = NULL;
	VTAILQ_FOREACH(o, &oh->objects, list) {
		if (o->busy) {
			busy_o = o;
			continue;
		}
		if (!o->cacheable)
			continue;
		if (o->ttl == 0) 
			continue;
		if (BAN_CheckObject(o, h->hd[HTTP_HDR_URL].b, oh->hash)) {
			o->ttl = 0;
			WSP(sp, SLT_ExpBan, "%u was banned", o->xid);
			EXP_Rearm(o);
			continue;
		}
		if (o->vary != NULL && !VRY_Match(sp, o->vary))
			continue;

		/* If still valid, use it */
		if (o->ttl >= sp->t_req)
			break;

		/* Remember any matching objects inside their grace period */
		if (o->ttl + HSH_Grace(o->grace) >= sp->t_req)
			grace_o = o;
	}

	/*
	 * If we have a object in grace and being fetched,
	 * use it, if req.grace is also satisified.
	 */
	if (o == NULL && grace_o != NULL &&
	    grace_o->child != NULL &&
	    grace_o->ttl + HSH_Grace(sp->grace) >= sp->t_req)
		o = grace_o;

	if (o != NULL) {
		/* We found an object we like */
		o->refcnt++;
		UNLOCK(&oh->mtx);
		if (params->log_hash)
			WSP(sp, SLT_Hash, "%s", oh->hash);
		(void)hash->deref(oh);
		return (o);
	}

	if (busy_o != NULL) {
		/* There are one or more busy objects, wait for them */
		VTAILQ_INSERT_TAIL(&oh->waitinglist, sp, list);
		sp->objhead = oh;
		UNLOCK(&oh->mtx);
		return (NULL);
	}

	/* Insert (precreated) object in objecthead */
	o = w->nobj;
	w->nobj = NULL;
	o->objhead = oh;
	/* XXX: Should this not be ..._HEAD now ? */
	VTAILQ_INSERT_TAIL(&oh->objects, o, list);
	/* NB: do not deref objhead the new object inherits our reference */
	if (grace_o != NULL) {
		grace_o->child = o;
		o->parent = grace_o;
		grace_o->refcnt++;
	}
	UNLOCK(&oh->mtx);
	if (params->log_hash)
		WSP(sp, SLT_Hash, "%s", oh->hash);
	/*
	 * XXX: This may be too early, relative to pass objects.
	 * XXX: possibly move to when we commit to have it in the cache.
	 */
	BAN_NewObj(o);
	return (o);
}

static void
hsh_rush(struct objhead *oh)
{
	unsigned u;
	struct sess *sp;

	for (u = 0; u < params->rush_exponent; u++) {
		sp = VTAILQ_FIRST(&oh->waitinglist);
		if (sp == NULL)
			return;
		VTAILQ_REMOVE(&oh->waitinglist, sp, list);
		DSL(0x20, SLT_Debug, sp->id, "off waiting list");
		WRK_QueueSession(sp);
	}
}

void
HSH_Unbusy(const struct sess *sp)
{
	struct object *o;
	struct objhead *oh;
	struct object *parent;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	o = sp->obj;
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	assert(o->busy);
	assert(o->refcnt > 0);
	if (o->ws_o->overflow)
		VSL_stats->n_objoverflow++;
	if (params->diag_bitmap & 0x40)
		WSP(sp, SLT_Debug, 
		    "Object %u workspace free %u", o->xid, WS_Free(o->ws_o));
	
	oh = o->objhead;
	if (oh != NULL) {
		CHECK_OBJ(oh, OBJHEAD_MAGIC);
		LOCK(&oh->mtx);
	}
	o->busy = 0;
	if (oh != NULL)
		hsh_rush(oh);
	parent = o->parent;
	o->parent = NULL;
	if (parent != NULL)
		parent->child = NULL;
	if (oh != NULL)
		UNLOCK(&oh->mtx);
	if (parent != NULL)
		HSH_Deref(parent);
}

void
HSH_Ref(struct object *o)
{
	struct objhead *oh;

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	oh = o->objhead;
	if (oh != NULL) {
		CHECK_OBJ(oh, OBJHEAD_MAGIC);
		LOCK(&oh->mtx);
	}
	assert(o->refcnt > 0);
	o->refcnt++;
	if (oh != NULL)
		UNLOCK(&oh->mtx);
}

void
HSH_Deref(struct object *o)
{
	struct objhead *oh;
	unsigned r;

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	oh = o->objhead;
	if (oh != NULL) {
		CHECK_OBJ(oh, OBJHEAD_MAGIC);

		/* drop ref on object */
		LOCK(&oh->mtx);
	}
	assert(o->refcnt > 0);
	r = --o->refcnt;
	if (oh != NULL)
		hsh_rush(oh);
	if (oh != NULL) {
		if (!r)
			VTAILQ_REMOVE(&oh->objects, o, list);
		UNLOCK(&oh->mtx);
	}

	/* If still referenced, done */
	if (r != 0)
		return;

	BAN_DestroyObj(o);
	DSL(0x40, SLT_Debug, 0, "Object %u workspace min free %u",
	    o->xid, WS_Free(o->ws_o));

	if (o->vary != NULL)
		free(o->vary);

	ESI_Destroy(o);
	HSH_Freestore(o);
	STV_free(o->objstore);
	VSL_stats->n_object--;

	if (oh == NULL)
		return;
	/* Drop our ref on the objhead */
	if (hash->deref(oh))
		return;
	assert(VTAILQ_EMPTY(&oh->objects));
	MTX_DESTROY(&oh->mtx);
	VSL_stats->n_objecthead--;
	free(oh->hash);
	FREE_OBJ(oh);
}

void
HSH_Init(void)
{

	hash = heritage.hash;
	if (hash->start != NULL)
		hash->start();
}
