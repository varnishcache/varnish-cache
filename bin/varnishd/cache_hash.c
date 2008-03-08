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
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>

#include "shmlog.h"
#include "heritage.h"
#include "cache.h"

static struct hash_slinger      *hash;

/* Precreate an objhead and object for later use */
void
HSH_Prealloc(struct sess *sp)
{
	struct worker *w;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	w = sp->wrk;

	if (w->nobjhead == NULL) {
		w->nobjhead = calloc(sizeof *w->nobjhead, 1);
		XXXAN(w->nobjhead);
		w->nobjhead->magic = OBJHEAD_MAGIC;
		VTAILQ_INIT(&w->nobjhead->objects);
		MTX_INIT(&w->nobjhead->mtx);
		VSL_stats->n_objecthead++;
	} else
		CHECK_OBJ_NOTNULL(w->nobjhead, OBJHEAD_MAGIC);
	if (w->nobj == NULL) {
		w->nobj = calloc(sizeof *w->nobj, 1);
		XXXAN(w->nobj);
		w->nobj->magic = OBJECT_MAGIC;
		w->nobj->http.magic = HTTP_MAGIC;
		w->nobj->busy = 1;
		w->nobj->refcnt = 1;
		VTAILQ_INIT(&w->nobj->store);
		VTAILQ_INIT(&w->nobj->waitinglist);
		VSL_stats->n_object++;
	} else
		CHECK_OBJ_NOTNULL(w->nobj, OBJECT_MAGIC);
}

static void
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
HSH_Compare(struct sess *sp, const char *b, const char *e)
{
	int i;
	unsigned u, v;

	i = sp->lhashptr - (e - b);
	if (i)
		return (i);
	for (u = 0; u < sp->ihashptr; u += 2) {
		v = sp->hashptr[u + 1] - sp->hashptr[u];
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
	assert(b == e);
	return (0);
}

void
HSH_Copy(struct sess *sp, char *b, const char *e)
{
	unsigned u, v;

	assert((e - b) >= sp->lhashptr);
	for (u = 0; u < sp->ihashptr; u += 2) {
		v = sp->hashptr[u + 1] - sp->hashptr[u];
		memcpy(b, sp->hashptr[u], v);
		b += v;
		*b++ = '#';
	}
	*b++ = '\0';
	assert(b <= e);
}

struct object *
HSH_Lookup(struct sess *sp)
{
	struct worker *w;
	struct http *h;
	struct objhead *oh;
	struct object *o;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(sp->http, HTTP_MAGIC);
	AN(hash);
	w = sp->wrk;
	h = sp->http;

	HSH_Prealloc(sp);
	if (sp->obj != NULL) {
		CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);
		o = sp->obj;
		oh = o->objhead;
		CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
		LOCK(&oh->mtx);
		goto were_back;
	}

	oh = hash->lookup(sp, w->nobjhead);
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	if (oh == w->nobjhead)
		w->nobjhead = NULL;
	LOCK(&oh->mtx);
	VTAILQ_FOREACH(o, &oh->objects, list) {
		o->refcnt++;
		if (o->busy) {
			VTAILQ_INSERT_TAIL(&o->waitinglist, sp, list);
			sp->obj = o;
			UNLOCK(&oh->mtx);
			return (NULL);
		}
	were_back:
		if (!o->cacheable) {
			/* ignore */
		} else if (o->ttl == 0) {
			/* Object banned but not reaped yet */
		} else if (o->ttl <= sp->t_req) {
			/* Object expired */
		} else if (BAN_CheckObject(o, h->hd[HTTP_HDR_URL].b)) {
			o->ttl = 0;
			VSL(SLT_ExpBan, 0, "%u was banned", o->xid);
			if (o->heap_idx != 0)
				EXP_TTLchange(o);
		} else if (o->vary == NULL || VRY_Match(sp, o->vary))
			break;
		o->refcnt--;
	}
	if (o != NULL) {
		UNLOCK(&oh->mtx);
		(void)hash->deref(oh);
		LRU_Enter(o, sp->t_req);
		return (o);
	}

	/* Insert (precreated) object in objecthead */
	o = w->nobj;
	w->nobj = NULL;
	o->objhead = oh;
	VTAILQ_INSERT_TAIL(&oh->objects, o, list);
	/* NB: do not deref objhead the new object inherits our reference */
	UNLOCK(&oh->mtx);
	BAN_NewObj(o);
	LRU_Enter(o, sp->t_req);
	return (o);
}

void
HSH_Unbusy(struct object *o)
{
	struct objhead *oh;
	struct sess *sp;

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	assert(o->busy);
	assert(o->refcnt > 0);
	if (o->cacheable)
		EXP_Insert(o);
	oh = o->objhead;
	if (oh != NULL) {
		CHECK_OBJ(oh, OBJHEAD_MAGIC);
		LOCK(&oh->mtx);
	}
	o->busy = 0;
	if (oh != NULL)
		UNLOCK(&oh->mtx);
	while (1) {
		sp = VTAILQ_FIRST(&o->waitinglist);
		if (sp == NULL)
			break;
		VTAILQ_REMOVE(&o->waitinglist, sp, list);
		WRK_QueueSession(sp);
	}
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
	if (oh != NULL) {
		if (!r)
			VTAILQ_REMOVE(&oh->objects, o, list);
		UNLOCK(&oh->mtx);
	}

	/* If still referenced, done */
	if (r != 0)
		return;

	if (o->http.ws->s != NULL)
		free(o->http.ws->s);

	if (o->vary != NULL)
		free(o->vary);

	LRU_Remove(o);
	HSH_Freestore(o);
	FREE_OBJ(o);
	VSL_stats->n_object--;

	if (oh == NULL)
		return;
	/* Drop our ref on the objhead */
	if (hash->deref(oh))
		return;
	assert(VTAILQ_EMPTY(&oh->objects));
	MTX_DESTROY(&oh->mtx);
	VSL_stats->n_objecthead--;
	FREE_OBJ(oh);
}

void
HSH_Init(void)
{

	hash = heritage.hash;
	if (hash->start != NULL)
		hash->start();
}
