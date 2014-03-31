/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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
#include "vsha256.h"
#include "vtim.h"

static const struct hash_slinger *hash;
static struct objhead *private_oh;

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

static struct objhead *
hsh_newobjhead(void)
{
	struct objhead *oh;

	ALLOC_OBJ(oh, OBJHEAD_MAGIC);
	XXXAN(oh);
	oh->refcnt = 1;
	VTAILQ_INIT(&oh->objcs);
	Lck_New(&oh->mtx, lck_objhdr);
	return (oh);
}

/*---------------------------------------------------------------------*/
/* Precreate an objhead and object for later use */
static void
hsh_prealloc(struct worker *wrk)
{
	struct waitinglist *wl;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	if (wrk->nobjcore == NULL)
		wrk->nobjcore = HSH_NewObjCore(wrk);
	CHECK_OBJ_NOTNULL(wrk->nobjcore, OBJCORE_MAGIC);

	if (wrk->nobjhead == NULL) {
		wrk->nobjhead = hsh_newobjhead();
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

/*---------------------------------------------------------------------*/

struct objcore *
HSH_Private(struct worker *wrk)
{
	struct objcore *oc;

	CHECK_OBJ_NOTNULL(private_oh, OBJHEAD_MAGIC);

	oc = HSH_NewObjCore(wrk);
	AN(oc);
	oc->refcnt = 1;
	oc->objhead = private_oh;
	oc->flags |= OC_F_PRIVATE;
	Lck_Lock(&private_oh->mtx);
	VTAILQ_INSERT_TAIL(&private_oh->objcs, oc, list);
	private_oh->refcnt++;
	Lck_Unlock(&private_oh->mtx);
	return (oc);
}

/*---------------------------------------------------------------------*/
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
HSH_AddString(const struct req *req, const char *str)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AN(req->sha256ctx);
	if (str != NULL)
		SHA256_Update(req->sha256ctx, str, strlen(str));
	else
		SHA256_Update(req->sha256ctx, &str, sizeof str);
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

static struct objcore *
hsh_insert_busyobj(struct worker *wrk, struct objhead *oh)
{
	struct objcore *oc;

	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	Lck_AssertHeld(&oh->mtx);

	oc = wrk->nobjcore;
	wrk->nobjcore = NULL;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	AN(oc->flags & OC_F_BUSY);
	oc->refcnt = 1;		/* Owned by busyobj */
	oc->objhead = oh;
	VTAILQ_INSERT_TAIL(&oh->objcs, oc, list);
	return (oc);
}

/*---------------------------------------------------------------------
 */

enum lookup_e
HSH_Lookup(struct req *req, struct objcore **ocp, struct objcore **bocp,
    int wait_for_busy, int always_insert)
{
	struct worker *wrk;
	struct objhead *oh;
	struct objcore *oc;
	struct objcore *exp_oc;
	struct object *o, *exp_o;
	double exp_t_origin;
	int busy_found;
	enum lookup_e retval;

	AN(ocp);
	*ocp = NULL;
	AN(bocp);
	*bocp = NULL;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	wrk = req->wrk;
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req->http, HTTP_MAGIC);
	AN(hash);

	hsh_prealloc(wrk);
	if (DO_DEBUG(DBG_HASHEDGE))
		hsh_testmagic(req->digest);

	if (req->hash_objhead != NULL) {
		/*
		 * This req came off the waiting list, and brings an
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

	if (always_insert) {
		/* XXX: should we do predictive Vary in this case ? */
		/* Insert new objcore in objecthead and release mutex */
		*bocp = hsh_insert_busyobj(wrk, oh);
		/* NB: no deref of objhead, new object inherits reference */
		Lck_Unlock(&oh->mtx);
		return (HSH_MISS);
	}

	assert(oh->refcnt > 0);
	busy_found = 0;
	exp_o = NULL;
	exp_oc = NULL;
	exp_t_origin = 0.0;
	o = NULL;
	VTAILQ_FOREACH(oc, &oh->objcs, list) {
		/* Must be at least our own ref + the objcore we examine */
		assert(oh->refcnt > 1);
		CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
		assert(oc->objhead == oh);

		if (oc->exp_flags & OC_EF_DYING)
			continue;
		if (oc->flags & OC_F_FAILED)
			continue;

		if (oc->flags & OC_F_BUSY) {
			CHECK_OBJ_ORNULL(oc->busyobj, BUSYOBJ_MAGIC);
			if (req->hash_ignore_busy)
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

		if (EXP_Ttl(req, o) >= req->t_req) {
			/* If still valid, use it */
			assert(oh->refcnt > 1);
			assert(oc->objhead == oh);
			oc->refcnt++;
			if (oh->hits < LONG_MAX)
				oh->hits++;
			Lck_Unlock(&oh->mtx);
			assert(HSH_DerefObjHead(&wrk->stats, &oh));
			*ocp = oc;
			return (HSH_HIT);
		}
		if (o->exp.t_origin > exp_t_origin &&
		    !(oc->flags & OC_F_PASS)) {
			/* record the newest object */
			exp_oc = oc;
			exp_o = o;
			exp_t_origin = o->exp.t_origin;
		}
	}

	if (exp_oc != NULL) {
		AN(exp_o);
		assert(oh->refcnt > 1);
		assert(exp_oc->objhead == oh);
		exp_oc->refcnt++;

		if (!busy_found) {
			AZ(req->hash_ignore_busy);
			*bocp = hsh_insert_busyobj(wrk, oh);
			retval = HSH_EXPBUSY;
		} else {
			retval = HSH_EXP;
		}
		if (oh->hits < LONG_MAX)
			oh->hits++;
		Lck_Unlock(&oh->mtx);
		if (retval == HSH_EXP)
			assert(HSH_DerefObjHead(&wrk->stats, &oh));
		*ocp = exp_oc;
		return (retval);
	}

	if (!busy_found) {
		/* Insert objcore in objecthead and release mutex */
		*bocp = hsh_insert_busyobj(wrk, oh);
		/* NB: no deref of objhead, new object inherits reference */
		Lck_Unlock(&oh->mtx);
		return (HSH_MISS);
	}

	/* There are one or more busy objects, wait for them */

	AZ(req->hash_ignore_busy);

	if (wait_for_busy) {
		CHECK_OBJ_NOTNULL(wrk->nwaitinglist, WAITINGLIST_MAGIC);
		if (oh->waitinglist == NULL) {
			oh->waitinglist = wrk->nwaitinglist;
			wrk->nwaitinglist = NULL;
		}
		VTAILQ_INSERT_TAIL(&oh->waitinglist->list,
		    req, w_list);
		if (DO_DEBUG(DBG_WAITINGLIST))
			VSLb(req->vsl, SLT_Debug, "on waiting list <%p>", oh);
	} else {
		if (DO_DEBUG(DBG_WAITINGLIST))
			VSLb(req->vsl, SLT_Debug, "hit busy obj <%p>", oh);
	}

	wrk->stats.busy_sleep++;
	/*
	 * The objhead reference transfers to the sess, we get it
	 * back when the sess comes off the waiting list and
	 * calls us again
	 */
	req->hash_objhead = oh;
	req->wrk = NULL;
	Lck_Unlock(&oh->mtx);
	return (HSH_BUSY);
}

/*---------------------------------------------------------------------
 */

static void
hsh_rush(struct dstat *ds, struct objhead *oh)
{
	unsigned u;
	struct req *req;
	struct waitinglist *wl;

	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	Lck_AssertHeld(&oh->mtx);
	wl = oh->waitinglist;
	CHECK_OBJ_NOTNULL(wl, WAITINGLIST_MAGIC);
	for (u = 0; u < cache_param->rush_exponent; u++) {
		req = VTAILQ_FIRST(&wl->list);
		if (req == NULL)
			break;
		CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
		ds->busy_wakeup++;
		AZ(req->wrk);
		VTAILQ_REMOVE(&wl->list, req, w_list);
		DSL(DBG_WAITINGLIST, req->vsl->wid, "off waiting list");
		if (SES_ScheduleReq(req)) {
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
HSH_Purge(struct worker *wrk, struct objhead *oh, double ttl, double grace)
{
	struct objcore *oc, **ocp;
	unsigned spc, nobj, n;
	struct object *o;
	double now;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	spc = WS_Reserve(wrk->aws, 0);
	ocp = (void*)wrk->aws->f;
	Lck_Lock(&oh->mtx);
	assert(oh->refcnt > 0);
	nobj = 0;
	now = VTIM_real();
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
		if (oc->exp_flags & OC_EF_DYING)
			continue;
		xxxassert(spc >= sizeof *ocp);
		oc->refcnt++;
		spc -= sizeof *ocp;
		ocp[nobj++] = oc;
	}
	Lck_Unlock(&oh->mtx);

	for (n = 0; n < nobj; n++) {
		oc = ocp[n];
		CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
		o = oc_getobj(&wrk->stats, oc);
		if (o == NULL)
			continue;
		CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
		EXP_Rearm(o, now, ttl, grace, NAN);	// XXX: Keep ?
		(void)HSH_DerefObj(&wrk->stats, &o);
	}
	WS_Release(wrk->aws, 0);
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
	AZ(HSH_DerefObj(&wrk->stats, oo));
}

/*---------------------------------------------------------------------
 * Fail an objcore
 */

void
HSH_Fail(struct objcore *oc)
{
	struct objhead *oh;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	oh = oc->objhead;
	CHECK_OBJ(oh, OBJHEAD_MAGIC);

	/*
	 * We have to have either a busy bit, so that HSH_Lookup
	 * will not consider this oc, or an object hung of the oc
	 * so that it can consider it.
	 */
	assert((oc->flags & OC_F_BUSY) || (oc->methods != NULL));

	Lck_Lock(&oh->mtx);
	oc->flags |= OC_F_FAILED;
	oc->busyobj = NULL;
	Lck_Unlock(&oh->mtx);
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

	AN(oc->methods);
	AN(oc->flags & OC_F_BUSY);
	assert(oh->refcnt > 0);

	if (!(oc->flags & OC_F_PRIVATE)) {
		BAN_NewObjCore(oc);
		EXP_Insert(oc);
		AN(oc->exp_flags & OC_EF_EXP);
		AN(oc->ban);
	}

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

/*---------------------------------------------------------------------
 * Gain a reference on the busyobj, if the objcore has one
 */

struct busyobj *
HSH_RefBusy(const struct objcore *oc)
{
	struct objhead *oh;
	struct busyobj *bo;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	oh = oc->objhead;
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	Lck_Lock(&oh->mtx);
	assert(oc->refcnt > 0);
	bo = oc->busyobj;
	CHECK_OBJ_ORNULL(bo, BUSYOBJ_MAGIC);
	if (bo != NULL)
		bo->refcount++;
	Lck_Unlock(&oh->mtx);
	return (bo);
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
HSH_DerefObj(struct dstat *ds, struct object **oo)
{
	struct object *o;
	struct objcore *oc;

	AN(oo);
	o = *oo;
	*oo = NULL;

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	oc = o->objcore;
	return (HSH_DerefObjCore(ds, &oc));
}

int
HSH_DerefObjCore(struct dstat *ds, struct objcore **ocp)
{
	struct objcore *oc;
	struct objhead *oh;
	unsigned r;

	AN(ocp);
	AN(ds);
	oc = *ocp;
	*ocp = NULL;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	assert(oc->refcnt > 0);

	oh = oc->objhead;
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);

	Lck_Lock(&oh->mtx);
	assert(oh->refcnt > 0);
	r = --oc->refcnt;
	if (!r)
		VTAILQ_REMOVE(&oh->objcs, oc, list);
	if (oh->waitinglist != NULL)
		hsh_rush(ds, oh);
	Lck_Unlock(&oh->mtx);
	if (r != 0)
		return (r);

	BAN_DestroyObj(oc);
	AZ(oc->ban);

	if (oc->methods != NULL) {
		oc_freeobj(oc);
		ds->n_object--;
	}
	FREE_OBJ(oc);

	ds->n_objectcore--;
	/* Drop our ref on the objhead */
	assert(oh->refcnt > 0);
	(void)HSH_DerefObjHead(ds, &oh);
	return (0);
}

int
HSH_DerefObjHead(struct dstat *ds, struct objhead **poh)
{
	struct objhead *oh;
	int r;

	AN(ds);
	AN(poh);
	oh = *poh;
	*poh = NULL;
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);

	if (oh == private_oh) {
		Lck_Lock(&oh->mtx);
		assert(oh->refcnt > 1);
		oh->refcnt--;
		Lck_Unlock(&oh->mtx);
		return(1);
	}

	assert(oh->refcnt > 0);
	r = hash->deref(oh);
	if (!r)
		HSH_DeleteObjHead(ds, oh);
	return (r);
}

void
HSH_Init(const struct hash_slinger *slinger)
{

	assert(DIGEST_LEN == SHA256_LEN);	/* avoid #include pollution */
	hash = slinger;
	if (hash->start != NULL)
		hash->start();
	private_oh = hsh_newobjhead();
	private_oh->refcnt = 1;
}
