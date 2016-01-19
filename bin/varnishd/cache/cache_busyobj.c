/*-
 * Copyright (c) 2013-2015 Varnish Software AS
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

#include "cache.h"

#include "hash/hash_slinger.h"
#include "cache/cache_filter.h"

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
	INIT_OBJ(bo->boc, BOC_MAGIC);
	Lck_New(&bo->boc->mtx, lck_busyobj);
	AZ(pthread_cond_init(&bo->boc->cond, NULL));
	return (bo);
}

static void
vbo_Free(struct busyobj **bop)
{
	struct busyobj *bo;

	AN(bop);
	bo = *bop;
	*bop = NULL;
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	AZ(bo->htc);
	AZ(bo->boc->refcount);
	AZ(pthread_cond_destroy(&bo->boc->cond));
	Lck_Delete(&bo->boc->mtx);
	MPL_Free(vbopool, bo);
}

struct busyobj *
VBO_GetBusyObj(struct worker *wrk, const struct req *req)
{
	struct busyobj *bo;
	uint16_t nhttp;
	unsigned sz;
	char *p;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	bo = vbo_New();
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	AZ(bo->boc->refcount);

	bo->boc->refcount = 1;

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
	bo->vsl->wid = VXID_Get(wrk, VSL_BACKENDMARKER);
	p += sz;
	p = (void*)PRNDUP(p);
	assert(p < bo->end);

	WS_Init(bo->ws, "bo", p, bo->end - p);

	bo->do_stream = 1;

	bo->director_req = req->director_hint;
	bo->vcl = req->vcl;
	VCL_Ref(bo->vcl);

	bo->t_first = bo->t_prev = NAN;

	memcpy(bo->digest, req->digest, sizeof bo->digest);

	VRTPRIV_init(bo->privs);

	VFP_Setup(bo->vfc);

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
	if (bo->fetch_objcore != NULL) {
		oc = bo->fetch_objcore;
		CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
		CHECK_OBJ_NOTNULL(oc->objhead, OBJHEAD_MAGIC);
		Lck_Lock(&oc->objhead->mtx);
		assert(bo->boc->refcount > 0);
		r = --bo->boc->refcount;
		Lck_Unlock(&oc->objhead->mtx);
	} else {
		Lck_Lock(&bo->boc->mtx);
		assert(bo->boc->refcount > 0);
		r = --bo->boc->refcount;
		Lck_Unlock(&bo->boc->mtx);
	}

	if (r)
		return;

	AZ(bo->htc);
	AZ(bo->stale_oc);

	VRTPRIV_dynamic_kill(bo->privs, (uintptr_t)bo);
	assert(VTAILQ_EMPTY(&bo->privs->privs));

	VSLb(bo->vsl, SLT_BereqAcct, "%ju %ju %ju %ju %ju %ju",
	    (uintmax_t)bo->acct.bereq_hdrbytes,
	    (uintmax_t)bo->acct.bereq_bodybytes,
	    (uintmax_t)(bo->acct.bereq_hdrbytes + bo->acct.bereq_bodybytes),
	    (uintmax_t)bo->acct.beresp_hdrbytes,
	    (uintmax_t)bo->acct.beresp_bodybytes,
	    (uintmax_t)(bo->acct.beresp_hdrbytes + bo->acct.beresp_bodybytes));

	VSL_End(bo->vsl);

	AZ(bo->htc);

	if (bo->fetch_objcore != NULL) {
		AN(wrk);
		if (bo->fetch_objcore->stobj->stevedore != NULL)
			ObjStable(bo->wrk, bo->fetch_objcore, bo);
		(void)HSH_DerefObjCore(wrk, &bo->fetch_objcore);
	}

	VCL_Rel(&bo->vcl);

	AZ(bo->boc->stevedore_priv);
	AZ(bo->boc->refcount);
	bo->boc->state = BOS_INVALID;
	if (bo->boc->vary != NULL) {
		free(bo->boc->vary);
		bo->boc->vary = NULL;
	}

	memset(&bo->retries, 0,
	    sizeof *bo - offsetof(struct busyobj, retries));

	vbo_Free(&bo);
}
