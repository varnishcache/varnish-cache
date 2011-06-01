/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <fcntl.h>

#include "cache.h"
#include "stevedore.h"
#include "hash_slinger.h"
#include "vsha256.h"
#include "cache_backend.h"

static const struct hash_slinger *hash;

/*---------------------------------------------------------------------*/
/* Precreate an objhead and object for later use */
void
HSH_Prealloc(const struct sess *sp)
{
	struct worker *w;
	struct objhead *oh;
	struct objcore *oc;
	struct waitinglist *wl;

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
		Lck_New(&oh->mtx, lck_objhdr);
		w->nobjhead = oh;
		w->stats.n_objecthead++;
	}
	CHECK_OBJ_NOTNULL(w->nobjhead, OBJHEAD_MAGIC);

	if (w->nwaitinglist == NULL) {
		ALLOC_OBJ(wl, WAITINGLIST_MAGIC);
		XXXAN(wl);
		VTAILQ_INIT(&wl->list);
		w->nwaitinglist = wl;
		w->stats.n_waitinglist++;
	}
	CHECK_OBJ_NOTNULL(w->nwaitinglist, WAITINGLIST_MAGIC);

	if (hash->prep != NULL)
		hash->prep(sp);
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
	if (w->nwaitinglist != NULL) {
		FREE_OBJ(w->nwaitinglist);
		w->nwaitinglist = NULL;
	}
	if (w->nhashpriv != NULL) {
		/* XXX: If needed, add slinger method for this */
		free(w->nhashpriv);
		w->nhashpriv = NULL;
	}
}

void
HSH_DeleteObjHead(struct worker *w, struct objhead *oh)
{

	AZ(oh->refcnt);
	assert(VTAILQ_EMPTY(&oh->objcs));
	Lck_Delete(&oh->mtx);
	w->stats.n_objecthead--;
	FREE_OBJ(oh);
}

void
HSH_AddString(const struct sess *sp, const char *str)
{
	int l;

	if (str == NULL)
		str = "";
	l = strlen(str);

	SHA256_Update(sp->wrk->sha256ctx, str, l);
	SHA256_Update(sp->wrk->sha256ctx, "#", 1);

	if (params->log_hash)
		WSP(sp, SLT_Hash, "%s", str);
}

/*---------------------------------------------------------------------
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

/*---------------------------------------------------------------------
 * Insert an object which magically appears out of nowhere or, more likely,
 * comes off some persistent storage device.
 * Return it with a reference held.
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

	AZ(sp->hash_objhead);
	AN(w->nobjhead);
	oh = hash->lookup(sp, w->nobjhead);
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	if (oh == w->nobjhead)
		w->nobjhead = NULL;
	Lck_Lock(&oh->mtx);
	assert(oh->refcnt > 0);

	/* Insert (precreated) objcore in objecthead */
	oc = w->nobjcore;
	w->nobjcore = NULL;
	oc->refcnt = 1;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AZ(oc->flags & OC_F_BUSY);

	VTAILQ_INSERT_HEAD(&oh->objcs, oc, list);
	/* NB: do not deref objhead the new object inherits our reference */
	oc->objhead = oh;
	Lck_Unlock(&oh->mtx);
	sp->wrk->stats.n_vampireobject++;
	return (oc);
}

/*---------------------------------------------------------------------
 */

struct objcore *
HSH_Lookup(struct sess *sp, struct objhead **poh)
{
	struct worker *w;
	struct objhead *oh;
	struct objcore *oc;
	struct objcore *busy_oc, *grace_oc;
	struct object *o;
	double grace_ttl;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(sp->http, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(sp->director, DIRECTOR_MAGIC);
	AN(hash);
	w = sp->wrk;

	HSH_Prealloc(sp);
	memcpy(sp->wrk->nobjhead->digest, sp->digest, sizeof sp->digest);
	if (params->diag_bitmap & 0x80000000)
		hsh_testmagic(sp->wrk->nobjhead->digest);

	if (sp->hash_objhead != NULL) {
		/*
		 * This sess came off the waiting list, and brings a
		 * oh refcnt with it.
		 */
		CHECK_OBJ_NOTNULL(sp->hash_objhead, OBJHEAD_MAGIC);
		oh = sp->hash_objhead;
		sp->hash_objhead = NULL;
	} else {
		AN(w->nobjhead);
		oh = hash->lookup(sp, w->nobjhead);
		if (oh == w->nobjhead)
			w->nobjhead = NULL;
	}

	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	Lck_Lock(&oh->mtx);
	assert(oh->refcnt > 0);
	busy_oc = NULL;
	grace_oc = NULL;
	grace_ttl = NAN;
	VTAILQ_FOREACH(oc, &oh->objcs, list) {
		/* Must be at least our own ref + the objcore we examine */
		assert(oh->refcnt > 1);
		CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
		assert(oc->objhead == oh);

		if (oc->flags & OC_F_BUSY) {
			if (!sp->hash_ignore_busy)
				busy_oc = oc;
			continue;
		}

		o = oc_getobj(sp->wrk, oc);
		CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);

		if (o->exp.ttl <= 0.)
			continue;
		if (BAN_CheckObject(o, sp))
			continue;
		if (o->vary != NULL && !VRY_Match(sp, o->vary))
			continue;

		/* If still valid, use it */
		if (EXP_Ttl(sp, o) >= sp->t_req)
			break;

		/*
		 * Remember any matching objects inside their grace period
		 * and if there are several, use the least expired one.
		 */
		if (EXP_Grace(sp, o) >= sp->t_req) {
			if (grace_oc == NULL ||
			    grace_ttl < o->entered + o->exp.ttl) {
				grace_oc = oc;
				grace_ttl = o->entered + o->exp.ttl;
			}
		}
	}

	/*
	 * If we have seen a busy object or the backend is unhealthy, and
	 * we have an object in grace, use it, if req.grace is also
	 * satisified.
	 * XXX: Interesting footnote:  The busy object might be for a
	 * XXX: different "Vary:" than we sought.  We have no way of knowing
	 * XXX: this until the object is unbusy'ed, so in practice we
	 * XXX: serialize fetch of all Vary's if grace is possible.
	 */

	AZ(sp->objcore);
	sp->objcore = grace_oc;		/* XXX: Hack-ish */
	if (oc == NULL			/* We found no live object */
	    && grace_oc != NULL		/* There is a grace candidate */
	    && (busy_oc != NULL		/* Somebody else is already busy */
	    || !VDI_Healthy(sp->director, sp))) {
					/* Or it is impossible to fetch */
		o = oc_getobj(sp->wrk, grace_oc);
		CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
		oc = grace_oc;
	}
	sp->objcore = NULL;

	if (oc != NULL && !sp->hash_always_miss) {
		o = oc_getobj(sp->wrk, oc);
		CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
		assert(oc->objhead == oh);

		/* We found an object we like */
		oc->refcnt++;
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
		if (sp->esi_level == 0) {
			CHECK_OBJ_NOTNULL(sp->wrk->nwaitinglist,
			    WAITINGLIST_MAGIC);
			if (oh->waitinglist == NULL) {
				oh->waitinglist = sp->wrk->nwaitinglist;
				sp->wrk->nwaitinglist = NULL;
			}
			VTAILQ_INSERT_TAIL(&oh->waitinglist->list, sp, list);
		}
		if (params->diag_bitmap & 0x20)
			WSP(sp, SLT_Debug,
				"on waiting list <%p>", oh);
		SES_Charge(sp);
		/*
		 * The objhead reference transfers to the sess, we get it
		 * back when the sess comes off the waiting list and
		 * calls us again
		 */
		sp->hash_objhead = oh;
		sp->wrk = NULL;
		Lck_Unlock(&oh->mtx);
		return (NULL);
	}

	/* Insert (precreated) objcore in objecthead */
	oc = w->nobjcore;
	w->nobjcore = NULL;
	AN(oc->flags & OC_F_BUSY);
	oc->refcnt = 1;

	/*
	 * Busy objects go on the tail, so they will not trip up searches.
	 * HSH_Unbusy() will move them to the front.
	 */
	VTAILQ_INSERT_TAIL(&oh->objcs, oc, list);
	oc->objhead = oh;
	/* NB: do not deref objhead the new object inherits our reference */
	Lck_Unlock(&oh->mtx);
	*poh = oh;
	return (oc);
}

/*---------------------------------------------------------------------
 */

static void
hsh_rush(struct objhead *oh)
{
	unsigned u;
	struct sess *sp;
	struct waitinglist *wl;

	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	Lck_AssertHeld(&oh->mtx);
	wl = oh->waitinglist;
	if (wl == NULL)
		return;
	CHECK_OBJ_NOTNULL(wl, WAITINGLIST_MAGIC);
	for (u = 0; u < params->rush_exponent; u++) {
		sp = VTAILQ_FIRST(&wl->list);
		if (sp == NULL)
			break;
		CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
		AZ(sp->wrk);
		VTAILQ_REMOVE(&wl->list, sp, list);
		DSL(0x20, SLT_Debug, sp->id, "off waiting list");
		if (WRK_QueueSession(sp)) {
			/*
			 * We could not schedule the session, leave the
			 * rest on the busy list.
			 */
			VSC_C_main->client_drop_late++;
			break;
		}
	}
	if (VTAILQ_EMPTY(&wl->list)) {
		oh->waitinglist = NULL;
		FREE_OBJ(wl);
	}
}

/*---------------------------------------------------------------------
 * Purge an entire objhead
 */

void
HSH_Purge(const struct sess *sp, struct objhead *oh, double ttl, double grace)
{
	struct objcore *oc, **ocp;
	unsigned spc, nobj, n;
	struct object *o;

	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	spc = WS_Reserve(sp->wrk->ws, 0);
	ocp = (void*)sp->wrk->ws->f;
	Lck_Lock(&oh->mtx);
	assert(oh->refcnt > 0);
	nobj = 0;
	VTAILQ_FOREACH(oc, &oh->objcs, list) {
		CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
		assert(oc->objhead == oh);
		if (oc->flags & OC_F_BUSY) {
			/*
			 * We cannot purge busy objects here, because their
			 * owners have special rights to them, and may nuke
			 * them without concern for the refcount, which by
			 * definition always must be one, so they don't check.
			 */
			continue;
		}

		(void)oc_getobj(sp->wrk, oc); /* XXX: still needed ? */

		xxxassert(spc >= sizeof *ocp);
		oc->refcnt++;
		spc -= sizeof *ocp;
		ocp[nobj++] = oc;
	}
	Lck_Unlock(&oh->mtx);

	/* NB: inverse test to catch NAN also */
	if (!(ttl > 0.))
		ttl = -1.;
	if (!(grace > 0.))
		grace = -1.;
	for (n = 0; n < nobj; n++) {
		oc = ocp[n];
		CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
		o = oc_getobj(sp->wrk, oc);
		if (o == NULL)
			continue;
		CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
		o->exp.ttl = ttl;
		o->exp.grace = grace;
		EXP_Rearm(o);
		(void)HSH_Deref(sp->wrk, NULL, &o);
	}
	WS_Release(sp->wrk->ws, 0);
}


/*---------------------------------------------------------------------
 * Kill a busy object we don't need anyway.
 * There may be sessions on the waiting list, so we cannot just blow
 * it out of the water.
 */

void
HSH_Drop(struct sess *sp)
{
	struct object *o;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	o = sp->obj;
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	AssertObjPassOrBusy(o);
	o->exp.ttl = -1.;
	if (o->objcore != NULL)		/* Pass has no objcore */
		HSH_Unbusy(sp);
	(void)HSH_Deref(sp->wrk, NULL, &sp->obj);
}

void
HSH_Unbusy(const struct sess *sp)
{
	struct object *o;
	struct objhead *oh;
	struct objcore *oc;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	o = sp->obj;
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	oc = o->objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	oh = oc->objhead;
	CHECK_OBJ(oh, OBJHEAD_MAGIC);

	AssertObjBusy(o);
	AN(oc->ban);
	assert(oc_getobj(sp->wrk, oc) == o);
	assert(oc->refcnt > 0);
	assert(oh->refcnt > 0);
	if (o->ws_o->overflow)
		sp->wrk->stats.n_objoverflow++;
	if (params->diag_bitmap & 0x40)
		WSP(sp, SLT_Debug,
		    "Object %u workspace free %u", o->xid, WS_Free(o->ws_o));

	/* XXX: pretouch neighbors on oh->objcs to prevent page-on under mtx */
	Lck_Lock(&oh->mtx);
	assert(oh->refcnt > 0);
	/* XXX: strictly speaking, we should sort in Date: order. */
	VTAILQ_REMOVE(&oh->objcs, oc, list);
	VTAILQ_INSERT_HEAD(&oh->objcs, oc, list);
	oc->flags &= ~OC_F_BUSY;
	hsh_rush(oh);
	AN(oc->ban);
	Lck_Unlock(&oh->mtx);
}

void
HSH_Ref(struct objcore *oc)
{
	struct objhead *oh;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	oh = oc->objhead;
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	Lck_Lock(&oh->mtx);
	assert(oc->refcnt > 0);
	oc->refcnt++;
	Lck_Unlock(&oh->mtx);
}

/*--------------------------------------------------------------------
 * Dereference objcore and or object
 *
 * Can deal with:
 *	bare objcore (incomplete fetch)
 *	bare object (pass)
 *	object with objcore
 *	XXX later:  objcore with object (?)
 *
 * But you can only supply one of the two arguments at a time.
 *
 * Returns zero if target was destroyed.
 */

int
HSH_Deref(struct worker *w, struct objcore *oc, struct object **oo)
{
	struct object *o = NULL;
	struct objhead *oh;
	unsigned r;

	/* Only one arg at a time */
	assert(oc == NULL || oo == NULL);

	if (oo != NULL) {
		o = *oo;
		*oo = NULL;
		CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
		oc = o->objcore;
	}

	if (o != NULL && oc == NULL) {
		/*
		 * A pass object with neither objcore nor objhdr reference.
		 * -> simply free the (Transient) storage
		 */
		STV_Freestore(o);
		STV_free(o->objstore);
		w->stats.n_object--;
		return (0);
	}

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	oh = oc->objhead;
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);

	Lck_Lock(&oh->mtx);
	assert(oh->refcnt > 0);
	assert(oc->refcnt > 0);
	r = --oc->refcnt;
	if (!r)
		VTAILQ_REMOVE(&oh->objcs, oc, list);
	else {
		/* Must have an object */
		AN(oc->methods);
	}
	if (oc->flags & OC_F_BUSY)
		hsh_rush(oh);
	Lck_Unlock(&oh->mtx);
	if (r != 0)
		return (r);

	BAN_DestroyObj(oc);
	AZ(oc->ban);

	if (oc->methods != NULL) {
		oc_freeobj(oc);
		w->stats.n_object--;
	}
	FREE_OBJ(oc);

	w->stats.n_objectcore--;
	/* Drop our ref on the objhead */
	assert(oh->refcnt > 0);
	if (hash->deref(oh))
		return (0);
	HSH_DeleteObjHead(w, oh);
	return (0);
}

void
HSH_Init(void)
{

	assert(DIGEST_LEN == SHA256_LEN);	/* avoid #include pollution */
	hash = heritage.hash;
	if (hash->start != NULL)
		hash->start();
}

static const struct choice hsh_choice[] = {
	{ "classic",		&hcl_slinger },
	{ "simple",		&hsl_slinger },
	{ "simple_list",	&hsl_slinger },	/* backwards compat */
	{ "critbit",		&hcb_slinger },
	{ NULL,			NULL }
};

/*--------------------------------------------------------------------*/

void
HSH_config(const char *h_arg)
{
	char **av;
	int ac;
	const struct hash_slinger *hp;

	ASSERT_MGT();
	av = VAV_Parse(h_arg, NULL, ARGV_COMMA);
	AN(av);

	if (av[0] != NULL)
		ARGV_ERR("%s\n", av[0]);

	if (av[1] == NULL)
		ARGV_ERR("-h argument is empty\n");

	for (ac = 0; av[ac + 2] != NULL; ac++)
		continue;

	hp = pick(hsh_choice, av[1], "hash");
	CHECK_OBJ_NOTNULL(hp, SLINGER_MAGIC);
	VSB_printf(vident, ",-h%s", av[1]);
	heritage.hash = hp;
	if (hp->init != NULL)
		hp->init(ac, av + 2);
	else if (ac > 0)
		ARGV_ERR("Hash method \"%s\" takes no arguments\n",
		    hp->name);
}

