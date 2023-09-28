/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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

#include "cache_varnishd.h"

#include "cache/cache_objhead.h"
#include "cache/cache_transport.h"

#include "hash/hash_slinger.h"

#include "vsha256.h"

struct rush {
	unsigned		magic;
#define RUSH_MAGIC		0xa1af5f01
	VTAILQ_HEAD(,req)	reqs;
};

static const struct hash_slinger *hash;
static struct objhead *private_oh;

static void hsh_rush1(const struct worker *, struct objcore *,
    struct rush *, int);
static void hsh_rush2(struct worker *, struct rush *);
static int hsh_deref_objhead(struct worker *wrk, struct objhead **poh);
static int hsh_deref_objhead_unlock(struct worker *wrk, struct objhead **poh);

/*---------------------------------------------------------------------*/

#define VCF_RETURN(x) const struct vcf_return VCF_##x[1] = { \
	{ .name = #x, } \
};

VCF_RETURNS()
#undef VCF_RETURN

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

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	if (wrk->wpriv->nobjcore == NULL)
		wrk->wpriv->nobjcore = ObjNew(wrk);
	CHECK_OBJ_NOTNULL(wrk->wpriv->nobjcore, OBJCORE_MAGIC);

	if (wrk->wpriv->nobjhead == NULL) {
		wrk->wpriv->nobjhead = hsh_newobjhead();
		wrk->stats->n_objecthead++;
	}
	CHECK_OBJ_NOTNULL(wrk->wpriv->nobjhead, OBJHEAD_MAGIC);

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
HSH_Cleanup(const struct worker *wrk)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(wrk->wpriv, WORKER_PRIV_MAGIC);
	if (wrk->wpriv->nobjcore != NULL)
		ObjDestroy(wrk, &wrk->wpriv->nobjcore);

	if (wrk->wpriv->nobjhead != NULL) {
		CHECK_OBJ(wrk->wpriv->nobjhead, OBJHEAD_MAGIC);
		Lck_Delete(&wrk->wpriv->nobjhead->mtx);
		FREE_OBJ(wrk->wpriv->nobjhead);
		wrk->stats->n_objecthead--;
	}
	if (wrk->wpriv->nhashpriv != NULL) {
		/* XXX: If needed, add slinger method for this */
		free(wrk->wpriv->nhashpriv);
		wrk->wpriv->nhashpriv = NULL;
	}
}

void
HSH_DeleteObjHead(const struct worker *wrk, struct objhead *oh)
{
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);

	AZ(oh->refcnt);
	assert(VTAILQ_EMPTY(&oh->objcs));
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
		VSHA256_Update(ctx, str, strlen(str));
		VSLbs(req->vsl, SLT_Hash, TOSTRAND(str));
	} else
		VSHA256_Update(ctx, &str, 1);
}

/*---------------------------------------------------------------------
 * This is a debugging hack to enable testing of boundary conditions
 * in the hash algorithm.
 * We trap the first 9 different digests and translate them to different
 * digests with edge bit conditions
 */

static struct hsh_magiclist {
	unsigned char was[VSHA256_LEN];
	unsigned char now[VSHA256_LEN];
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
	size_t i, j;
	static size_t nused = 0;

	for (i = 0; i < nused; i++)
		if (!memcmp(hsh_magiclist[i].was, result, VSHA256_LEN))
			break;
	if (i == nused && i < HSH_NMAGIC)
		memcpy(hsh_magiclist[nused++].was, result, VSHA256_LEN);
	if (i == nused)
		return;
	assert(i < HSH_NMAGIC);
	fprintf(stderr, "HASHMAGIC: <");
	for (j = 0; j < VSHA256_LEN; j++)
		fprintf(stderr, "%02x", ((unsigned char*)result)[j]);
	fprintf(stderr, "> -> <");
	memcpy(result, hsh_magiclist[i].now, VSHA256_LEN);
	for (j = 0; j < VSHA256_LEN; j++)
		fprintf(stderr, "%02x", ((unsigned char*)result)[j]);
	fprintf(stderr, ">\n");
}

/*---------------------------------------------------------------------
 * Insert an object which magically appears out of nowhere or, more likely,
 * comes off some persistent storage device.
 * Insert it with a reference held.
 */

void
HSH_Insert(struct worker *wrk, const void *digest, struct objcore *oc,
    struct ban *ban)
{
	struct objhead *oh;
	struct rush rush;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(wrk->wpriv, WORKER_PRIV_MAGIC);
	AN(digest);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AN(ban);
	AN(oc->flags & OC_F_BUSY);
	AZ(oc->flags & OC_F_PRIVATE);
	assert(oc->refcnt == 1);
	INIT_OBJ(&rush, RUSH_MAGIC);

	hsh_prealloc(wrk);

	AN(wrk->wpriv->nobjhead);
	oh = hash->lookup(wrk, digest, &wrk->wpriv->nobjhead);
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	Lck_AssertHeld(&oh->mtx);
	assert(oh->refcnt > 0);

	/* Mark object busy and insert (precreated) objcore in
	   objecthead. The new object inherits our objhead reference. */
	oc->objhead = oh;
	VTAILQ_INSERT_TAIL(&oh->objcs, oc, hsh_list);
	EXP_RefNewObjcore(oc);
	Lck_Unlock(&oh->mtx);

	BAN_RefBan(oc, ban);
	AN(oc->ban);

	/* Move the object first in the oh list, unbusy it and run the
	   waitinglist if necessary */
	Lck_Lock(&oh->mtx);
	VTAILQ_REMOVE(&oh->objcs, oc, hsh_list);
	VTAILQ_INSERT_HEAD(&oh->objcs, oc, hsh_list);
	oc->flags &= ~OC_F_BUSY;
	if (!VTAILQ_EMPTY(&oc->waitinglist))
		hsh_rush1(wrk, oc, &rush, HSH_RUSH_POLICY);
	Lck_Unlock(&oh->mtx);
	hsh_rush2(wrk, &rush);

	EXP_Insert(wrk, oc);
}

/*---------------------------------------------------------------------
 */

static struct objcore *
hsh_insert_busyobj(const struct worker *wrk, struct objhead *oh)
{
	struct objcore *oc;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(wrk->wpriv, WORKER_PRIV_MAGIC);
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	Lck_AssertHeld(&oh->mtx);

	oc = wrk->wpriv->nobjcore;
	wrk->wpriv->nobjcore = NULL;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	AN(oc->flags & OC_F_BUSY);
	oc->refcnt = 1;		/* Owned by busyobj */
	oc->objhead = oh;
	VTAILQ_INSERT_TAIL(&oh->objcs, oc, hsh_list);
	return (oc);
}

/*---------------------------------------------------------------------
 */

static unsigned
hsh_rush_match(struct req *req)
{
	struct objcore *oc;
	const uint8_t *vary;

	oc = req->hash_oc;
	CHECK_OBJ_ORNULL(oc, OBJCORE_MAGIC);

	if (oc == NULL)
		return (0);

	AZ(oc->flags & OC_F_BUSY);
	if (oc->flags != 0)
		return (0);

	/* The objcore was serviceable, all the requests on the waiting list
	 * were rushed at once.
	 */
	assert(VTAILQ_EMPTY(&oc->waitinglist));

	if (req->hash_ignore_vary)
		return (1);
	if (!ObjHasAttr(req->wrk, oc, OA_VARY))
		return (1);

	vary = ObjGetAttr(req->wrk, oc, OA_VARY, NULL);
	AN(vary);
	return (VRY_Match(req, vary));
}

/*---------------------------------------------------------------------
 */

static enum lookup_e
hsh_objhead_lookup(struct objhead *oh, struct req *req, struct objcore **ocp,
    struct objcore **bocp)
{
	struct worker *wrk;
	struct objcore *oc;
	struct objcore *exp_oc;
	struct objcore *busy_oc;
	const struct vcf_return *vr;
	vtim_real exp_t_origin;
	const uint8_t *vary;

	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	Lck_AssertHeld(&oh->mtx);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	wrk = req->wrk;
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AN(ocp);
	*ocp = NULL;
	AN(bocp);
	*bocp = NULL;

	if (req->hash_always_miss)
		return (HSH_MISS);

	assert(oh->refcnt > 0);
	busy_oc = NULL;
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

		CHECK_OBJ_ORNULL(oc->boc, BOC_MAGIC);
		if (oc->flags & OC_F_BUSY) {
			if (busy_oc != NULL)
				continue;
			if (req->hash_ignore_busy)
				continue;

			if (oc->boc != NULL && oc->boc->vary != NULL &&
			    !req->hash_ignore_vary &&
			    !VRY_Match(req, oc->boc->vary)) {
				wrk->strangelove++;
				continue;
			}

			busy_oc = oc;
			continue;
		}

		if (oc->ttl <= 0.)
			continue;

		if (BAN_CheckObject(wrk, oc, req)) {
			oc->flags |= OC_F_DYING;
			EXP_Remove(oc);
			continue;
		}

		if (!req->hash_ignore_vary && ObjHasAttr(wrk, oc, OA_VARY)) {
			vary = ObjGetAttr(wrk, oc, OA_VARY, NULL);
			AN(vary);
			if (!VRY_Match(req, vary)) {
				wrk->strangelove++;
				continue;
			}
		}

		if (req->vcf != NULL) {
			vr = req->vcf->func(req, &oc, &exp_oc, 0);
			if (vr == VCF_CONTINUE)
				continue;
			if (vr == VCF_MISS) {
				oc = NULL;
				break;
			}
			if (vr == VCF_HIT)
				break;
			assert(vr == VCF_DEFAULT);
		}

		if (EXP_Ttl(req, oc) > req->t_req) {
			assert(oh->refcnt > 1);
			assert(oc->objhead == oh);
			break;
		}

		if (EXP_Ttl(NULL, oc) < req->t_req && /* ignore req.ttl */
		    oc->t_origin > exp_t_origin) {
			/* record the newest object */
			exp_oc = oc;
			exp_t_origin = oc->t_origin;
			assert(oh->refcnt > 1);
			assert(exp_oc->objhead == oh);
		}
	}

	if (req->vcf != NULL)
		(void)req->vcf->func(req, &oc, &exp_oc, 1);

	if (oc != NULL && oc->flags & OC_F_HFP) {
		*ocp = oc;
		return (HSH_HITPASS);
	}

	if (oc != NULL) {
		*ocp = oc;
		oc->refcnt++;
		if (oc->flags & OC_F_HFM)
			return (HSH_HITMISS);
		return (HSH_HIT);
	}

	if (exp_oc != NULL && exp_oc->flags & OC_F_HFM) {
		/*
		 * expired HFM ("grace/keep HFM")
		 *
		 * XXX should HFM objects actually have grace/keep ?
		 */
		*ocp = exp_oc;
		return (HSH_HITMISS_EXP);
	}

	if (busy_oc == NULL) {
		if (exp_oc == NULL)
			return (HSH_MISS);
		*ocp = exp_oc;
		exp_oc->refcnt++;
		if (EXP_Ttl_grace(req, exp_oc) >= req->t_req)
			return (HSH_GRACE);
		return (HSH_MISS_EXP);
	}

	CHECK_OBJ_NOTNULL(busy_oc, OBJCORE_MAGIC);
	*bocp = busy_oc;

	if (exp_oc != NULL && EXP_Ttl_grace(req, exp_oc) >= req->t_req) {
		/* we do not wait on the busy object if in grace */
		*ocp = exp_oc;
		exp_oc->refcnt++;
		return (HSH_GRACE);
	}

	return (HSH_BUSY);
}

/*---------------------------------------------------------------------
 */

enum lookup_e
HSH_Lookup(struct req *req, struct objcore **ocp, struct objcore **bocp)
{
	enum lookup_e lr;
	struct worker *wrk;
	struct objhead *oh;
	struct objcore *oc;
	struct objcore *busy_oc;
	intmax_t boc_progress;
	unsigned xid = 0;
	float dttl = 0.0;

	AN(ocp);
	*ocp = NULL;
	AN(bocp);
	*bocp = NULL;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	wrk = req->wrk;
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(wrk->wpriv, WORKER_PRIV_MAGIC);
	CHECK_OBJ_NOTNULL(req->http, HTTP_MAGIC);
	CHECK_OBJ_ORNULL(req->vcf, VCF_MAGIC);
	AN(hash);

	hsh_prealloc(wrk);
	if (DO_DEBUG(DBG_HASHEDGE))
		hsh_testmagic(req->digest);

	if (hsh_rush_match(req)) {
		TAKE_OBJ_NOTNULL(oc, &req->hash_oc, OBJCORE_MAGIC);
		*ocp = oc;
		oc->hits++;
		boc_progress = oc->boc == NULL ? -1 : oc->boc->fetched_so_far;
		Req_LogHit(wrk, req, oc, boc_progress);
		/* NB: since this hit comes from the waiting list instead of
		 * a regular lookup, grace is not considered. The object is
		 * fresh in the context of the waiting list, even expired: it
		 * was successfully just [re]validated by a fetch task.
		 */
		return (HSH_HIT);
	}

	if (req->hash_oc != NULL) {
		/*
		 * This req came off the waiting list, and brings an
		 * incompatible hash_oc refcnt with it.
		 */
		CHECK_OBJ_NOTNULL(req->hash_oc->objhead, OBJHEAD_MAGIC);
		oh = req->hash_oc->objhead;
		/* XXX: can we avoid grabbing the oh lock twice here? */
		(void)HSH_DerefObjCore(wrk, &req->hash_oc, HSH_RUSH_POLICY);
		Lck_Lock(&oh->mtx);
	} else {
		AN(wrk->wpriv->nobjhead);
		oh = hash->lookup(wrk, req->digest, &wrk->wpriv->nobjhead);
	}

	lr = hsh_objhead_lookup(oh, req, &oc, &busy_oc);
	switch (lr) {
	case HSH_MISS:
	case HSH_MISS_EXP:
		if (lr == HSH_MISS)
			AZ(oc);
		else
			CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
		AZ(busy_oc);
		*bocp = hsh_insert_busyobj(wrk, oh);
		Lck_Unlock(&oh->mtx);
		break;
	case HSH_HITMISS:
	case HSH_HITMISS_EXP:
		CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
		AZ(busy_oc);
		xid = VXID(ObjGetXID(wrk, oc));
		dttl = EXP_Dttl(req, oc);
		*bocp = hsh_insert_busyobj(wrk, oh);
		Lck_Unlock(&oh->mtx);
		wrk->stats->cache_hitmiss++;
		VSLb(req->vsl, SLT_HitMiss, "%u %.6f", xid, dttl);
		if (lr == HSH_HITMISS_EXP)
			oc = NULL;
		break;
	case HSH_HITPASS:
		CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
		AZ(busy_oc);
		xid = VXID(ObjGetXID(wrk, oc));
		dttl = EXP_Dttl(req, oc);
		AN(hsh_deref_objhead_unlock(wrk, &oh));
		wrk->stats->cache_hitpass++;
		VSLb(req->vsl, SLT_HitPass, "%u %.6f", xid, dttl);
		oc = NULL;
		break;
	case HSH_HIT:
		CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
		AZ(busy_oc);
		oc->hits++;
		boc_progress = oc->boc == NULL ? -1 : oc->boc->fetched_so_far;
		AN(hsh_deref_objhead_unlock(wrk, &oh));
		Req_LogHit(wrk, req, oc, boc_progress);
		break;
	case HSH_GRACE:
		CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
		CHECK_OBJ_ORNULL(busy_oc, OBJCORE_MAGIC);
		oc->hits++;
		boc_progress = oc->boc == NULL ? -1 : oc->boc->fetched_so_far;
		if (busy_oc == NULL) {
			*bocp = hsh_insert_busyobj(wrk, oh);
			Lck_Unlock(&oh->mtx);
		} else {
			/* we do not wait on the busy object if in grace */
			AN(hsh_deref_objhead_unlock(wrk, &oh));
			busy_oc = NULL;
		}
		Req_LogHit(wrk, req, oc, boc_progress);
		break;
	case HSH_BUSY:
		AZ(oc);
		CHECK_OBJ_NOTNULL(busy_oc, OBJCORE_MAGIC);
		AZ(req->hash_ignore_busy);

		/* There are one or more busy objects, wait for them */
		VTAILQ_INSERT_TAIL(&busy_oc->waitinglist, req, w_list);

		/*
		 * The objcore reference transfers to the req, we get it
		 * back when the req comes off the waiting list and calls
		 * us again
		 */
		AZ(req->hash_oc);
		req->hash_oc = busy_oc;
		req->wrk = NULL;
		req->waitinglist = 1;

		if (DO_DEBUG(DBG_WAITINGLIST))
			VSLb(req->vsl, SLT_Debug,
			    "on waiting list <%p>", busy_oc);

		Lck_Unlock(&oh->mtx);

		wrk->stats->busy_sleep++;
		break;
	default:
		WRONG("Invalid lookup result");
	}

	if (oc != NULL)
		*ocp = oc;

	if (busy_oc != NULL) {
		assert(busy_oc->refcnt > 0);
		busy_oc->refcnt++;
	}

	return (lr);
}

/*---------------------------------------------------------------------
 * Pick the req's we are going to rush from the waiting list
 */

static void
hsh_rush1(const struct worker *wrk, struct objcore *oc, struct rush *r, int max)
{
	int i;
	struct req *req;

	if (max == 0)
		return;
	if (max == HSH_RUSH_POLICY) {
		AZ(oc->flags & OC_F_BUSY);
		if (oc->flags != 0)
			max = cache_param->rush_exponent;
		else
			max = INT_MAX;
	}
	assert(max > 0);

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(r, RUSH_MAGIC);
	VTAILQ_INIT(&r->reqs);
	Lck_AssertHeld(&oc->objhead->mtx);
	for (i = 0; i < max; i++) {
		req = VTAILQ_FIRST(&oc->waitinglist);
		if (req == NULL)
			break;
		CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
		wrk->stats->busy_wakeup++;
		AZ(req->wrk);
		VTAILQ_REMOVE(&oc->waitinglist, req, w_list);
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
		if (req->transport->reembark != NULL) {
			// For ESI includes
			req->transport->reembark(wrk, req);
		} else {
			/*
			 * We ignore the queue limits which apply to new
			 * requests because if we fail to reschedule there
			 * may be vmod_privs to cleanup and we need a proper
			 * workerthread for that.
			 */
			AZ(Pool_Task(req->sp->pool, req->task, TASK_QUEUE_RUSH));
		}
	}
}

/*---------------------------------------------------------------------
 * Purge an entire objhead
 */

unsigned
HSH_Purge(struct worker *wrk, struct objhead *oh, vtim_real ttl_now,
    vtim_dur ttl, vtim_dur grace, vtim_dur keep)
{
	struct objcore *oc, *oc_nows[2], **ocp;
	unsigned i, j, n, n_max, total = 0;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);

	n_max = WS_ReserveLumps(wrk->aws, sizeof *ocp);
	if (n_max < 2) {
		/* No space on the workspace. Give it a stack buffer of 2
		 * elements, which is the minimum for the algorithm
		 * below. */
		ocp = oc_nows;
		n_max = 2;
	} else
		ocp = WS_Reservation(wrk->aws);
	AN(ocp);

	/* Note: This algorithm uses OC references in the list as
	 * bookmarks, in order to know how far into the list we were when
	 * releasing the mutex partway through and want to resume
	 * again. This relies on the list not being reordered while we are
	 * not holding the mutex. The only place where that happens is in
	 * HSH_Unbusy(), where an OC_F_BUSY OC is moved first in the
	 * list. This does not cause problems because we skip OC_F_BUSY
	 * OCs. */

	Lck_Lock(&oh->mtx);
	oc = VTAILQ_FIRST(&oh->objcs);
	n = 0;
	while (1) {
		for (; n < n_max && oc != NULL; oc = VTAILQ_NEXT(oc, hsh_list))
		{
			CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
			assert(oc->objhead == oh);
			if (oc->flags & OC_F_BUSY) {
				/* We cannot purge busy objects here, because
				 * their owners have special rights to them,
				 * and may nuke them without concern for the
				 * refcount, which by definition always must
				 * be one, so they don't check. */
				continue;
			}
			if (oc->flags & OC_F_DYING)
				continue;
			oc->refcnt++;
			ocp[n++] = oc;
		}

		Lck_Unlock(&oh->mtx);

		if (n == 0) {
			/* No eligible objcores found. We are finished. */
			break;
		}

		j = n;
		if (oc != NULL) {
			/* There are more objects on the objhead that we
			 * have not yet looked at, but no more space on
			 * the objcore reference list. Do not process the
			 * last one, it will be used as the bookmark into
			 * the objcore list for the next iteration of the
			 * outer loop. */
			j--;
			assert(j >= 1); /* True because n_max >= 2 */
		}
		for (i = 0; i < j; i++) {
			CHECK_OBJ_NOTNULL(ocp[i], OBJCORE_MAGIC);
			EXP_Rearm(ocp[i], ttl_now, ttl, grace, keep);
			(void)HSH_DerefObjCore(wrk, &ocp[i], 0);
			AZ(ocp[i]);
			total++;
		}

		if (j == n) {
			/* No bookmark set, that means we got to the end
			 * of the objcore list in the previous run and are
			 * finished. */
			break;
		}

		Lck_Lock(&oh->mtx);

		/* Move the bookmark first and continue scanning the
		 * objcores */
		CHECK_OBJ_NOTNULL(ocp[j], OBJCORE_MAGIC);
		ocp[0] = ocp[j];
		n = 1;
		oc = VTAILQ_NEXT(ocp[0], hsh_list);
		CHECK_OBJ_ORNULL(oc, OBJCORE_MAGIC);
	}

	WS_Release(wrk->aws, 0);
	Pool_PurgeStat(total);
	return (total);
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
 * Mark a fetch we will not need as cancelled
 */

static void
hsh_cancel(struct objcore *oc)
{
	struct objhead *oh;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	oh = oc->objhead;
	CHECK_OBJ(oh, OBJHEAD_MAGIC);

	Lck_Lock(&oh->mtx);
	oc->flags |= OC_F_CANCEL;
	Lck_Unlock(&oh->mtx);
}

/*---------------------------------------------------------------------
 * Cancel a fetch when the client does not need it any more
 */

void
HSH_Cancel(struct worker *wrk, struct objcore *oc, struct boc *boc)
{
	struct boc *bocref = NULL;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	if ((oc->flags & (OC_F_PRIVATE | OC_F_HFM | OC_F_HFP)) == 0)
		return;

	/*
	 * NB: we use two distinct variables to only release the reference if
	 * we had to acquire one. The caller-provided boc is optional.
	 */
	if (boc == NULL)
		bocref = boc = HSH_RefBoc(oc);

	CHECK_OBJ_ORNULL(boc, BOC_MAGIC);

	if (oc->flags & OC_F_HFP)
		AN(oc->flags & OC_F_HFM);

	if (boc != NULL) {
		hsh_cancel(oc);
		ObjWaitState(oc, BOS_FINISHED);
	}

	if (bocref != NULL)
		HSH_DerefBoc(wrk, oc);

	ObjSlim(wrk, oc);
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
	CHECK_OBJ_NOTNULL(oc->boc, BOC_MAGIC);

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
		EXP_RefNewObjcore(oc); /* Takes a ref for expiry */
	/* XXX: strictly speaking, we should sort in Date: order. */
	VTAILQ_REMOVE(&oh->objcs, oc, hsh_list);
	VTAILQ_INSERT_HEAD(&oh->objcs, oc, hsh_list);
	oc->flags &= ~OC_F_BUSY;
	if (!VTAILQ_EMPTY(&oc->waitinglist)) {
		assert(oc->refcnt > 1);
		hsh_rush1(wrk, oc, &rush, HSH_RUSH_POLICY);
	}
	Lck_Unlock(&oh->mtx);
	EXP_Insert(wrk, oc); /* Does nothing unless EXP_RefNewObjcore was
			      * called */
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
	int r;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	TAKE_OBJ_NOTNULL(oc, ocp, OBJCORE_MAGIC);
	assert(oc->refcnt > 0);
	INIT_OBJ(&rush, RUSH_MAGIC);

	oh = oc->objhead;
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);

	Lck_Lock(&oh->mtx);
	assert(oh->refcnt > 0);
	r = --oc->refcnt;
	if (!r)
		VTAILQ_REMOVE(&oh->objcs, oc, hsh_list);
	if (!VTAILQ_EMPTY(&oc->waitinglist)) {
		assert(oc->refcnt > 0);
		hsh_rush1(wrk, oc, &rush, rushmax);
	}
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
	(void)hsh_deref_objhead(wrk, &oh);
	return (0);
}

static int
hsh_deref_objhead_unlock(struct worker *wrk, struct objhead **poh)
{
	struct objhead *oh;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	TAKE_OBJ_NOTNULL(oh, poh, OBJHEAD_MAGIC);

	Lck_AssertHeld(&oh->mtx);

	if (oh == private_oh) {
		assert(oh->refcnt > 1);
		oh->refcnt--;
		Lck_Unlock(&oh->mtx);
		return (1);
	}

	assert(oh->refcnt > 0);
	return (hash->deref(wrk, oh)); /* Unlocks oh->mtx */
}

static int
hsh_deref_objhead(struct worker *wrk, struct objhead **poh)
{
	struct objhead *oh;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	TAKE_OBJ_NOTNULL(oh, poh, OBJHEAD_MAGIC);

	Lck_Lock(&oh->mtx);
	return (hsh_deref_objhead_unlock(wrk, &oh));
}

void
HSH_Init(const struct hash_slinger *slinger)
{

	assert(DIGEST_LEN == VSHA256_LEN);	/* avoid #include pollution */
	hash = slinger;
	if (hash->start != NULL)
		hash->start();
	private_oh = hsh_newobjhead();
	private_oh->refcnt = 1;
}
