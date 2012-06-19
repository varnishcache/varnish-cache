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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache.h"


#include "hash/hash_slinger.h"
#include "vmb.h"
#include "vsha256.h"

static const struct hash_slinger *hash;

/*---------------------------------------------------------------------*/

struct objcore *
HSH_NewObjCore(struct worker *wrk)
{
	struct objcore *oc;

	ALLOC_OBJ(oc, OBJCORE_MAGIC);
	XXXAN(oc);
	wrk->stats.n_objectcore++;
	oc->flags |= OC_F_BUSY;
	return (oc);
}

/*---------------------------------------------------------------------*/
/* Precreate an objhead and object for later use */
static void
hsh_prealloc(struct worker *wrk)
{
	struct objhead *oh;
	struct waitinglist *wl;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	if (wrk->nobjcore == NULL)
		wrk->nobjcore = HSH_NewObjCore(wrk);
	CHECK_OBJ_NOTNULL(wrk->nobjcore, OBJCORE_MAGIC);

	if (wrk->nobjhead == NULL) {
		ALLOC_OBJ(oh, OBJHEAD_MAGIC);
		XXXAN(oh);
		oh->refcnt = 1;
		VTAILQ_INIT(&oh->objcs);
		Lck_New(&oh->mtx, lck_objhdr);
		wrk->nobjhead = oh;
		wrk->stats.n_objecthead++;
	}
	CHECK_OBJ_NOTNULL(wrk->nobjhead, OBJHEAD_MAGIC);

	if (wrk->nwaitinglist == NULL) {
		ALLOC_OBJ(wl, WAITINGLIST_MAGIC);
		XXXAN(wl);
		VTAILQ_INIT(&wl->list);
		wrk->nwaitinglist = wl;
		wrk->stats.n_waitinglist++;
	}
	CHECK_OBJ_NOTNULL(wrk->nwaitinglist, WAITINGLIST_MAGIC);

	if (hash->prep != NULL)
		hash->prep(wrk);
}

void
HSH_Cleanup(struct worker *wrk)
{

	if (wrk->nobjcore != NULL) {
		FREE_OBJ(wrk->nobjcore);
		wrk->stats.n_objectcore--;
		wrk->nobjcore = NULL;
	}
	if (wrk->nobjhead != NULL) {
		Lck_Delete(&wrk->nobjhead->mtx);
		FREE_OBJ(wrk->nobjhead);
		wrk->nobjhead = NULL;
		wrk->stats.n_objecthead--;
	}
	if (wrk->nwaitinglist != NULL) {
		FREE_OBJ(wrk->nwaitinglist);
		wrk->nwaitinglist = NULL;
		wrk->stats.n_waitinglist--;
	}
	if (wrk->nhashpriv != NULL) {
		/* XXX: If needed, add slinger method for this */
		free(wrk->nhashpriv);
		wrk->nhashpriv = NULL;
	}
}

void
HSH_DeleteObjHead(struct dstat *ds, struct objhead *oh)
{

	AZ(oh->refcnt);
	assert(VTAILQ_EMPTY(&oh->objcs));
	Lck_Delete(&oh->mtx);
	ds->n_objecthead--;
	FREE_OBJ(oh);
}

void
HSH_AddString(struct req *req, const char *str)
{
	int l;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	if (str == NULL)
		str = "";
	l = strlen(str);

	AN(req->sha256ctx);
	SHA256_Update(req->sha256ctx, str, l);
	SHA256_Update(req->sha256ctx, "#", 1);

	if (cache_param->log_hash)
		VSLb(req->vsl, SLT_Hash, "%s", str);
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

void
HSH_Insert(struct worker *wrk, const void *digest, struct objcore *oc)
{
	struct objhead *oh;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AN(digest);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	hsh_prealloc(wrk);

	AN(wrk->nobjhead);
	oh = hash->lookup(wrk, digest, &wrk->nobjhead);
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	Lck_AssertHeld(&oh->mtx);
	assert(oh->refcnt > 0);

	/* Insert (precreated) objcore in objecthead */
	oc->refcnt = 1;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AZ(oc->flags & OC_F_BUSY);

	VTAILQ_INSERT_HEAD(&oh->objcs, oc, list);
	/* NB: do not deref objhead the new object inherits our reference */
	oc->objhead = oh;
	Lck_Unlock(&oh->mtx);
	wrk->stats.n_objectcore++;
	wrk->stats.n_vampireobject++;
}

/*---------------------------------------------------------------------
 */

struct objcore *
HSH_Lookup(struct sess *sp)
{
	struct worker *wrk;
	struct objhead *oh;
	struct objcore *oc;
	struct objcore *grace_oc;
	struct object *o;
	struct req *req;
	double grace_ttl;
	int busy_found;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	wrk = sp->wrk;
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	req = sp->req;
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->http, HTTP_MAGIC);
	AN(req->director);
	AN(hash);

	hsh_prealloc(wrk);
	if (cache_param->diag_bitmap & 0x80000000)
		hsh_testmagic(req->digest);

	if (req->hash_objhead != NULL) {
		/*
		 * This sess came off the waiting list, and brings a
		 * oh refcnt with it.
		 */
		CHECK_OBJ_NOTNULL(req->hash_objhead, OBJHEAD_MAGIC);
		oh = req->hash_objhead;
		Lck_Lock(&oh->mtx);
		req->hash_objhead = NULL;
	} else {
		AN(wrk->nobjhead);
		oh = hash->lookup(wrk, req->digest, &wrk->nobjhead);
	}

	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	Lck_AssertHeld(&oh->mtx);

	assert(oh->refcnt > 0);
	busy_found = 0;
	grace_oc = NULL;
	grace_ttl = NAN;
	VTAILQ_FOREACH(oc, &oh->objcs, list) {
		/* Must be at least our own ref + the objcore we examine */
		assert(oh->refcnt > 1);
		CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
		assert(oc->objhead == oh);

		if (oc->flags & OC_F_BUSY || oc->busyobj != NULL) {
			CHECK_OBJ_ORNULL(oc->busyobj, BUSYOBJ_MAGIC);
			if (req->hash_ignore_busy || req->hash_always_miss)
				continue;

			if (oc->busyobj != NULL &&
			    oc->busyobj->vary != NULL &&
			    !VRY_Match(req, oc->busyobj->vary))
				continue;

			busy_found = 1;
			continue;
		}

		o = oc_getobj(&wrk->stats, oc);
		CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);

		if (o->exp.ttl <= 0.)
			continue;
		if (BAN_CheckObject(o, req))
			continue;
		if (o->vary != NULL && !VRY_Match(req, o->vary))
			continue;

		/* If still valid, use it */
		if (EXP_Ttl(req, o) >= req->t_req)
			break;

		/*
		 * Remember any matching objects inside their grace period
		 * and if there are several, use the least expired one.
		 */
		if (EXP_Grace(req, o) >= req->t_req) {
			if (grace_oc == NULL ||
			    grace_ttl < o->exp.entered + o->exp.ttl) {
				grace_oc = oc;
				grace_ttl = o->exp.entered + o->exp.ttl;
			}
		}
	}

	/*
	 * If we have seen a busy object or the backend is unhealthy, and
	 * we have an object in grace, use it, if req.grace is also
	 * satisified.
	 * XXX: VDI_Healty() call with oh->mtx is not so cool.
	 */

	AZ(req->objcore);
	if (oc == NULL			/* We found no live object */
	    && grace_oc != NULL		/* There is a grace candidate */
	    && (busy_found		/* Somebody else is already busy */
	    || !VDI_Healthy(req->director, req))) {
					/* Or it is impossible to fetch */
		o = oc_getobj(&wrk->stats, grace_oc);
		CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
		oc = grace_oc;
	}

	if (oc != NULL && !req->hash_always_miss) {
		/* We found an object we like */
		assert(oh->refcnt > 1);
		assert(oc->objhead == oh);
		oc->refcnt++;
		Lck_Unlock(&oh->mtx);
		assert(hash->deref(oh));
		o = oc_getobj(&wrk->stats, oc);
		CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
		if (!cache_param->obj_readonly && o->hits < INT_MAX)
			o->hits++;
		return (oc);
	}

	if (busy_found) {
		/* There are one or more busy objects, wait for them */
		if (req->esi_level == 0) {
			CHECK_OBJ_NOTNULL(wrk->nwaitinglist,
			    WAITINGLIST_MAGIC);
			if (oh->waitinglist == NULL) {
				oh->waitinglist = wrk->nwaitinglist;
				wrk->nwaitinglist = NULL;
			}
			VTAILQ_INSERT_TAIL(&oh->waitinglist->list, sp, list);
		}
		if (cache_param->diag_bitmap & 0x20)
			VSLb(req->vsl, SLT_Debug,
				"on waiting list <%p>", oh);
		SES_Charge(sp);
		/*
		 * The objhead reference transfers to the sess, we get it
		 * back when the sess comes off the waiting list and
		 * calls us again
		 */
		req->hash_objhead = oh;
		sp->wrk = NULL;
		Lck_Unlock(&oh->mtx);
		return (NULL);
	}

	/* Insert (precreated) objcore in objecthead and release mutex */
	oc = wrk->nobjcore;
	wrk->nobjcore = NULL;
	AN(oc->flags & OC_F_BUSY);
	oc->refcnt = 1;		/* Owned by busyobj */
	oc->objhead = oh;
	VTAILQ_INSERT_TAIL(&oh->objcs, oc, list);
	/* NB: do not deref objhead the new object inherits our reference */
	Lck_Unlock(&oh->mtx);

	AZ(req->busyobj);
	req->busyobj = VBO_GetBusyObj(wrk);
	req->busyobj->vsl->wid = sp->vsl_id;
	req->busyobj->refcount = 2;	/* One for req, one for FetchBody */

	VRY_Validate(req->vary_b);
	if (req->vary_l != NULL)
		req->busyobj->vary = req->vary_b;
	else
		req->busyobj->vary = NULL;

	VMB();
	oc->busyobj = req->busyobj;
	return (oc);
}

/*---------------------------------------------------------------------
 */

static void
hsh_rush(struct dstat *ds, struct objhead *oh)
{
	unsigned u;
	struct sess *sp;
	struct waitinglist *wl;

	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	Lck_AssertHeld(&oh->mtx);
	wl = oh->waitinglist;
	CHECK_OBJ_NOTNULL(wl, WAITINGLIST_MAGIC);
	for (u = 0; u < cache_param->rush_exponent; u++) {
		sp = VTAILQ_FIRST(&wl->list);
		if (sp == NULL)
			break;
		CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
		AZ(sp->wrk);
		VTAILQ_REMOVE(&wl->list, sp, list);
		DSL(0x20, SLT_Debug, sp->vsl_id, "off waiting list");
		if (SES_Schedule(sp)) {
			/*
			 * We could not schedule the session, leave the
			 * rest on the busy list.
			 */
			break;
		}
	}
	if (VTAILQ_EMPTY(&wl->list)) {
		oh->waitinglist = NULL;
		FREE_OBJ(wl);
		ds->n_waitinglist--;
	}
}

/*---------------------------------------------------------------------
 * Purge an entire objhead
 */

void
HSH_Purge(struct req *req, struct objhead *oh, double ttl, double grace)
{
	struct objcore *oc, **ocp;
	unsigned spc, nobj, n;
	struct object *o;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	spc = WS_Reserve(req->ws, 0);
	ocp = (void*)req->ws->f;
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

		(void)oc_getobj(&req->sp->wrk->stats, oc);
		    /* XXX: still needed ? */

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
		o = oc_getobj(&req->sp->wrk->stats, oc);
		if (o == NULL)
			continue;
		CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
		o->exp.ttl = ttl;
		o->exp.grace = grace;
		EXP_Rearm(o);
		(void)HSH_Deref(&req->sp->wrk->stats, NULL, &o);
	}
	WS_Release(req->ws, 0);
}


/*---------------------------------------------------------------------
 * Kill a busy object we don't need and can't use.
 */

void
HSH_Drop(struct worker *wrk, struct object **oo)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AN(oo);
	CHECK_OBJ_NOTNULL(*oo, OBJECT_MAGIC);
	(*oo)->exp.ttl = -1.;
	AZ(HSH_Deref(&wrk->stats, NULL, oo));
}

/*---------------------------------------------------------------------
 * Remove the busyobj from an objcore
 */

void
HSH_Complete(struct objcore *oc)
{
	struct objhead *oh;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	oh = oc->objhead;
	CHECK_OBJ(oh, OBJHEAD_MAGIC);

	Lck_Lock(&oh->mtx);
	oc->busyobj = NULL;
	Lck_Unlock(&oh->mtx);
}

/*---------------------------------------------------------------------
 * Unbusy an objcore when the object is completely fetched.
 */

void
HSH_Unbusy(struct dstat *ds, struct objcore *oc)
{
	struct objhead *oh;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	oh = oc->objhead;
	CHECK_OBJ(oh, OBJHEAD_MAGIC);

	AN(oc->flags & OC_F_BUSY);
	AN(oc->ban);
	assert(oh->refcnt > 0);

	/* XXX: pretouch neighbors on oh->objcs to prevent page-on under mtx */
	Lck_Lock(&oh->mtx);
	assert(oh->refcnt > 0);
	/* XXX: strictly speaking, we should sort in Date: order. */
	VTAILQ_REMOVE(&oh->objcs, oc, list);
	VTAILQ_INSERT_HEAD(&oh->objcs, oc, list);
	oc->flags &= ~OC_F_BUSY;
	if (oh->waitinglist != NULL)
		hsh_rush(ds, oh);
	Lck_Unlock(&oh->mtx);
}

/*---------------------------------------------------------------------
 * Gain a reference on an objcore
 */

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
HSH_Deref(struct dstat *ds, struct objcore *oc, struct object **oo)
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

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	oh = oc->objhead;
	if (oh != NULL) {
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
		if (oh->waitinglist != NULL)
			hsh_rush(ds, oh);
		Lck_Unlock(&oh->mtx);
		if (r != 0)
			return (r);

		BAN_DestroyObj(oc);
		AZ(oc->ban);
	}

	if (oc->methods != NULL) {
		oc_freeobj(oc);
		ds->n_object--;
	}
	FREE_OBJ(oc);

	ds->n_objectcore--;
	if (oh != NULL) {
		/* Drop our ref on the objhead */
		assert(oh->refcnt > 0);
		if (hash->deref(oh))
			return (0);
		HSH_DeleteObjHead(ds, oh);
	}
	return (0);
}

void
HSH_Init(const struct hash_slinger *slinger)
{

	assert(DIGEST_LEN == SHA256_LEN);	/* avoid #include pollution */
	hash = slinger;
	if (hash->start != NULL)
		hash->start();
}
