/*-
 * Copyright (c) 2007-2015 Varnish Software AS
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
 */

#include "config.h"

#include <stdlib.h>

#include "cache/cache_varnishd.h"

#include "cache/cache_obj.h"
#include "cache/cache_objhead.h"

#include "storage/storage.h"
#include "storage/storage_simple.h"

#include "vtim.h"

/* Flags for allocating memory in sml_stv_alloc */
#define LESS_MEM_ALLOCED_IS_OK	1

// marker pointer for sml_trimstore
static void *trim_once = &trim_once;
// for delayed return of hdl->last resume pointer
static void *null_iov = &null_iov;

/*-------------------------------------------------------------------*/

static struct storage *
objallocwithnuke(struct worker *, const struct stevedore *, ssize_t size,
    int flags);

static struct storage *
sml_stv_alloc(const struct stevedore *stv, ssize_t size, int flags)
{
	struct storage *st;

	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
	AN(stv->sml_alloc);

	if (!(flags & LESS_MEM_ALLOCED_IS_OK)) {
		if (size > cache_param->fetch_maxchunksize)
			return (NULL);
		else
			return (stv->sml_alloc(stv, size));
	}

	if (size > cache_param->fetch_maxchunksize)
		size = cache_param->fetch_maxchunksize;

	assert(size <= UINT_MAX);	/* field limit in struct storage */

	for (;;) {
		/* try to allocate from it */
		assert(size > 0);
		st = stv->sml_alloc(stv, size);
		if (st != NULL)
			break;

		if (size <= cache_param->fetch_chunksize)
			break;

		size /= 2;
	}
	CHECK_OBJ_ORNULL(st, STORAGE_MAGIC);
	return (st);
}

static void
sml_stv_free(const struct stevedore *stv, struct storage *st)
{

	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
	CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
	if (stv->sml_free != NULL)
		stv->sml_free(st);
}

/*--------------------------------------------------------------------
 * This function is called by stevedores ->allocobj() method, which
 * very often will be SML_allocobj() below, to convert a slab
 * of storage into object which the stevedore can then register in its
 * internal state, before returning it to STV_NewObject().
 * As you probably guessed: All this for persistence.
 */

struct object *
SML_MkObject(const struct stevedore *stv, struct objcore *oc, void *ptr)
{
	struct object *o;

	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
	AN(stv->methods);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	assert(PAOK(ptr));

	o = ptr;
	INIT_OBJ(o, OBJECT_MAGIC);

	VTAILQ_INIT(&o->list);

	oc->stobj->stevedore = stv;
	oc->stobj->priv = o;
	oc->stobj->priv2 = 0;
	return (o);
}

/*--------------------------------------------------------------------
 * This is the default ->allocobj() which all stevedores who do not
 * implement persistent storage can rely on.
 */

int v_matchproto_(storage_allocobj_f)
SML_allocobj(struct worker *wrk, const struct stevedore *stv,
    struct objcore *oc, unsigned wsl)
{
	struct object *o;
	struct storage *st = NULL;
	unsigned ltot;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	AN(stv->sml_alloc);

	ltot = sizeof(*o) + PRNDUP(wsl);

	do {
		st = stv->sml_alloc(stv, ltot);
		if (st != NULL && st->space < ltot) {
			stv->sml_free(st);
			st = NULL;
		}
	} while (st == NULL && LRU_NukeOne(wrk, stv->lru));
	if (st == NULL)
		return (0);

	CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
	o = SML_MkObject(stv, oc, st->ptr);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	st->len = sizeof(*o);
	o->objstore = st;
	return (1);
}

void * v_matchproto_(storage_allocbuf_t)
SML_AllocBuf(struct worker *wrk, const struct stevedore *stv, size_t size,
    uintptr_t *ppriv)
{
	struct storage *st;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
	AN(ppriv);

	if (size > UINT_MAX)
		return (NULL);
	st = objallocwithnuke(wrk, stv, size, 0);
	if (st == NULL)
		return (NULL);
	assert(st->space >= size);
	st->flags = STORAGE_F_BUFFER;
	st->len = size;
	*ppriv = (uintptr_t)st;
	return (st->ptr);
}

void v_matchproto_(storage_freebuf_t)
SML_FreeBuf(struct worker *wrk, const struct stevedore *stv, uintptr_t priv)
{
	struct storage *st;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);

	CAST_OBJ_NOTNULL(st, (void *)priv, STORAGE_MAGIC);
	assert(st->flags == STORAGE_F_BUFFER);
	sml_stv_free(stv, st);
}

/*---------------------------------------------------------------------
 */

static struct object *
sml_getobj(struct worker *wrk, struct objcore *oc)
{
	const struct stevedore *stv;
	struct object *o;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	stv = oc->stobj->stevedore;
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
	if (stv->sml_getobj != NULL)
		return (stv->sml_getobj(wrk, oc));
	if (oc->stobj->priv == NULL)
		return (NULL);
	CAST_OBJ_NOTNULL(o, oc->stobj->priv, OBJECT_MAGIC);
	return (o);
}

static void v_matchproto_(objslim_f)
sml_slim(struct worker *wrk, struct objcore *oc)
{
	const struct stevedore *stv;
	struct object *o;
	struct storage *st, *stn;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	stv = oc->stobj->stevedore;
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
	o = sml_getobj(wrk, oc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);

#define OBJ_AUXATTR(U, l)					\
	do {							\
		if (o->aa_##l != NULL) {			\
			sml_stv_free(stv, o->aa_##l);		\
			o->aa_##l = NULL;			\
		}						\
	} while (0);
#include "tbl/obj_attr.h"

	VTAILQ_FOREACH_SAFE(st, &o->list, list, stn) {
		CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
		VTAILQ_REMOVE(&o->list, st, list);
		sml_stv_free(stv, st);
	}
}

static void
sml_bocfini(const struct stevedore *stv, struct boc *boc)
{
	struct storage *st;

	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
	CHECK_OBJ_NOTNULL(boc, BOC_MAGIC);

	if (boc->stevedore_priv == NULL ||
	    boc->stevedore_priv == trim_once)
		return;

	/* Free any leftovers from Trim */
	TAKE_OBJ_NOTNULL(st, &boc->stevedore_priv, STORAGE_MAGIC);
	sml_stv_free(stv, st);
}

/*
 * called in two cases:
 * - oc->boc == NULL: cache object on LRU freed
 * - oc->boc != NULL: cache object replaced for backend error
 */
static void v_matchproto_(objfree_f)
sml_objfree(struct worker *wrk, struct objcore *oc)
{
	const struct stevedore *stv;
	struct storage *st;
	struct object *o;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	stv = oc->stobj->stevedore;
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
	CAST_OBJ_NOTNULL(o, oc->stobj->priv, OBJECT_MAGIC);

	sml_slim(wrk, oc);
	st = o->objstore;
	CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
	FINI_OBJ(o);

	if (oc->boc != NULL)
		sml_bocfini(stv, oc->boc);
	else if (stv->lru != NULL)
		LRU_Remove(oc);

	sml_stv_free(stv, st);

	memset(oc->stobj, 0, sizeof oc->stobj);

	wrk->stats->n_object--;
}

// kept for reviewers - XXX remove later
#undef VAI_DBG

struct sml_hdl {
	struct vai_hdl_preamble	preamble;
#define SML_HDL_MAGIC		0x37dfd996
	struct vai_qe		qe;
	struct pool_task	task;	// unfortunate
	struct ws		*ws;	// NULL is malloc()
	struct objcore		*oc;
	struct object		*obj;
	const struct stevedore	*stv;
	struct boc		*boc;

	struct storage		*st;	// updated by _lease()

	// only for _lease_boc()
	uint64_t		st_off;	// already returned fragment of current st
	uint64_t		avail, returned;
	struct storage		*last;	// to resume, held back by _return()
};

static inline uint64_t
st2lease(const struct storage *st)
{
	uint64_t r = (uintptr_t)st;

	if (sizeof(void *) < 8) //lint !e506 !e774
		r <<= 1;

	return (r);
}

static inline struct storage *
lease2st(uint64_t l)
{

	if (sizeof(void *) < 8) //lint !e506 !e774
		l >>= 1;

	return ((void *)(uintptr_t)l);
}

static inline void
sml_ai_viov_fill(struct viov *viov, struct storage *st)
{
	viov->iov.iov_base = TRUST_ME(st->ptr);
	viov->iov.iov_len = st->len;
	viov->lease = st2lease(st);
	VAI_ASSERT_LEASE(viov->lease);
}

// sml has no mechanism to notify "I got free space again now"
// (we could add that, but because storage.h is used in mgt, a first attempt
//  looks at least like this would cause some include spill for vai_q_head or
//  something similar)
//
// So anyway, to get ahead we just implement a pretty stupid "call the notify
// some time later" on a thread
static void
sml_ai_later_task(struct worker *wrk, void *priv)
{
	struct sml_hdl *hdl;
	const vtim_dur dur = 0.0042;

	(void)wrk;
	VTIM_sleep(dur);
	CAST_VAI_HDL_NOTNULL(hdl, priv, SML_HDL_MAGIC);
	memset(&hdl->task, 0, sizeof hdl->task);
	hdl->qe.cb(hdl, hdl->qe.priv);
}
static void
sml_ai_later(struct worker *wrk, struct sml_hdl *hdl)
{
	AZ(hdl->task.func);
	AZ(hdl->task.priv);
	hdl->task.func = sml_ai_later_task;
	hdl->task.priv = hdl;
	AZ(Pool_Task(wrk->pool, &hdl->task, TASK_QUEUE_BG));
}


static int
sml_ai_buffer(struct worker *wrk, vai_hdl vhdl, struct vscarab *scarab)
{
	const struct stevedore *stv;
	struct sml_hdl *hdl;
	struct storage *st;
	struct viov *vio;
	int r = 0;

	(void) wrk;
	CAST_VAI_HDL_NOTNULL(hdl, vhdl, SML_HDL_MAGIC);
	stv = hdl->stv;
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);

	VSCARAB_FOREACH(vio, scarab)
		if (vio->iov.iov_len > UINT_MAX)
			return (-EINVAL);

	VSCARAB_FOREACH(vio, scarab) {
		st = objallocwithnuke(wrk, stv, vio->iov.iov_len, 0);
		if (st == NULL)
			break;
		assert(st->space >= vio->iov.iov_len);
		st->flags = STORAGE_F_BUFFER;
		st->len = st->space;

		sml_ai_viov_fill(vio, st);
		r++;
	}
	if (r == 0) {
		sml_ai_later(wrk, hdl);
		r = -EAGAIN;
	}
	return (r);
}

static int
sml_ai_lease_simple(struct worker *wrk, vai_hdl vhdl, struct vscarab *scarab)
{
	struct storage *st;
	struct sml_hdl *hdl;
	struct viov *viov;
	int r = 0;

	(void) wrk;
	CAST_VAI_HDL_NOTNULL(hdl, vhdl, SML_HDL_MAGIC);
	VSCARAB_CHECK_NOTNULL(scarab);

	AZ(hdl->st_off);
	st = hdl->st;
	while (st != NULL && (viov = VSCARAB_GET(scarab)) != NULL) {
		CHECK_OBJ(st, STORAGE_MAGIC);
		sml_ai_viov_fill(viov, st);
		r++;
		st = VTAILQ_PREV(st, storagehead, list);
	}
	hdl->st = st;
	if (st == NULL)
		scarab->flags |= VSCARAB_F_END;
	return (r);
}

/*
 * on leases while streaming (with a boc):
 *
 * SML uses the lease return facility to implement the "free behind" for
 * OC_F_TRANSIENT objects. When streaming, we also return leases on
 * fragments of sts, but we must only "free behind" when we are done with the
 * last fragment.
 *
 * So we use a magic lease to signal "this is only a fragment", which we ignore
 * on returns
 */

static int
sml_ai_lease_boc(struct worker *wrk, vai_hdl vhdl, struct vscarab *scarab)
{
	enum boc_state_e state = BOS_INVALID;
	struct storage *next;
	struct sml_hdl *hdl;
	struct viov *viov;
	int r = 0;

	CAST_VAI_HDL_NOTNULL(hdl, vhdl, SML_HDL_MAGIC);
	VSCARAB_CHECK_NOTNULL(scarab);

	if (hdl->avail == hdl->returned) {
		hdl->avail = ObjVAIGetExtend(wrk, hdl->oc, hdl->returned,
		    &state, &hdl->qe);
		if (state == BOS_FAILED) {
			hdl->last = NULL;
			return (-EPIPE);
		}
		else if (state == BOS_FINISHED)
			(void)0;
		else if (hdl->avail == hdl->returned) {
			// ObjVAIGetExtend() has scheduled a notification
			if (hdl->boc->transit_buffer > 0)
				return (-ENOBUFS);
			else
				return (-EAGAIN);
		}
		else
			assert(state < BOS_FINISHED);
	}
	Lck_Lock(&hdl->boc->mtx);
	if (hdl->st == NULL && hdl->last != NULL)
		hdl->st = VTAILQ_PREV(hdl->last, storagehead, list);
	if (hdl->last != NULL && state < BOS_FINISHED) {
		viov = VSCARAB_GET(scarab);
		AN(viov);
		viov->iov.iov_base = null_iov;
		viov->iov.iov_len = 0;
		viov->lease = st2lease(hdl->last);
		hdl->last = NULL;
	}
	if (hdl->st == NULL) {
		assert(hdl->returned == 0 || hdl->avail == hdl->returned);
		hdl->st = VTAILQ_LAST(&hdl->obj->list, storagehead);
	}
	if (hdl->st == NULL)
		assert(hdl->avail == hdl->returned);

	while (hdl->avail > hdl->returned && (viov = VSCARAB_GET(scarab)) != NULL) {
		CHECK_OBJ_NOTNULL(hdl->st, STORAGE_MAGIC); // ObjVAIGetExtend ensures
		assert(hdl->st_off <= hdl->st->space);
		size_t av = hdl->avail - hdl->returned;
		size_t l = hdl->st->space - hdl->st_off;
		AN(l);
		if (l > av)
			l = av;
		viov->iov.iov_base = TRUST_ME(hdl->st->ptr + hdl->st_off);
		viov->iov.iov_len = l;
		if (hdl->st_off + l == hdl->st->space) {
			next = VTAILQ_PREV(hdl->st, storagehead, list);
			AZ(hdl->last);
			if (next == NULL) {
				hdl->last = hdl->st;
				viov->lease = VAI_LEASE_NORET;
			}
			else {
				CHECK_OBJ(next, STORAGE_MAGIC);
				viov->lease = st2lease(hdl->st);
			}
#ifdef VAI_DBG
			VSLb(wrk->vsl, SLT_Debug, "off %zu + l %zu == space st %p next st %p stvprv %p",
			    hdl->st_off, l, hdl->st, next, hdl->boc->stevedore_priv);
#endif
			hdl->st_off = 0;
			hdl->st = next;
		}
		else {
			viov->lease = VAI_LEASE_NORET;
			hdl->st_off += l;
		}
		hdl->returned += l;
		VAI_ASSERT_LEASE(viov->lease);
		r++;
	}

	Lck_Unlock(&hdl->boc->mtx);
	if (state != BOS_FINISHED && hdl->avail == hdl->returned) {
		hdl->avail = ObjVAIGetExtend(wrk, hdl->oc, hdl->returned,
		    &state, &hdl->qe);
	}
	if (state == BOS_FINISHED && hdl->avail == hdl->returned)
		scarab->flags |= VSCARAB_F_END;
	return (r);
}

// return only buffers, used if object is not streaming
static void v_matchproto_(vai_return_f)
sml_ai_return_buffers(struct worker *wrk, vai_hdl vhdl, struct vscaret *scaret)
{
	struct storage *st;
	struct sml_hdl *hdl;
	uint64_t *p;

	(void) wrk;
	CAST_VAI_HDL_NOTNULL(hdl, vhdl, SML_HDL_MAGIC);

	VSCARET_FOREACH(p, scaret) {
		if (*p == VAI_LEASE_NORET)
			continue;
		CAST_OBJ_NOTNULL(st, lease2st(*p), STORAGE_MAGIC);
		if ((st->flags & STORAGE_F_BUFFER) == 0)
			continue;
		sml_stv_free(hdl->stv, st);
	}
	VSCARET_INIT(scaret, scaret->capacity);
}

// generic return for buffers and object leases, used when streaming
static void v_matchproto_(vai_return_f)
sml_ai_return(struct worker *wrk, vai_hdl vhdl, struct vscaret *scaret)
{
	struct storage *st;
	struct sml_hdl *hdl;
	uint64_t *p;

	(void) wrk;
	CAST_VAI_HDL_NOTNULL(hdl, vhdl, SML_HDL_MAGIC);
	VSCARET_CHECK_NOTNULL(scaret);
	if (scaret->used == 0)
		return;

	// callback is only registered if needed
	assert(hdl->boc != NULL && (hdl->oc->flags & OC_F_TRANSIENT) != 0);

	// filter noret and last
	VSCARET_LOCAL(todo, scaret->used);
	VSCARET_FOREACH(p, scaret) {
		if (*p == VAI_LEASE_NORET)
			continue;
		CAST_OBJ_NOTNULL(st, lease2st(*p), STORAGE_MAGIC);
		VSCARET_ADD(todo, *p);
	}
	VSCARET_INIT(scaret, scaret->capacity);

	Lck_Lock(&hdl->boc->mtx);
	VSCARET_FOREACH(p, todo) {
		CAST_OBJ_NOTNULL(st, lease2st(*p), STORAGE_MAGIC);
		if ((st->flags & STORAGE_F_BUFFER) != 0)
			continue;
		VTAILQ_REMOVE(&hdl->obj->list, st, list);
		if (st == hdl->boc->stevedore_priv)
			hdl->boc->stevedore_priv = trim_once;
	}
	Lck_Unlock(&hdl->boc->mtx);

	VSCARET_FOREACH(p, todo) {
		CAST_OBJ_NOTNULL(st, lease2st(*p), STORAGE_MAGIC);
#ifdef VAI_DBG
		VSLb(wrk->vsl, SLT_Debug, "ret %p", st);
#endif
		sml_stv_free(hdl->stv, st);
	}
}

static void v_matchproto_(vai_fini_f)
sml_ai_fini(struct worker *wrk, vai_hdl *vai_hdlp)
{
	struct sml_hdl *hdl;

	AN(vai_hdlp);
	CAST_VAI_HDL_NOTNULL(hdl, *vai_hdlp, SML_HDL_MAGIC);
	*vai_hdlp = NULL;

	if (hdl->boc != NULL) {
		ObjVAICancel(wrk, hdl->boc, &hdl->qe);
		HSH_DerefBoc(wrk, hdl->oc);
		hdl->boc = NULL;
	}

	if (hdl->ws != NULL)
		WS_Release(hdl->ws, 0);
	else
		free(hdl);
}

static vai_hdl v_matchproto_(vai_init_f)
sml_ai_init(struct worker *wrk, struct objcore *oc, struct ws *ws,
    vai_notify_cb *notify, void *notify_priv)
{
	struct sml_hdl *hdl;
	const size_t sz = sizeof *hdl;

	if (ws != NULL && WS_ReserveSize(ws, (unsigned)sz))
		hdl = WS_Reservation(ws);
	else {
		hdl = malloc(sz);
		ws = NULL;
	}

	AN(hdl);
	INIT_VAI_HDL(hdl, SML_HDL_MAGIC);
	hdl->preamble.vai_lease = sml_ai_lease_simple;
	hdl->preamble.vai_buffer = sml_ai_buffer;
	hdl->preamble.vai_return = sml_ai_return_buffers;
	hdl->preamble.vai_fini = sml_ai_fini;
	hdl->ws = ws;

	hdl->oc = oc;
	hdl->obj = sml_getobj(wrk, oc);
	CHECK_OBJ_NOTNULL(hdl->obj, OBJECT_MAGIC);
	hdl->stv = oc->stobj->stevedore;
	CHECK_OBJ_NOTNULL(hdl->stv, STEVEDORE_MAGIC);

	hdl->st = VTAILQ_LAST(&hdl->obj->list, storagehead);
	CHECK_OBJ_ORNULL(hdl->st, STORAGE_MAGIC);

	hdl->qe.magic = VAI_Q_MAGIC;
	hdl->qe.cb = notify;
	hdl->qe.hdl = hdl;
	hdl->qe.priv = notify_priv;

	hdl->boc = HSH_RefBoc(oc);
	if (hdl->boc == NULL)
		return (hdl);
	/* we only initialize notifications if we have a boc, so
	 * any wrong attempt triggers magic checks.
	 */
	hdl->preamble.vai_lease = sml_ai_lease_boc;
	if ((hdl->oc->flags & OC_F_TRANSIENT) != 0)
		hdl->preamble.vai_return = sml_ai_return;
	return (hdl);
}

/*
 * trivial notification to allow the iterator to simply block
 */
struct sml_notify {
	unsigned		magic;
#define SML_NOTIFY_MAGIC	0x4589af31
	unsigned		hasmore;
	pthread_mutex_t		mtx;
	pthread_cond_t		cond;
};

static void
sml_notify_init(struct sml_notify *sn)
{

	INIT_OBJ(sn, SML_NOTIFY_MAGIC);
	AZ(pthread_mutex_init(&sn->mtx, NULL));
	AZ(pthread_cond_init(&sn->cond, NULL));
}

static void
sml_notify_fini(struct sml_notify *sn)
{

	CHECK_OBJ_NOTNULL(sn, SML_NOTIFY_MAGIC);
	AZ(pthread_mutex_destroy(&sn->mtx));
	AZ(pthread_cond_destroy(&sn->cond));
}

static void v_matchproto_(vai_notify_cb)
sml_notify(vai_hdl hdl, void *priv)
{
	struct sml_notify *sn;

	(void) hdl;
	CAST_OBJ_NOTNULL(sn, priv, SML_NOTIFY_MAGIC);
	AZ(pthread_mutex_lock(&sn->mtx));
	sn->hasmore = 1;
	AZ(pthread_cond_signal(&sn->cond));
	AZ(pthread_mutex_unlock(&sn->mtx));

}

static void
sml_notify_wait(struct sml_notify *sn)
{

	CHECK_OBJ_NOTNULL(sn, SML_NOTIFY_MAGIC);
	AZ(pthread_mutex_lock(&sn->mtx));
	while (sn->hasmore == 0)
		AZ(pthread_cond_wait(&sn->cond, &sn->mtx));
	AN(sn->hasmore);
	sn->hasmore = 0;
	AZ(pthread_mutex_unlock(&sn->mtx));
}

static int v_matchproto_(objiterator_f)
sml_iterator(struct worker *wrk, struct objcore *oc,
    void *priv, objiterate_f *func, int final)
{
	struct sml_notify sn;
	struct viov *vio, *last;
	unsigned u, uu;
	vai_hdl hdl;
	int nn, r, r2, islast;

	VSCARAB_LOCAL(scarab, 16);
	VSCARET_LOCAL(scaret, 16);

	(void) final; // phase out?
	sml_notify_init(&sn);
	hdl = ObjVAIinit(wrk, oc, NULL, sml_notify, &sn);
	AN(hdl);

	r = u = 0;

	do {
		do {
			nn = ObjVAIlease(wrk, hdl, scarab);
			if (nn <= 0 || scarab->flags & VSCARAB_F_END)
				break;
		} while (scarab->used < scarab->capacity);

		/*
		 * nn is the wait/return action or 0
		 * nn tells us if to flush
		 */
		uu = u;
		last = VSCARAB_LAST(scarab);
		VSCARAB_FOREACH(vio, scarab) {
			islast = vio == last;
			AZ(u & OBJ_ITER_END);
			if (islast && scarab->flags & VSCARAB_F_END)
				u |= OBJ_ITER_END;

			// flush if it is the scarab's last IOV and we will block next
			// or if we need space in the return leases array
			uu = u;
			if ((islast && nn < 0) || scaret->used == scaret->capacity - 1)
				uu |= OBJ_ITER_FLUSH;

			// null iov with the only purpose to return the resume ptr lease
			// exception needed because assert(len > 0) in VDP_bytes()
			if (vio->iov.iov_base == null_iov)
				r = 0;
			else
				r = func(priv, uu, vio->iov.iov_base, vio->iov.iov_len);
			if (r != 0)
				break;

			// sufficient space ensured by capacity check above
			VSCARET_ADD(scaret, vio->lease);

#ifdef VAI_DBG
			VSLb(wrk->vsl, SLT_Debug, "len %zu scaret %u uu %u",
			    vio->iov.iov_len, scaret->used, uu);
#endif

			// whenever we have flushed, return leases
			if ((uu & OBJ_ITER_FLUSH) && scaret->used > 0)
				ObjVAIreturn(wrk, hdl, scaret);
		}

		// return leases which we did not use if error (break)
		VSCARAB_FOREACH_RESUME(vio, scarab) {
			if (scaret->used == scaret->capacity)
				ObjVAIreturn(wrk, hdl, scaret);
			VSCARET_ADD(scaret, vio->lease);
		}

		// we have now completed the scarab
		VSCARAB_INIT(scarab, scarab->capacity);

#ifdef VAI_DBG
		VSLb(wrk->vsl, SLT_Debug, "r %d nn %d uu %u",
		    r, nn, uu);
#endif

		// flush before blocking if we did not already
		if (r == 0 && (nn == -ENOBUFS || nn == -EAGAIN) &&
		    (uu & OBJ_ITER_FLUSH) == 0) {
			r = func(priv, OBJ_ITER_FLUSH, NULL, 0);
			if (scaret->used > 0)
				ObjVAIreturn(wrk, hdl, scaret);
		}

		if (r == 0 && (nn == -ENOBUFS || nn == -EAGAIN)) {
			assert(scaret->used <= 1);
			sml_notify_wait(&sn);
		}
		else if (r == 0 && nn < 0)
			r = -1;
	} while (nn != 0 && r == 0);

	if ((u & OBJ_ITER_END) == 0) {
		r2 = func(priv, OBJ_ITER_END, NULL, 0);
		if (r == 0)
			r = r2;
	}

	if (scaret->used > 0)
		ObjVAIreturn(wrk, hdl, scaret);

	ObjVAIfini(wrk, &hdl);
	sml_notify_fini(&sn);

	return (r);
}

/*--------------------------------------------------------------------
 */

static struct storage *
objallocwithnuke(struct worker *wrk, const struct stevedore *stv, ssize_t size,
    int flags)
{
	struct storage *st = NULL;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);

	if (size > cache_param->fetch_maxchunksize) {
		if (!(flags & LESS_MEM_ALLOCED_IS_OK))
			return (NULL);
		size = cache_param->fetch_maxchunksize;
	}

	assert(size <= UINT_MAX);	/* field limit in struct storage */

	do {
		/* try to allocate from it */
		st = sml_stv_alloc(stv, size, flags);
		if (st != NULL)
			break;

		/* no luck; try to free some space and keep trying */
		if (stv->lru == NULL)
			break;
	} while (LRU_NukeOne(wrk, stv->lru));

	CHECK_OBJ_ORNULL(st, STORAGE_MAGIC);
	return (st);
}

static int v_matchproto_(objgetspace_f)
sml_getspace(struct worker *wrk, struct objcore *oc, ssize_t *sz,
    uint8_t **ptr)
{
	struct object *o;
	struct storage *st;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(oc->boc, BOC_MAGIC);
	AN(sz);
	AN(ptr);
	if (*sz == 0)
		*sz = cache_param->fetch_chunksize;
	assert(*sz > 0);
	if (oc->boc->transit_buffer > 0)
		*sz = vmin_t(ssize_t, *sz, oc->boc->transit_buffer);

	o = sml_getobj(wrk, oc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	CHECK_OBJ_NOTNULL(oc->boc, BOC_MAGIC);

	st = VTAILQ_FIRST(&o->list);
	if (st != NULL && st->len < st->space) {
		*sz = st->space - st->len;
		*ptr = st->ptr + st->len;
		assert (*sz > 0);
		return (1);
	}

	st = objallocwithnuke(wrk, oc->stobj->stevedore, *sz,
	    LESS_MEM_ALLOCED_IS_OK);
	if (st == NULL)
		return (0);

	CHECK_OBJ_NOTNULL(oc->boc, BOC_MAGIC);
	Lck_Lock(&oc->boc->mtx);
	VTAILQ_INSERT_HEAD(&o->list, st, list);
	Lck_Unlock(&oc->boc->mtx);

	*sz = st->space - st->len;
	assert (*sz > 0);
	*ptr = st->ptr + st->len;
	return (1);
}

static void v_matchproto_(objextend_f)
sml_extend(struct worker *wrk, struct objcore *oc, ssize_t l)
{
	struct object *o;
	struct storage *st;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	assert(l > 0);

	o = sml_getobj(wrk, oc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	st = VTAILQ_FIRST(&o->list);
	CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
	assert(st->len + l <= st->space);
	st->len += l;
}

static void v_matchproto_(objtrimstore_f)
sml_trimstore(struct worker *wrk, struct objcore *oc)
{
	const struct stevedore *stv;
	struct storage *st, *st1;
	struct object *o;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(oc->boc, BOC_MAGIC);

	stv = oc->stobj->stevedore;
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);

	if (oc->boc->stevedore_priv != NULL)
		WRONG("sml_trimstore already called");
	oc->boc->stevedore_priv = trim_once;

	if (stv->sml_free == NULL)
		return;

	o = sml_getobj(wrk, oc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	st = VTAILQ_FIRST(&o->list);

	if (st == NULL)
		return;

	if (st->len == 0) {
		Lck_Lock(&oc->boc->mtx);
		VTAILQ_REMOVE(&o->list, st, list);
		Lck_Unlock(&oc->boc->mtx);
		/* sml_bocdone frees this */
		oc->boc->stevedore_priv = st;
		return;
	}

	if (st->space - st->len < 512)
		return;

	st1 = sml_stv_alloc(stv, st->len, 0);
	if (st1 == NULL)
		return;
	assert(st1->space >= st->len);

	memcpy(st1->ptr, st->ptr, st->len);
	st1->len = st->len;
	Lck_Lock(&oc->boc->mtx);
	VTAILQ_REMOVE(&o->list, st, list);
	VTAILQ_INSERT_HEAD(&o->list, st1, list);
	Lck_Unlock(&oc->boc->mtx);
	/* sml_bocdone frees this */
	oc->boc->stevedore_priv = st;
}

static void v_matchproto_(objbocdone_f)
sml_bocdone(struct worker *wrk, struct objcore *oc, struct boc *boc)
{
	const struct stevedore *stv;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(boc, BOC_MAGIC);
	stv = oc->stobj->stevedore;
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);

	sml_bocfini(stv, boc);

	if (stv->lru != NULL) {
		if (isnan(wrk->lastused))
			wrk->lastused = VTIM_real();
		LRU_Add(oc, wrk->lastused);	// approx timestamp is OK
	}
}

static const void * v_matchproto_(objgetattr_f)
sml_getattr(struct worker *wrk, struct objcore *oc, enum obj_attr attr,
   ssize_t *len)
{
	struct object *o;
	ssize_t dummy;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	if (len == NULL)
		len = &dummy;
	o = sml_getobj(wrk, oc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);

	switch (attr) {
		/* Fixed size attributes */
#define OBJ_FIXATTR(U, l, s)						\
	case OA_##U:							\
		*len = sizeof o->fa_##l;				\
		return (o->fa_##l);
#include "tbl/obj_attr.h"

		/* Variable size attributes */
#define OBJ_VARATTR(U, l)						\
	case OA_##U:							\
		if (o->va_##l == NULL)					\
			return (NULL);					\
		*len = o->va_##l##_len;					\
		return (o->va_##l);
#include "tbl/obj_attr.h"

		/* Auxiliary attributes */
#define OBJ_AUXATTR(U, l)						\
	case OA_##U:							\
		if (o->aa_##l == NULL)					\
			return (NULL);					\
		CHECK_OBJ_NOTNULL(o->aa_##l, STORAGE_MAGIC);		\
		*len = o->aa_##l->len;					\
		return (o->aa_##l->ptr);
#include "tbl/obj_attr.h"

	default:
		break;
	}
	WRONG("Unsupported OBJ_ATTR");
}

static void * v_matchproto_(objsetattr_f)
sml_setattr(struct worker *wrk, struct objcore *oc, enum obj_attr attr,
    ssize_t len, const void *ptr)
{
	struct object *o;
	void *retval = NULL;
	struct storage *st;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	o = sml_getobj(wrk, oc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	st = o->objstore;

	switch (attr) {
		/* Fixed size attributes */
#define OBJ_FIXATTR(U, l, s)						\
	case OA_##U:							\
		assert(len == sizeof o->fa_##l);			\
		retval = o->fa_##l;					\
		break;
#include "tbl/obj_attr.h"

		/* Variable size attributes */
#define OBJ_VARATTR(U, l)						\
	case OA_##U:							\
		if (o->va_##l##_len > 0) {				\
			AN(o->va_##l);					\
			assert(len == o->va_##l##_len);			\
			retval = o->va_##l;				\
		} else if (len > 0) {					\
			assert(len <= UINT_MAX);			\
			assert(st->len + len <= st->space);		\
			o->va_##l = st->ptr + st->len;			\
			st->len += len;					\
			o->va_##l##_len = len;				\
			retval = o->va_##l;				\
		}							\
		break;
#include "tbl/obj_attr.h"

		/* Auxiliary attributes */
#define OBJ_AUXATTR(U, l)						\
	case OA_##U:							\
		if (o->aa_##l != NULL) {				\
			CHECK_OBJ_NOTNULL(o->aa_##l, STORAGE_MAGIC);	\
			assert(len == o->aa_##l->len);			\
			retval = o->aa_##l->ptr;			\
			break;						\
		}							\
		if (len == 0)						\
			break;						\
		o->aa_##l = objallocwithnuke(wrk, oc->stobj->stevedore,	\
		    len, 0);						\
		if (o->aa_##l == NULL)					\
			break;						\
		CHECK_OBJ_NOTNULL(o->aa_##l, STORAGE_MAGIC);		\
		assert(len <= o->aa_##l->space);			\
		o->aa_##l->len = len;					\
		retval = o->aa_##l->ptr;				\
		break;
#include "tbl/obj_attr.h"

	default:
		WRONG("Unsupported OBJ_ATTR");
		break;
	}

	if (retval != NULL && ptr != NULL)
		memcpy(retval, ptr, len);
	return (retval);
}

const struct obj_methods SML_methods = {
	.objfree	= sml_objfree,
	.objiterator	= sml_iterator,
	.objgetspace	= sml_getspace,
	.objextend	= sml_extend,
	.objtrimstore	= sml_trimstore,
	.objbocdone	= sml_bocdone,
	.objslim	= sml_slim,
	.objgetattr	= sml_getattr,
	.objsetattr	= sml_setattr,
	.objtouch	= LRU_Touch,
	.vai_init	= sml_ai_init
};

static void
sml_panic_st(struct vsb *vsb, const char *hd, const struct storage *st)
{
	VSB_printf(vsb, "%s = %p {priv=%p, ptr=%p, len=%u, space=%u},\n",
	    hd, st, st->priv, st->ptr, st->len, st->space);
}

void
SML_panic(struct vsb *vsb, const struct objcore *oc)
{
	struct object *o;
	struct storage *st;

	VSB_printf(vsb, "Simple = %p,\n", oc->stobj->priv);
	if (oc->stobj->priv == NULL)
		return;
	o = oc->stobj->priv;
	PAN_CheckMagic(vsb, o, OBJECT_MAGIC);
	sml_panic_st(vsb, "Obj", o->objstore);

#define OBJ_FIXATTR(U, l, sz) \
	VSB_printf(vsb, "%s = ", #U); \
	VSB_quote(vsb, (const void*)o->fa_##l, sz, VSB_QUOTE_HEX); \
	VSB_printf(vsb, ",\n");

#define OBJ_VARATTR(U, l) \
	VSB_printf(vsb, "%s = {len=%u, ptr=%p},\n", \
	    #U, o->va_##l##_len, o->va_##l);

#define OBJ_AUXATTR(U, l)						\
	do {								\
		if (o->aa_##l != NULL) sml_panic_st(vsb, #U, o->aa_##l);\
	} while(0);

#include "tbl/obj_attr.h"

	VTAILQ_FOREACH(st, &o->list, list) {
		sml_panic_st(vsb, "Body", st);
	}
}
