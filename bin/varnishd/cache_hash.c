/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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
#include <limits.h>
#include <sys/types.h>
#include <fcntl.h>

#include "shmlog.h"
#include "cache.h"
#include "stevedore.h"
#include "hash_slinger.h"
#include "vsha256.h"

static const struct hash_slinger *hash;
unsigned	save_hash;

double
HSH_Grace(double g)
{
	if (isnan(g))
		return (double)(params->default_grace);
	return (g);
}

struct object *
HSH_NewObject(struct sess *sp, int transient)
{
	struct object *o;
	struct storage *st;
	void *p;

	if (transient) {
		p = malloc(sizeof *o + params->obj_workspace);
		XXXAN(p);
		o = p;
		p = o + 1;
		memset(o, 0, sizeof *o);
		o->magic = OBJECT_MAGIC;
		WS_Init(o->ws_o, "obj", p, params->obj_workspace);
	} else {
		st = STV_alloc(sp, params->obj_workspace);
		XXXAN(st);
		assert(st->space > sizeof *o);
		o = (void *)st->ptr; /* XXX: align ? */
		st->len = sizeof *o;
		memset(o, 0, sizeof *o);
		o->magic = OBJECT_MAGIC;
		o->objstore = st;
		WS_Init(o->ws_o, "obj",
		    st->ptr + st->len, st->space - st->len);
		st->len = st->space;
	}
	WS_Assert(o->ws_o);
	http_Setup(o->http, o->ws_o);
	o->http->magic = HTTP_MAGIC;
	o->refcnt = 1;
	o->grace = NAN;
	o->entered = NAN;
	VTAILQ_INIT(&o->store);
	VTAILQ_INIT(&o->esibits);
	sp->wrk->stats->n_object++;
	return (o);
}

/* Precreate an objhead and object for later use */
void
HSH_Prealloc(const struct sess *sp)
{
	struct worker *w;
	struct objhead *oh;
	struct objcore *oc;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	w = sp->wrk;

	if (w->nobjcore == NULL) {
		ALLOC_OBJ(oc, OBJCORE_MAGIC);
		XXXAN(oc);
		w->nobjcore = oc;
		oc->flags |= OC_F_BUSY;
	}
	CHECK_OBJ_NOTNULL(w->nobjcore, OBJCORE_MAGIC);

	if (w->nobjhead == NULL) {
		ALLOC_OBJ(oh, OBJHEAD_MAGIC);
		XXXAN(oh);
		oh->refcnt = 1;
		VTAILQ_INIT(&oh->objcs);
		VTAILQ_INIT(&oh->waitinglist);
		Lck_New(&oh->mtx);
		w->nobjhead = oh;
		w->stats->n_objecthead++;
	}
	CHECK_OBJ_NOTNULL(w->nobjhead, OBJHEAD_MAGIC);

}

void
HSH_Cleanup(struct worker *w)
{

	if (w->nobjcore != NULL) {
		FREE_OBJ(w->nobjcore);
		w->nobjcore = NULL;
	}
	if (w->nobjhead != NULL) {
		Lck_Delete(&w->nobjhead->mtx);
		FREE_OBJ(w->nobjhead);
		w->nobjhead = NULL;
		w->stats->n_objecthead--;
	}
}

void
HSH_DeleteObjHead(const struct worker *w, struct objhead *oh)
{

	AZ(oh->refcnt);
	assert(VTAILQ_EMPTY(&oh->objcs));
	Lck_Delete(&oh->mtx);
	w->stats->n_objecthead--;
	free(oh->hash);
	FREE_OBJ(oh);
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

void
HSH_Copy(const struct sess *sp, struct objhead *oh)
{
	unsigned u, v;
	char *b;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	if (!save_hash)
		return;

	oh->hash = malloc(sp->lhashptr);
	XXXAN(oh->hash);
	b = oh->hash;
	for (u = 0; u < sp->ihashptr; u += 2) {
		v = pdiff(sp->hashptr[u], sp->hashptr[u + 1]);
		memcpy(b, sp->hashptr[u], v);
		b += v;
		*b++ = '#';
	}
	*b++ = '\0';
	assert(b <= oh->hash + sp->lhashptr);
}

void
HSH_Prepare(struct sess *sp, unsigned nhashcount)
{
	char *p;
	unsigned u;

	SHA256_Init(sp->wrk->sha256ctx);
	if (!save_hash)
		return;

	/* Allocate the pointers we need, align properly. */
	sp->lhashptr = 1;       /* space for NUL */
	sp->ihashptr = 0;
	sp->nhashptr = nhashcount * 2;
	p = WS_Alloc(sp->http->ws, sizeof(const char *) * (sp->nhashptr + 1));
	XXXAN(p);
	/* Align pointer properly (?) */
	u = (uintptr_t)p;
	u &= sizeof(const char *) - 1;
	if (u)
		p += sizeof(const char *) - u;
	sp->hashptr = (void*)p;
}

void
HSH_AddString(struct sess *sp, const char *str)
{
	int l;

	if (str == NULL)
		str = "";
	l = strlen(str);

	SHA256_Update(sp->wrk->sha256ctx, str, l);
	SHA256_Update(sp->wrk->sha256ctx, "#", 1);

	if (params->log_hash)
		WSP(sp, SLT_Hash, "%s", str);

	if (!save_hash)
		return;

	/*
	* XXX: handle this by bouncing sp->vcl->nhashcount when it fails
	* XXX: and dispose of this request either by reallocating the
	* XXX: hashptr (if possible) or restarting/error the request
	*/
	xxxassert(sp->ihashptr < sp->nhashptr);

	sp->hashptr[sp->ihashptr] = str;
	sp->hashptr[sp->ihashptr + 1] = str + l;
	sp->ihashptr += 2;
	sp->lhashptr += l + 1;
}

struct objcore *
HSH_Lookup(struct sess *sp, struct objhead **poh)
{
	struct worker *w;
	struct objhead *oh;
	struct objcore *oc;
	struct objcore *busy_oc, *grace_oc;
	struct object *o;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(sp->http, HTTP_MAGIC);
	AN(hash);
	w = sp->wrk;

	HSH_Prealloc(sp);
	SHA256_Final(sp->wrk->nobjhead->digest, sp->wrk->sha256ctx);
	
	if (sp->objhead != NULL) {
		CHECK_OBJ_NOTNULL(sp->objhead, OBJHEAD_MAGIC);
		oh = sp->objhead;
		sp->objhead = NULL;
		CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
		Lck_Lock(&oh->mtx);
	} else {
		AN(w->nobjhead);
		oh = hash->lookup(sp, w->nobjhead);
		CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
		if (oh == w->nobjhead)
			w->nobjhead = NULL;
		Lck_Lock(&oh->mtx);
	}

	busy_oc = NULL;
	grace_oc = NULL;
	VTAILQ_FOREACH(oc, &oh->objcs, list) {
		/* Must be at least our own ref + the objcore we examine */
		assert(oh->refcnt > 1);
		CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

		if (oc->flags & OC_F_BUSY) {
			busy_oc = oc;
			continue;
		}

		o = oc->obj;
		CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
		
		if (!o->cacheable)
			continue;
		if (o->ttl == 0)
			continue;
		if (BAN_CheckObject(o, sp)) 
			continue;
		if (o->vary != NULL && !VRY_Match(sp, o->vary))
			continue;

		/* If still valid, use it */
		if (o->ttl >= sp->t_req)
			break;

		/* Remember any matching objects inside their grace period */
		if (o->ttl + HSH_Grace(o->grace) >= sp->t_req)
			grace_oc = oc;
	}

	/*
	 * If we have seen a busy object, and have an object in grace,
	 * use it, if req.grace is also satisified.
	 * XXX: Interesting footnote:  The busy object might be for a
	 * XXX: different "Vary:" than we sought.  We have no way of knowing
	 * XXX: this until the object is unbusy'ed, so in practice we
	 * XXX: serialize fetch of all Vary's if grace is possible.
	 */
	if (oc == NULL && grace_oc != NULL && busy_oc != NULL) {
		o = grace_oc->obj;
		CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
		if (o->ttl + HSH_Grace(sp->grace) >= sp->t_req)
			oc = grace_oc;
	}

	if (oc != NULL) {
		o = oc->obj;
		CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);

		/* We found an object we like */
		o->refcnt++;
		if (o->hits < INT_MAX)
			o->hits++;
		assert(oh->refcnt > 1);
		Lck_Unlock(&oh->mtx);
		assert(hash->deref(oh));
		*poh = oh;
		return (oc);
	}

	if (busy_oc != NULL) {
		/* There are one or more busy objects, wait for them */
		if (sp->esis == 0)
			VTAILQ_INSERT_TAIL(&oh->waitinglist, sp, list);
		if (params->diag_bitmap & 0x20)
			WSP(sp, SLT_Debug,
				"on waiting list <%s>", oh->hash);
		sp->objhead = oh;
		sp->wrk = NULL;
		Lck_Unlock(&oh->mtx);
		return (NULL);
	}

	/* Insert (precreated) objcore in objecthead */
	oc = w->nobjcore;
	w->nobjcore = NULL;
	AN(oc->flags & OC_F_BUSY);

	/* XXX: Should this not be ..._HEAD now ? */
	VTAILQ_INSERT_TAIL(&oh->objcs, oc, list);
	/* NB: do not deref objhead the new object inherits our reference */
	Lck_Unlock(&oh->mtx);
	*poh = oh;
	return (oc);
}

static void
hsh_rush(struct objhead *oh)
{
	unsigned u;
	struct sess *sp;

	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	Lck_AssertHeld(&oh->mtx);
	for (u = 0; u < params->rush_exponent; u++) {
		sp = VTAILQ_FIRST(&oh->waitinglist);
		if (sp == NULL)
			return;
		CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
		AZ(sp->wrk);
		VTAILQ_REMOVE(&oh->waitinglist, sp, list);
		DSL(0x20, SLT_Debug, sp->id, "off waiting list");
		WRK_QueueSession(sp);
	}
}

void
HSH_Drop(struct sess *sp)
{
	struct object *o;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	o = sp->obj;
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	assert(o->refcnt > 0);
	if (o->objcore != NULL) {	/* Pass has no objcore */
		AN(ObjIsBusy(o));
		o->ttl = 0;
	}
	o->cacheable = 0;
	if (o->objcore != NULL)		/* Pass has no objcore */
		HSH_Unbusy(sp);
	HSH_Deref(sp->wrk, &sp->obj);
}

void
HSH_Unbusy(const struct sess *sp)
{
	struct object *o;
	struct objhead *oh;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	o = sp->obj;
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	AN(ObjIsBusy(o));
	assert(o->objcore->obj == o);
	assert(o->refcnt > 0);
	if (o->ws_o->overflow)
		VSL_stats->n_objoverflow++;
	if (params->diag_bitmap & 0x40)
		WSP(sp, SLT_Debug,
		    "Object %u workspace free %u", o->xid, WS_Free(o->ws_o));

	oh = o->objhead;
	if (oh != NULL) {
		CHECK_OBJ(oh, OBJHEAD_MAGIC);
		Lck_Lock(&oh->mtx);
	}
	o->objcore->flags &= ~OC_F_BUSY;
	if (oh != NULL) {
		hsh_rush(oh);
		Lck_Unlock(&oh->mtx);
	}
}

void
HSH_Ref(struct object *o)
{
	struct objhead *oh;

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	oh = o->objhead;
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	Lck_Lock(&oh->mtx);
	assert(o->refcnt > 0);
	o->refcnt++;
	Lck_Unlock(&oh->mtx);
}

void
HSH_DerefObjCore(struct sess *sp)
{
	struct objhead *oh;
	struct objcore *oc;

	CHECK_OBJ_NOTNULL(sp->objhead, OBJHEAD_MAGIC);
	CHECK_OBJ_NOTNULL(sp->objcore, OBJCORE_MAGIC);

	oh = sp->objhead;
	sp->objhead = NULL;
	oc = sp->objcore;
	sp->objcore = NULL;

	Lck_Lock(&oh->mtx);
	VTAILQ_REMOVE(&oh->objcs, oc, list);
	Lck_Unlock(&oh->mtx);
	assert(oh->refcnt > 0);
	if (hash->deref(oh))
		return;
	HSH_DeleteObjHead(sp->wrk, oh);
}

void
HSH_Deref(const struct worker *w, struct object **oo)
{
	struct object *o;
	struct objhead *oh;
	struct objcore *oc;
	unsigned r;

	AN(oo);
	o = *oo;
	*oo = NULL;
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	oh = o->objhead;
	if (oh == NULL) {
		oc = NULL;
		assert(o->refcnt > 0);
		r = --o->refcnt;
	} else {
		CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);

		oc = o->objcore;
		CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
		
		Lck_Lock(&oh->mtx);
		assert(o->refcnt > 0);
		r = --o->refcnt;
		if (!r)
			VTAILQ_REMOVE(&oh->objcs, oc, list);
		else
			hsh_rush(oh);
		Lck_Unlock(&oh->mtx);
	}

	/* If still referenced, done */
	if (r != 0)
		return;

	BAN_DestroyObj(o);
	AZ(o->ban);
	DSL(0x40, SLT_Debug, 0, "Object %u workspace min free %u",
	    o->xid, WS_Free(o->ws_o));

	if (o->vary != NULL)
		free(o->vary);

	ESI_Destroy(o);
	HSH_Freestore(o);
	if (o->objstore != NULL)
		STV_free(o->objstore);
	else
		FREE_OBJ(o);
	o = NULL;
	w->stats->n_object--;

	if (oh == NULL) {
		AZ(oc);
		return;
	}
	AN(oc);
	FREE_OBJ(oc);
	/* Drop our ref on the objhead */
	assert(oh->refcnt > 0);
	if (hash->deref(oh))
		return;
	HSH_DeleteObjHead(w, oh);
}


void
HSH_Init(void)
{

	save_hash = params->save_hash;
	hash = heritage.hash;
	if (hash->start != NULL)
		hash->start();
}

const struct choice hsh_choice[] = {
	{ "classic",		&hcl_slinger },
	{ "simple",		&hsl_slinger },
	{ "simple_list",	&hsl_slinger },	/* backwards compat */
	{ "critbit",		&hcb_slinger },
	{ NULL,			NULL }
};
