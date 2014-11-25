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


#include <stdio.h>
#include <stdlib.h>

#include "cache.h"

#include "vcl.h"
#include "vrt.h"

struct vrt_privs {
	unsigned			magic;
#define VRT_PRIVS_MAGIC			0x24157a52
	VTAILQ_ENTRY(vrt_privs)		list;
	struct vmod_priv		priv[1];
	const struct VCL_conf		*vcl;
	uintptr_t			id;
	uintptr_t			vmod_id;
};

/*--------------------------------------------------------------------
 */

static struct vmod_priv *
VRT_priv_dynamic(VRT_CTX, uintptr_t id, uintptr_t vmod_id)
{
	struct vrt_privs *vps;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	VTAILQ_FOREACH(vps, &ctx->req->sp->privs, list) {
		CHECK_OBJ_NOTNULL(vps, VRT_PRIVS_MAGIC);
		if (vps->vcl == ctx->vcl && vps->id == id
		    && vps->vmod_id == vmod_id)
			return (vps->priv);
	}
	ALLOC_OBJ(vps, VRT_PRIVS_MAGIC);
	AN(vps);
	vps->vcl = ctx->vcl;
	vps->id = id;
	vps->vmod_id = vmod_id;
	VTAILQ_INSERT_TAIL(&ctx->req->sp->privs, vps, list);
	return (vps->priv);
}

void
VRTPRIV_dynamic_kill(struct sess *sp, uintptr_t id)
{
	struct vrt_privs *vps, *vps1;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	VTAILQ_FOREACH_SAFE(vps, &sp->privs, list, vps1) {
		CHECK_OBJ_NOTNULL(vps, VRT_PRIVS_MAGIC);
		if (id == vps->id) {
			VTAILQ_REMOVE(&sp->privs, vps, list);
			VRT_priv_fini(vps->priv);
			FREE_OBJ(vps);
		}
	}
}

struct vmod_priv *
VRT_priv_req(VRT_CTX, void *vmod_id)
{
	return (VRT_priv_dynamic(ctx, (uintptr_t)ctx->req, (uintptr_t)vmod_id));
}

/*--------------------------------------------------------------------
 */

void
VRT_priv_fini(const struct vmod_priv *p)
{

	if (p->priv != (void*)0 && p->free != (void*)0)
		p->free(p->priv);
}

