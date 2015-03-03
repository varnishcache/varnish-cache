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
 * Runtime support for compiled VCL programs: private variables
 */

#include "config.h"


#include <stdlib.h>

#include "cache.h"

#include "vrt.h"

struct vrt_priv {
	unsigned			magic;
#define VRT_PRIV_MAGIC			0x24157a52
	VTAILQ_ENTRY(vrt_priv)		list;
	struct vmod_priv		priv[1];
	const struct VCL_conf		*vcl;
	uintptr_t			id;
	uintptr_t			vmod_id;
};

/*--------------------------------------------------------------------
 */

void
VRTPRIV_init(struct vrt_privs *privs)
{
	privs->magic = VRT_PRIVS_MAGIC;
	VTAILQ_INIT(&privs->privs);
}

static struct vmod_priv *
VRT_priv_dynamic(VRT_CTX, uintptr_t id, uintptr_t vmod_id)
{
	struct vrt_privs *vps;
	struct vrt_priv *vp;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (ctx->req) {
		CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
		CHECK_OBJ_NOTNULL(ctx->req->sp, SESS_MAGIC);
		CAST_OBJ_NOTNULL(vps, ctx->req->sp->privs, VRT_PRIVS_MAGIC);
	} else {
		CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
		CAST_OBJ_NOTNULL(vps, ctx->bo->privs, VRT_PRIVS_MAGIC);
	}

	VTAILQ_FOREACH(vp, &vps->privs, list) {
		CHECK_OBJ_NOTNULL(vp, VRT_PRIV_MAGIC);
		if (vp->vcl == ctx->vcl && vp->id == id
		    && vp->vmod_id == vmod_id)
			return (vp->priv);
	}
	ALLOC_OBJ(vp, VRT_PRIV_MAGIC);
	AN(vp);
	vp->vcl = ctx->vcl;
	vp->id = id;
	vp->vmod_id = vmod_id;
	VTAILQ_INSERT_TAIL(&vps->privs, vp, list);
	return (vp->priv);
}

void
VRTPRIV_dynamic_kill(struct vrt_privs *privs, uintptr_t id)
{
	struct vrt_priv *vp, *vp1;

	CHECK_OBJ_NOTNULL(privs, VRT_PRIVS_MAGIC);

	VTAILQ_FOREACH_SAFE(vp, &privs->privs, list, vp1) {
		CHECK_OBJ_NOTNULL(vp, VRT_PRIV_MAGIC);
		if (id == vp->id) {
			VTAILQ_REMOVE(&privs->privs, vp, list);
			VRT_priv_fini(vp->priv);
			FREE_OBJ(vp);
		}
	}
}

struct vmod_priv *
VRT_priv_task(VRT_CTX, void *vmod_id)
{
	uintptr_t id;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (ctx->req) {
		CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
		id = (uintptr_t)ctx->req;
	} else {
		CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
		id = (uintptr_t)ctx->bo;
	}
	return (VRT_priv_dynamic(ctx, id, (uintptr_t)vmod_id));
}

/*--------------------------------------------------------------------
 */

void
VRT_priv_fini(const struct vmod_priv *p)
{

	if (p->priv != (void*)0 && p->free != (void*)0)
		p->free(p->priv);
}
