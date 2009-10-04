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

#include "svnid.h"
SVNID("$Id$")

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
#include "cache_backend.h"

static const struct hash_slinger *hash;
unsigned	save_hash;

double
HSH_Grace(double g)
{
	if (isnan(g))
		return (double)(params->default_grace);
	return (g);
}

/*
 * XXX: this should vector through stevedore.c instead of calling the
 * XXX: member function directly.
 */

void
HSH_Object(const struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj->objstore, STORAGE_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj->objstore->stevedore, STEVEDORE_MAGIC);
	AN(ObjIsBusy(sp->obj));
	if (sp->obj->objstore->stevedore->object != NULL)
		sp->obj->objstore->stevedore->object(sp);
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
		w->stats.n_objectcore++;
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
		w->stats.n_objecthead++;
	}
	CHECK_OBJ_NOTNULL(w->nobjhead, OBJHEAD_MAGIC);

}

void
HSH_Cleanup(struct worker *w)
{

	if (w->nobjcore != NULL) {
		FREE_OBJ(w->nobjcore);
		w->stats.n_objectcore--;
		w->nobjcore = NULL;
	}
	if (w->nobjhead != NULL) {
		Lck_Delete(&w->nobjhead->mtx);
		FREE_OBJ(w->nobjhead);
		w->nobjhead = NULL;
		w->stats.n_objecthead--;
	}
}

void
HSH_DeleteObjHead(struct worker *w, struct objhead *oh)
{

	AZ(oh->refcnt);
	assert(VTAILQ_EMPTY(&oh->objcs));
	Lck_Delete(&oh->mtx);
	w->stats.n_objecthead--;
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
HSH_BeforeVclHash(struct sess *sp, unsigned nhashcount)
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
	p = WS_Alloc(sp->http->ws, sizeof(const char *) * (sp->nhashptr + 1L));
	XXXAN(p);
	/* Align pointer properly (?) */
	u = (uintptr_t)p;
	u &= sizeof(const char *) - 1;
	if (u)
		p += sizeof(const char *) - u;
	sp->hashptr = (void*)p;
}

void
HSH_AfterVclHash(const struct sess *sp)
{

	HSH_Prealloc(sp);
	SHA256_Final(sp->wrk->nobjhead->digest, sp->wrk->sha256ctx);
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

/**********************************************************************
 * This is a debugging hack to enable testing of boundary conditions
 * in the hash algorithm.
 * We trap the first 9 different digests and translate them to different
 * digests with edge bit conditions
 */

static struct hsh_magiclist {
	unsigned char was[SHA256_LEN];
	unsigned char now[SHA256_LEN];
} hsh_magiclist[] = {
	{ .now = {	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ .now = {	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 } },
	{ .now = {	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 } },
	{ .now = {	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40 } },
	{ .now = {	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80 } },
	{ .now = {	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ .now = {	0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ .now = {	0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ .now = {	0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
};

#define HSH_NMAGIC (sizeof hsh_magiclist / sizeof hsh_magiclist[0])

static void
hsh_testmagic(void *result)
{
	int i, j;
	static int nused = 0;

	for (i = 0; i < nused; i++)
		if (!memcmp(hsh_magiclist[i].was, result, SHA256_LEN))
			break;
	if (i == nused && i < HSH_NMAGIC) 
		memcpy(hsh_magiclist[nused++].was, result, SHA256_LEN);
	if (i == nused)
		return;
	assert(i < HSH_NMAGIC);
	fprintf(stderr, "HASHMAGIC: <");
	for (j = 0; j < SHA256_LEN; j++)
		fprintf(stderr, "%02x", ((unsigned char*)result)[j]);
	fprintf(stderr, "> -> <");
	memcpy(result, hsh_magiclist[i].now, SHA256_LEN);
	for (j = 0; j < SHA256_LEN; j++)
		fprintf(stderr, "%02x", ((unsigned char*)result)[j]);
	fprintf(stderr, ">\n");
}

/**********************************************************************
 * Insert an object which magically appears out of nowhere or, more likely,
 * comes off some persistent storage device.
 */

struct objcore *
HSH_Insert(const struct sess *sp)
{
	struct worker *w;
	struct objhead *oh;
	struct objcore *oc;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	AN(hash);
	w = sp->wrk;

	HSH_Prealloc(sp);
	if (params->diag_bitmap & 0x80000000)
		hsh_testmagic(sp->wrk->nobjhead->digest);
	
	AZ(sp->objhead);
	AN(w->nobjhead);
	oh = hash->lookup(sp, w->nobjhead);
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	if (oh == w->nobjhead)
		w->nobjhead = NULL;
	Lck_Lock(&oh->mtx);

	/* Insert (precreated) objcore in objecthead */
	oc = w->nobjcore;
	w->nobjcore = NULL;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AZ(oc->flags & OC_F_BUSY);

	/* XXX: Should this not be ..._HEAD now ? */
	VTAILQ_INSERT_TAIL(&oh->objcs, oc, list);
	/* NB: do not deref objhead the new object inherits our reference */
	oc->objhead = oh;
	Lck_Unlock(&oh->mtx);
	sp->wrk->stats.n_vampireobject++;
	return (oc);
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
	CHECK_OBJ_NOTNULL(sp->director, DIRECTOR_MAGIC);
	AN(hash);
	w = sp->wrk;

	HSH_Prealloc(sp);
	if (params->diag_bitmap & 0x80000000)
		hsh_testmagic(sp->wrk->nobjhead->digest);
	
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

		if (oc->flags & OC_F_PERSISTENT)
			SMP_Fixup(sp, oh, oc);

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
	 * If we have seen a busy object or the backend is unhealthy, and
	 * have an object in grace, use it, if req.grace is also
	 * satisified.
	 * XXX: Interesting footnote:  The busy object might be for a
	 * XXX: different "Vary:" than we sought.  We have no way of knowing
	 * XXX: this until the object is unbusy'ed, so in practice we
	 * XXX: serialize fetch of all Vary's if grace is possible.
	 */
	/* Grace-stuff: sp->objhead is evaluated in healthy() for 'saint
	 * mode'. Is this entirely wrong, or just ugly? Why isn't objhead
	 * set here? FIXME:Grace.
	 */
	sp->objhead = oh;
	if (oc == NULL && grace_oc != NULL && 
	    (busy_oc != NULL || !sp->director->healthy(sp))) {
		o = grace_oc->obj;
		CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
		if (o->ttl + HSH_Grace(sp->grace) >= sp->t_req)
			oc = grace_oc;
	}
	sp->objhead = NULL;

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
		SES_Charge(sp);
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
	CHECK_OBJ_NOTNULL(o->objcore, OBJCORE_MAGIC);
	oh = o->objcore->objhead;
	CHECK_OBJ(oh, OBJHEAD_MAGIC);

	AN(ObjIsBusy(o));
	AN(o->ban);
	assert(o->objcore->obj == o);
	assert(o->refcnt > 0);
	assert(oh->refcnt > 0);
	if (o->ws_o->overflow)
		sp->wrk->stats.n_objoverflow++;
	if (params->diag_bitmap & 0x40)
		WSP(sp, SLT_Debug,
		    "Object %u workspace free %u", o->xid, WS_Free(o->ws_o));

	Lck_Lock(&oh->mtx);
	o->objcore->flags &= ~OC_F_BUSY;
	hsh_rush(oh);
	AN(o->ban);
	Lck_Unlock(&oh->mtx);
}

void
HSH_Ref(struct object *o)
{
	struct objhead *oh;

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	CHECK_OBJ_NOTNULL(o->objcore, OBJCORE_MAGIC);
	oh = o->objcore->objhead;
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
	FREE_OBJ(oc);
	sp->wrk->stats.n_objectcore--;
	if (hash->deref(oh))
		return;
	HSH_DeleteObjHead(sp->wrk, oh);
}

/*******************************************************************
 * This one is slightly tricky.  This is called from the BAN module
 * to try to wash the object which holds the oldest ban.
 * We compete against HSH_Deref() which comes in the opposite 
 * locking order, we need to hold the BAN mutex, to stop the
 * BAN_DestroyObj() call in HSH_Deref(), so that the objhead
 * will not be removed under us.
 * NB: Do not call this function any other way or from any other
 * NB: place in the code.  It will not work for you.
 */

void
HSH_FindBan(struct sess *sp, struct objcore **oc)
{
	struct objcore *oc1, *oc2;
	struct objhead *oh;

	oc1 = *oc;
	CHECK_OBJ_NOTNULL(oc1, OBJCORE_MAGIC);
	oh = oc1->objhead;
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	Lck_Lock(&oh->mtx);
	VTAILQ_FOREACH(oc2, &oh->objcs, list)
		if (oc1 == oc2)
			break;
	if (oc2 != NULL && oc2->flags & OC_F_PERSISTENT)
		SMP_Fixup(sp, oh, oc2);
	if (oc2 != NULL)
		oc2->obj->refcnt++;
	Lck_Unlock(&oh->mtx);
	*oc = oc2;
}

void
HSH_Deref(struct worker *w, struct object **oo)
{
	struct object *o;
	struct objhead *oh;
	struct objcore *oc;
	unsigned r;

	AN(oo);
	o = *oo;
	*oo = NULL;
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	oc = o->objcore;
	if (oc == NULL) {
		assert(o->refcnt > 0);
		r = --o->refcnt;
		oh = NULL;
	} else {
		CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
		oh = oc->objhead;
		CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
		
		Lck_Lock(&oh->mtx);
		assert(oh->refcnt > 0);
		assert(o->refcnt > 0);
		r = --o->refcnt;
		if (!r) 
			VTAILQ_REMOVE(&oh->objcs, oc, list);
		hsh_rush(oh);
		Lck_Unlock(&oh->mtx);
	}

	/* If still referenced, done */
	if (r != 0)
		return;

	if (oh != NULL)
		BAN_DestroyObj(o);
	AZ(o->ban);
	DSL(0x40, SLT_Debug, 0, "Object %u workspace min free %u",
	    o->xid, WS_Free(o->ws_o));

	if (o->vary != NULL)
		free(o->vary);

	ESI_Destroy(o);
	if (o->objcore != NULL && o->objcore->smp_seg != NULL) {
		SMP_FreeObj(o);
	} else {
		HSH_Freestore(o);
		if (o->objstore != NULL)
			STV_free(o->objstore);
		else
			FREE_OBJ(o);
	}
	o = NULL;
	w->stats.n_object--;

	if (oc == NULL) {
		AZ(oh);
		return;
	}
	AN(oh);
	FREE_OBJ(oc);
	w->stats.n_objectcore--;
	/* Drop our ref on the objhead */
	assert(oh->refcnt > 0);
	if (hash->deref(oh))
		return;
	HSH_DeleteObjHead(w, oh);
}

void
HSH_Init(void)
{

	assert(DIGEST_LEN == SHA256_LEN);	/* avoid #include pollution */
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
