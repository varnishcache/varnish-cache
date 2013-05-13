/*-
 * Copyright (c) 2011 Varnish Software AS
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
	Lck_Delete(&bo->mtx);
	MPL_Free(vbopool, bo);
}

struct busyobj *
VBO_GetBusyObj(struct worker *wrk, struct req *req)
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
	VSLb(bo->vsl, SLT_Link, "req %u", req->vsl->wid & VSL_IDENTMASK);
	VSLb(req->vsl, SLT_Link, "bereq %u", bo->vsl->wid & VSL_IDENTMASK);
	p += sz;
	p = (void*)PRNDUP(p);
	assert(p < bo->end);

	WS_Init(bo->ws, "bo", p, bo->end - p);

	memcpy(bo->digest, req->digest, sizeof bo->digest);

	bo->do_stream = 1;

	bo->director = req->director;
	bo->vcl = req->vcl;
	VCL_Ref(bo->vcl);

	bo->t_fetch = req->t_req;

	return (bo);
}

void
VBO_DerefBusyObj(struct worker *wrk, struct busyobj **pbo)
{
	struct busyobj *bo;
	struct objcore *oc;
	unsigned r;

	CHECK_OBJ_ORNULL(wrk, WORKER_MAGIC);
	AN(pbo);
	bo = *pbo;
	*pbo = NULL;
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_ORNULL(bo->fetch_obj, OBJECT_MAGIC);
	if (bo->fetch_obj != NULL && bo->fetch_obj->objcore->objhead != NULL) {
		oc = bo->fetch_obj->objcore;
		CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
		CHECK_OBJ_NOTNULL(oc->objhead, OBJHEAD_MAGIC);
		Lck_Lock(&oc->objhead->mtx);
		assert(bo->refcount > 0);
		r = --bo->refcount;
		Lck_Unlock(&oc->objhead->mtx);
	} else {
		oc = NULL;
		Lck_Lock(&bo->mtx);
		assert(bo->refcount > 0);
		r = --bo->refcount;
		Lck_Unlock(&bo->mtx);
	}

	if (r)
		return;

	VSL_Flush(bo->vsl, 0);

	if (oc != NULL) {
		AN(wrk);
		(void)HSH_Deref(&wrk->stats, NULL, &bo->fetch_obj);
	}

	VCL_Rel(&bo->vcl);

	memset(&bo->refcount, 0,
	    sizeof *bo - offsetof(struct busyobj, refcount));

	if (cache_param->bo_cache && wrk != NULL && wrk->nbo == NULL)
		wrk->nbo = bo;
	else
		VBO_Free(&bo);
}

void
VBO_extend(const struct busyobj *bo, ssize_t l)
{

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->fetch_obj, OBJECT_MAGIC);
	if (l == 0)
		return;
	assert(l > 0);
	bo->fetch_obj->len += l;
}
