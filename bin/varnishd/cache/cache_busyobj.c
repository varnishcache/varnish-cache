/*-
 * Copyright (c) 2013 Varnish Software AS
 * All rights reserved.
 *
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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
 * Handle backend connections and backend request structures.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include "cache.h"

#include "hash/hash_slinger.h"

static struct mempool		*vbopool;

/*--------------------------------------------------------------------
 */

void
VBO_Init(void)
{

	vbopool = MPL_New("busyobj", &cache_param->vbo_pool,
	    &cache_param->workspace_backend);
	AN(vbopool);
}

/*--------------------------------------------------------------------
 * BusyObj handling
 */

static struct busyobj *
vbo_New(void)
{
	struct busyobj *bo;
	unsigned sz;

	bo = MPL_Get(vbopool, &sz);
	XXXAN(bo);
	bo->magic = BUSYOBJ_MAGIC;
	bo->end = (char *)bo + sz;
	Lck_New(&bo->mtx, lck_busyobj);
	AZ(pthread_cond_init(&bo->cond, NULL));
	return (bo);
}

void
VBO_Free(struct busyobj **bop)
{
	struct busyobj *bo;

	AN(bop);
	bo = *bop;
	*bop = NULL;
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	AZ(bo->refcount);
	AZ(pthread_cond_destroy(&bo->cond));
	Lck_Delete(&bo->mtx);
	MPL_Free(vbopool, bo);
}

struct busyobj *
VBO_GetBusyObj(struct worker *wrk, const struct req *req)
{
	struct busyobj *bo = NULL;
	uint16_t nhttp;
	unsigned sz;
	char *p;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	if (wrk->nbo != NULL) {
		bo = wrk->nbo;
		wrk->nbo = NULL;
	}

	if (bo == NULL)
		bo = vbo_New();

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	AZ(bo->refcount);

	bo->refcount = 1;

	p = (void*)(bo + 1);
	p = (void*)PRNDUP(p);
	assert(p < bo->end);

	nhttp = (uint16_t)cache_param->http_max_hdr;
	sz = HTTP_estimate(nhttp);

	bo->bereq0 = HTTP_create(p, nhttp);
	p += sz;
	p = (void*)PRNDUP(p);
	assert(p < bo->end);

	bo->bereq = HTTP_create(p, nhttp);
	p += sz;
	p = (void*)PRNDUP(p);
	assert(p < bo->end);

	bo->beresp = HTTP_create(p, nhttp);
	p += sz;
	p = (void*)PRNDUP(p);
	assert(p < bo->end);

	sz = cache_param->vsl_buffer;
	VSL_Setup(bo->vsl, p, sz);
	bo->vsl->wid = VXID_Get(&wrk->vxid_pool) | VSL_BACKENDMARKER;
	p += sz;
	p = (void*)PRNDUP(p);
	assert(p < bo->end);

	WS_Init(bo->ws, "bo", p, bo->end - p);

	bo->do_stream = 1;

	bo->director = req->director_hint;
	bo->vcl = req->vcl;
	VCL_Ref(bo->vcl);

	bo->t_fetch = req->t_req;
	assert(!isnan(bo->t_fetch) && bo->t_fetch != 0.);
	bo->t_first = bo->t_prev = NAN;

	return (bo);
}

void
VBO_DerefBusyObj(struct worker *wrk, struct busyobj **pbo)
{
	struct busyobj *bo;
	struct objcore *oc = NULL;
	unsigned r;

	CHECK_OBJ_ORNULL(wrk, WORKER_MAGIC);
	AN(pbo);
	bo = *pbo;
	*pbo = NULL;
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_ORNULL(bo->fetch_objcore, OBJCORE_MAGIC);
	CHECK_OBJ_ORNULL(bo->fetch_obj, OBJECT_MAGIC);
	if (bo->fetch_objcore != NULL) {
		oc = bo->fetch_objcore;
		CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
		CHECK_OBJ_NOTNULL(oc->objhead, OBJHEAD_MAGIC);
		Lck_Lock(&oc->objhead->mtx);
		assert(bo->refcount > 0);
		r = --bo->refcount;
		Lck_Unlock(&oc->objhead->mtx);
	} else {
		Lck_Lock(&bo->mtx);
		assert(bo->refcount > 0);
		r = --bo->refcount;
		Lck_Unlock(&bo->mtx);
	}

	if (r)
		return;

	VSLb(bo->vsl, SLT_BereqAcct, "%ju %ju %ju %ju %ju %ju",
	    (uintmax_t)bo->acct.bereq_hdrbytes,
	    (uintmax_t)bo->acct.bereq_bodybytes,
	    (uintmax_t)(bo->acct.bereq_hdrbytes + bo->acct.bereq_bodybytes),
	    (uintmax_t)bo->acct.beresp_hdrbytes,
	    (uintmax_t)bo->acct.beresp_bodybytes,
	    (uintmax_t)(bo->acct.beresp_hdrbytes + bo->acct.beresp_bodybytes));

	VSLb(bo->vsl, SLT_End, "%s", "");
	VSL_Flush(bo->vsl, 0);

	if (bo->fetch_objcore != NULL) {
		AN(wrk);
		(void)HSH_DerefObjCore(&wrk->stats, &bo->fetch_objcore);
	}

	VCL_Rel(&bo->vcl);

	if (bo->vary != NULL)
		free(bo->vary);

	memset(&bo->refcount, 0,
	    sizeof *bo - offsetof(struct busyobj, refcount));

	if (cache_param->bo_cache && wrk != NULL && wrk->nbo == NULL)
		wrk->nbo = bo;
	else
		VBO_Free(&bo);
}

void
VBO_extend(struct busyobj *bo, ssize_t l)
{
	struct storage *st;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->fetch_obj, OBJECT_MAGIC);
	if (l == 0)
		return;
	assert(l > 0);
	Lck_Lock(&bo->mtx);
	st = VTAILQ_LAST(&bo->fetch_obj->store, storagehead);
	CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
	st->len += l;
	bo->fetch_obj->len += l;
	AZ(pthread_cond_broadcast(&bo->cond));
	Lck_Unlock(&bo->mtx);
}

ssize_t
VBO_waitlen(struct busyobj *bo, ssize_t l)
{
	Lck_Lock(&bo->mtx);
	assert(l <= bo->fetch_obj->len || bo->state == BOS_FAILED);
	while (1) {
		if (bo->fetch_obj->len > l || bo->state >= BOS_FINISHED)
			break;
		(void)Lck_CondWait(&bo->cond, &bo->mtx, 0);
	}
	l = bo->fetch_obj->len;
	Lck_Unlock(&bo->mtx);
	return (l);
}

void
VBO_setstate(struct busyobj *bo, enum busyobj_state_e next)
{
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	assert(bo->do_stream || next != BOS_STREAM);
	assert(next > bo->state);
	Lck_Lock(&bo->mtx);
	bo->state = next;
	AZ(pthread_cond_broadcast(&bo->cond));
	Lck_Unlock(&bo->mtx);
}

void
VBO_waitstate(struct busyobj *bo, enum busyobj_state_e want)
{
	Lck_Lock(&bo->mtx);
	while (1) {
		if (bo->state >= want)
			break;
		(void)Lck_CondWait(&bo->cond, &bo->mtx, 0);
	}
	Lck_Unlock(&bo->mtx);
}
