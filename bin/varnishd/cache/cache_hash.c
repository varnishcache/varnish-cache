/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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

#include "cache.h"

#include <stdio.h>
#include <stdlib.h>

#include "cache/cache_transport.h"

#include "hash/hash_slinger.h"
#include "vsha256.h"
#include "vtim.h"

struct rush {
	unsigned		magic;
#define RUSH_MAGIC		0xa1af5f01
	VTAILQ_HEAD(,req)	reqs;
};

static const struct hash_slinger *hash;
static struct objhead *private_oh;

static void hsh_rush1(const struct worker *, struct objhead *,
    struct rush *, int);
static void hsh_rush2(struct worker *, struct rush *);

/*---------------------------------------------------------------------*/

static struct objhead *
hsh_newobjhead(void)
{
	struct objhead *oh;

	ALLOC_OBJ(oh, OBJHEAD_MAGIC);
	XXXAN(oh);
	oh->refcnt = 1;
	VTAILQ_INIT(&oh->objcs);
	VTAILQ_INIT(&oh->waitinglist);
	Lck_New(&oh->mtx, lck_objhdr);
	return (oh);
}

/*---------------------------------------------------------------------*/
/* Precreate an objhead and object for later use */
static void
hsh_prealloc(struct worker *wrk)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	if (wrk->nobjcore == NULL)
		wrk->nobjcore = ObjNew(wrk);
	CHECK_OBJ_NOTNULL(wrk->nobjcore, OBJCORE_MAGIC);

	if (wrk->nobjhead == NULL) {
		wrk->nobjhead = hsh_newobjhead();
		wrk->stats->n_objecthead++;
	}
	CHECK_OBJ_NOTNULL(wrk->nobjhead, OBJHEAD_MAGIC);

	if (hash->prep != NULL)
		hash->prep(wrk);
}

/*---------------------------------------------------------------------*/

struct objcore *
HSH_Private(const struct worker *wrk)
{
	struct objcore *oc;

	CHECK_OBJ_NOTNULL(private_oh, OBJHEAD_MAGIC);

	oc = ObjNew(wrk);
	AN(oc);
	oc->refcnt = 1;
	oc->objhead = private_oh;
	oc->flags |= OC_F_PRIVATE;
	Lck_Lock(&private_oh->mtx);
	VTAILQ_INSERT_TAIL(&private_oh->objcs, oc, hsh_list);
	private_oh->refcnt++;
	Lck_Unlock(&private_oh->mtx);
	return (oc);
}

/*---------------------------------------------------------------------*/
void
HSH_Cleanup(struct worker *wrk)
{

	if (wrk->nobjcore != NULL)
		ObjDestroy(wrk, &wrk->nobjcore);

	if (wrk->nobjhead != NULL) {
		Lck_Delete(&wrk->nobjhead->mtx);
		FREE_OBJ(wrk->nobjhead);
		wrk->nobjhead = NULL;
		wrk->stats->n_objecthead--;
	}
	if (wrk->nhashpriv != NULL) {
		/* XXX: If needed, add slinger method for this */
		free(wrk->nhashpriv);
		wrk->nhashpriv = NULL;
	}
}

void
HSH_DeleteObjHead(const struct worker *wrk, struct objhead *oh)
{

	AZ(oh->refcnt);
	assert(VTAILQ_EMPTY(&oh->objcs));
	assert(VTAILQ_EMPTY(&oh->waitinglist));
	Lck_Delete(&oh->mtx);
	wrk->stats->n_objecthead--;
	FREE_OBJ(oh);
}

void
HSH_AddString(struct req *req, void *ctx, const char *str)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AN(ctx);
	if (str != NULL) {
		SHA256_Update(ctx, str, strlen(str));
		VSLb(req->vsl, SLT_Hash, "%s", str);
	} else
		SHA256_Update(ctx, &str, 1);
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
HSH_Insert(struct worker *wrk, const void *digest, struct objcore *oc,
    struct ban *ban)
{
	struct objhead *oh;
	struct rush rush;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AN(digest);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AN(ban);
	AN(oc->flags & OC_F_BUSY);
	AZ(oc->flags & OC_F_PRIVATE);
	assert(oc->refcnt == 1);
	INIT_OBJ(&rush, RUSH_MAGIC);

	hsh_prealloc(wrk);

	AN(wrk->nobjhead);
	oh = hash->lookup(wrk, digest, &wrk->nobjhead);
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	Lck_AssertHeld(&oh->mtx);
	assert(oh->refcnt > 0);

	/* Mark object busy and insert (precreated) objcore in
	   objecthead. The new object inherits our objhead reference. */
	oc->objhead = oh;
	VTAILQ_INSERT_TAIL(&oh->objcs, oc, hsh_list);
	oc->refcnt++;				// For EXP_Insert
	Lck_Unlock(&oh->mtx);

	BAN_RefBan(oc, ban);
	AN(oc->ban);
	EXP_Insert(wrk, oc);

	/* Move the object first in the oh list, unbusy it and run the
	   waitinglist if necessary */
	Lck_Lock(&oh->mtx);
	VTAILQ_REMOVE(&oh->objcs, oc, hsh_list);
	VTAILQ_INSERT_HEAD(&oh->objcs, oc, hsh_list);
	oc->flags &= ~OC_F_BUSY;
	if (!VTAILQ_EMPTY(&oh->waitinglist))
		hsh_rush1(wrk, oh, &rush, HSH_RUSH_POLICY);
	Lck_Unlock(&oh->mtx);
	hsh_rush2(wrk, &rush);
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
	VTAILQ_INSERT_TAIL(&oh->objcs, oc, hsh_list);
	return (oc);
}

/*---------------------------------------------------------------------
 */

enum lookup_e
HSH_Lookup(struct req *req, struct objcore **ocp, struct objcore **bocp,
    int always_insert)
{
	struct worker *wrk;
	struct objhead *oh;
	struct objcore *oc;
	struct objcore *exp_oc;
	double exp_t_origin;
	int busy_found;
	enum lookup_e retval;
	const uint8_t *vary;

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
	exp_oc = NULL;
	exp_t_origin = 0.0;
	VTAILQ_FOREACH(oc, &oh->objcs, hsh_list) {
		/* Must be at least our own ref + the objcore we examine */
		assert(oh->refcnt > 1);
		CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
		assert(oc->objhead == oh);
		assert(oc->refcnt > 0);

		if (oc->flags & OC_F_DYING)
			continue;
		if (oc->flags & OC_F_FAILED)
			continue;

		if (oc->boc != NULL && oc->boc->state < BOS_STREAM) {
			CHECK_OBJ_ORNULL(oc->boc, BOC_MAGIC);

			if (req->hash_ignore_busy)
				continue;

			if (oc->boc->vary != NULL &&
			    !VRY_Match(req, oc->boc->vary))
				continue;

			busy_found = 1;
			continue;
		}

		if (oc->ttl <= 0.)
			continue;

		if (BAN_CheckObject(wrk, oc, req)) {
			oc->flags |= OC_F_DYING;
			EXP_Remove(oc);
			continue;
		}

		if (ObjHasAttr(wrk, oc, OA_VARY)) {
			vary = ObjGetAttr(wrk, oc, OA_VARY, NULL);
			AN(vary);
			if (!VRY_Match(req, vary))
				continue;
		}

		if (EXP_Ttl(req, oc) >= req->t_req) {
			/* If still valid, use it */
			assert(oh->refcnt > 1);
			assert(oc->objhead == oh);
			if (oc->flags & OC_F_HFP) {
				wrk->stats->cache_hitpass++;
				VSLb(req->vsl, SLT_HitPass, "%u",
				     ObjGetXID(wrk, oc));
				oc = NULL;
			} else if (oc->flags & OC_F_PASS) {
				wrk->stats->cache_hitmiss++;
				VSLb(req->vsl, SLT_HitMiss, "%u",
				     ObjGetXID(wrk, oc));
				oc = NULL;
				*bocp = hsh_insert_busyobj(wrk, oh);
			} else {
				oc->refcnt++;
				if (oc->hits < LONG_MAX)
					oc->hits++;
			}
			Lck_Unlock(&oh->mtx);
			if (oc == NULL)
				return (HSH_MISS);
			assert(HSH_DerefObjHead(wrk, &oh));
			*ocp = oc;
			return (HSH_HIT);
		}
		if (oc->t_origin > exp_t_origin) {
			/* record the newest object */
			exp_oc = oc;
			exp_t_origin = oc->t_origin;
		}
	}

	if (exp_oc != NULL && exp_oc->flags & OC_F_PASS) {
		wrk->stats->cache_hitmiss++;
		VSLb(req->vsl, SLT_HitMiss, "%u", ObjGetXID(wrk, exp_oc));
		exp_oc = NULL;
		busy_found = 0;
	}

	if (exp_oc != NULL) {
		assert(oh->refcnt > 1);
		assert(exp_oc->objhead == oh);
		exp_oc->refcnt++;

		if (!busy_found) {
			*bocp = hsh_insert_busyobj(wrk, oh);
			retval = HSH_EXPBUSY;
		} else {
			AZ(req->hash_ignore_busy);
			retval = HSH_EXP;
		}
		if (exp_oc->hits < LONG_MAX)
			exp_oc->hits++;
		Lck_Unlock(&oh->mtx);
		if (retval == HSH_EXP)
			assert(HSH_DerefObjHead(wrk, &oh));
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

	VTAILQ_INSERT_TAIL(&oh->waitinglist, req, w_list);
	if (DO_DEBUG(DBG_WAITINGLIST))
		VSLb(req->vsl, SLT_Debug, "on waiting list <%p>", oh);

	wrk->stats->busy_sleep++;
	/*
	 * The objhead reference transfers to the sess, we get it
	 * back when the sess comes off the waiting list and
	 * calls us again
	 */
	req->hash_objhead = oh;
	req->wrk = NULL;
	req->waitinglist = 1;
	Lck_Unlock(&oh->mtx);
	return (HSH_BUSY);
}

/*---------------------------------------------------------------------
 * Pick the req's we are going to rush from the waiting list
 */

static void
hsh_rush1(const struct worker *wrk, struct objhead *oh, struct rush *r, int max)
{
	unsigned u;
	struct req *req;

	if (max == 0)
		return;
	if (max == HSH_RUSH_POLICY)
		max = cache_param->rush_exponent;
	assert(max > 0);

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	CHECK_OBJ_NOTNULL(r, RUSH_MAGIC);
	VTAILQ_INIT(&r->reqs);
	Lck_AssertHeld(&oh->mtx);
	for (u = 0; u < max; u++) {
		req = VTAILQ_FIRST(&oh->waitinglist);
		if (req == NULL)
			break;
		CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
		wrk->stats->busy_wakeup++;
		AZ(req->wrk);
		VTAILQ_REMOVE(&oh->waitinglist, req, w_list);
		VTAILQ_INSERT_TAIL(&r->reqs, req, w_list);
		req->waitinglist = 0;
	}
}

/*---------------------------------------------------------------------
 * Rush req's that came from waiting list.
 */

static void
hsh_rush2(struct worker *wrk, struct rush *r)
{
	struct req *req;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(r, RUSH_MAGIC);

	while (!VTAILQ_EMPTY(&r->reqs)) {
		req = VTAILQ_FIRST(&r->reqs);
		CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
		VTAILQ_REMOVE(&r->reqs, req, w_list);
		DSL(DBG_WAITINGLIST, req->vsl->wid, "off waiting list");
		AN(req->transport->reembark);
		req->transport->reembark(wrk, req);
	}
}

/*---------------------------------------------------------------------
 * Purge an entire objhead
 */

void
HSH_Purge(struct worker *wrk, struct objhead *oh, double ttl, double grace,
double keep)
{
	struct objcore *oc, **ocp;
	unsigned spc, ospc, nobj, n;
	int more = 0;
	double now;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	ospc = WS_Reserve(wrk->aws, 0);
	assert(ospc >= sizeof *ocp);
	do {
		more = 0;
		spc = ospc;
		nobj = 0;
		ocp = (void*)wrk->aws->f;
		Lck_Lock(&oh->mtx);
		assert(oh->refcnt > 0);
		now = VTIM_real();
		VTAILQ_FOREACH(oc, &oh->objcs, hsh_list) {
			CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
			assert(oc->objhead == oh);
			if (oc->flags & OC_F_BUSY) {
				/*
				 * We cannot purge busy objects here, because
				 * their owners have special rights to them,
				 * and may nuke them without concern for the
				 * refcount, which by definition always must
				 * be one, so they don't check.
				 */
				continue;
			}
			if (oc->flags & OC_F_DYING)
				continue;
			if (spc < sizeof *ocp) {
				/* Iterate if aws is not big enough */
				more = 1;
				break;
			}
			oc->refcnt++;
			spc -= sizeof *ocp;
			ocp[nobj++] = oc;
		}
		Lck_Unlock(&oh->mtx);

		for (n = 0; n < nobj; n++) {
			oc = ocp[n];
			CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
			EXP_Rearm(oc, now, ttl, grace, keep);
			(void)HSH_DerefObjCore(wrk, &oc, 0);
		}
	} while (more);
	WS_Release(wrk->aws, 0);
	Pool_PurgeStat(nobj);
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
	assert((oc->flags & OC_F_BUSY) || (oc->stobj->stevedore != NULL));

	Lck_Lock(&oh->mtx);
	oc->flags |= OC_F_FAILED;
	Lck_Unlock(&oh->mtx);
}

/*---------------------------------------------------------------------
 * Abandon a fetch we will not need
 */

void
HSH_Abandon(struct objcore *oc)
{
	struct objhead *oh;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	oh = oc->objhead;
	CHECK_OBJ(oh, OBJHEAD_MAGIC);

	Lck_Lock(&oh->mtx);
	oc->flags |= OC_F_ABANDON;
	Lck_Unlock(&oh->mtx);
}

/*---------------------------------------------------------------------
 * Unbusy an objcore when the object is completely fetched.
 */

void
HSH_Unbusy(struct worker *wrk, struct objcore *oc)
{
	struct objhead *oh;
	struct rush rush;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	oh = oc->objhead;
	CHECK_OBJ(oh, OBJHEAD_MAGIC);
	INIT_OBJ(&rush, RUSH_MAGIC);

	AN(oc->stobj->stevedore);
	AN(oc->flags & OC_F_BUSY);
	assert(oh->refcnt > 0);
	assert(oc->refcnt > 0);

	if (!(oc->flags & OC_F_PRIVATE)) {
		BAN_NewObjCore(oc);
		AN(oc->ban);
	}

	/* XXX: pretouch neighbors on oh->objcs to prevent page-on under mtx */
	Lck_Lock(&oh->mtx);
	assert(oh->refcnt > 0);
	assert(oc->refcnt > 0);
	if (!(oc->flags & OC_F_PRIVATE))
		oc->refcnt++;			// For EXP_Insert
	/* XXX: strictly speaking, we should sort in Date: order. */
	VTAILQ_REMOVE(&oh->objcs, oc, hsh_list);
	VTAILQ_INSERT_HEAD(&oh->objcs, oc, hsh_list);
	oc->flags &= ~OC_F_BUSY;
	if (!VTAILQ_EMPTY(&oh->waitinglist))
		hsh_rush1(wrk, oh, &rush, HSH_RUSH_POLICY);
	Lck_Unlock(&oh->mtx);
	if (!(oc->flags & OC_F_PRIVATE))
		EXP_Insert(wrk, oc);
	hsh_rush2(wrk, &rush);
}

/*====================================================================
 * HSH_Kill()
 *
 * It's dead Jim, kick it...
 */

void
HSH_Kill(struct objcore *oc)
{

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(oc->objhead, OBJHEAD_MAGIC);

	Lck_Lock(&oc->objhead->mtx);
	oc->flags |= OC_F_DYING;
	Lck_Unlock(&oc->objhead->mtx);
	EXP_Remove(oc);
}

/*====================================================================
 * HSH_Snipe()
 *
 * If objcore is idle, gain a ref and mark it dead.
 */

int
HSH_Snipe(const struct worker *wrk, struct objcore *oc)
{
	int retval = 0;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(oc->objhead, OBJHEAD_MAGIC);

	if (oc->refcnt == 1 && !Lck_Trylock(&oc->objhead->mtx)) {
		if (oc->refcnt == 1 && !(oc->flags & OC_F_DYING)) {
			oc->flags |= OC_F_DYING;
			oc->refcnt++;
			retval = 1;
		}
		Lck_Unlock(&oc->objhead->mtx);
	}
	if (retval)
		EXP_Remove(oc);
	return (retval);
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

struct boc *
HSH_RefBoc(const struct objcore *oc)
{
	struct objhead *oh;
	struct boc *boc;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	oh = oc->objhead;
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	if (oc->boc == NULL)
		return (NULL);
	Lck_Lock(&oh->mtx);
	assert(oc->refcnt > 0);
	boc = oc->boc;
	CHECK_OBJ_ORNULL(boc, BOC_MAGIC);
	if (boc != NULL) {
		assert(boc->refcount > 0);
		if (boc->state < BOS_FINISHED)
			boc->refcount++;
		else
			boc = NULL;
	}
	Lck_Unlock(&oh->mtx);
	return (boc);
}

void
HSH_DerefBoc(struct worker *wrk, struct objcore *oc)
{
	struct boc *boc;
	unsigned r;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	boc = oc->boc;
	CHECK_OBJ_NOTNULL(boc, BOC_MAGIC);
	Lck_Lock(&oc->objhead->mtx);
	assert(oc->refcnt > 0);
	assert(boc->refcount > 0);
	r = --boc->refcount;
	if (r == 0)
		oc->boc = NULL;
	Lck_Unlock(&oc->objhead->mtx);
	if (r == 0)
		ObjBocDone(wrk, oc, &boc);
}

/*--------------------------------------------------------------------
 * Dereference objcore
 *
 * Returns zero if target was destroyed.
 */

int
HSH_DerefObjCore(struct worker *wrk, struct objcore **ocp, int rushmax)
{
	struct objcore *oc;
	struct objhead *oh;
	struct rush rush;
	unsigned r;

	AN(ocp);
	oc = *ocp;
	*ocp = NULL;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	assert(oc->refcnt > 0);
	INIT_OBJ(&rush, RUSH_MAGIC);

	oh = oc->objhead;
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);

	Lck_Lock(&oh->mtx);
	assert(oh->refcnt > 0);
	r = --oc->refcnt;
	if (!r)
		VTAILQ_REMOVE(&oh->objcs, oc, hsh_list);
	if (!VTAILQ_EMPTY(&oh->waitinglist))
		hsh_rush1(wrk, oh, &rush, rushmax);
	Lck_Unlock(&oh->mtx);
	hsh_rush2(wrk, &rush);
	if (r != 0)
		return (r);

	AZ(oc->exp_flags);

	BAN_DestroyObj(oc);
	AZ(oc->ban);

	if (oc->stobj->stevedore != NULL)
		ObjFreeObj(wrk, oc);
	ObjDestroy(wrk, &oc);

	/* Drop our ref on the objhead */
	assert(oh->refcnt > 0);
	(void)HSH_DerefObjHead(wrk, &oh);
	return (0);
}

int
HSH_DerefObjHead(struct worker *wrk, struct objhead **poh)
{
	struct objhead *oh;
	struct rush rush;
	int r;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	TAKE_OBJ_NOTNULL(oh, poh, OBJHEAD_MAGIC);
	INIT_OBJ(&rush, RUSH_MAGIC);

	if (oh == private_oh) {
		assert(VTAILQ_EMPTY(&oh->waitinglist));
		Lck_Lock(&oh->mtx);
		assert(oh->refcnt > 1);
		oh->refcnt--;
		Lck_Unlock(&oh->mtx);
		return(1);
	}

	/*
	 * Make absolutely certain that we do not let the final ref
	 * disappear until the waitinglist is empty.
	 * This is necessary because the req's on the waiting list do
	 * not hold any ref on the objhead of their own, and we cannot
	 * just make the hold the same ref's as objcore, that would
	 * confuse hashers.
	 */
	Lck_Lock(&oh->mtx);
	while (oh->refcnt == 1 && !VTAILQ_EMPTY(&oh->waitinglist)) {
		hsh_rush1(wrk, oh, &rush, HSH_RUSH_ALL);
		Lck_Unlock(&oh->mtx);
		hsh_rush2(wrk, &rush);
		Lck_Lock(&oh->mtx);
	}
	Lck_Unlock(&oh->mtx);

	assert(oh->refcnt > 0);
	r = hash->deref(oh);
	if (!r)
		HSH_DeleteObjHead(wrk, oh);
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
