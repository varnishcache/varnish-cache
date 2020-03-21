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
 * Runtime support for compiled VCL programs: private variables
 */

#include "config.h"


#include <stdlib.h>

#include "cache_varnishd.h"
#include "vcl.h"

struct vrt_priv {
	unsigned			magic;
#define VRT_PRIV_MAGIC			0x24157a52
	VRBT_ENTRY(vrt_priv)		entry;
	struct vmod_priv		priv[1];
	uintptr_t			vmod_id;
};

struct vrt_privs cli_task_privs[1];

static inline int vrt_priv_dyncmp(const struct vrt_priv *,
    const struct vrt_priv *);

VRBT_PROTOTYPE_STATIC(vrt_privs, vrt_priv, entry, vrt_priv_dyncmp)

/*--------------------------------------------------------------------
 */

void
pan_privs(struct vsb *vsb, const struct vrt_privs *privs)
{
	struct vrt_priv *vp;

	VSB_printf(vsb, "privs = %p {\n", privs);
	if (PAN_already(vsb, privs))
		return;
	VSB_indent(vsb, 2);
	if (privs != NULL) {
		VRBT_FOREACH(vp, vrt_privs, privs) {
			PAN_CheckMagic(vsb, vp, VRT_PRIV_MAGIC);
			VSB_printf(vsb,
			    "priv {p %p l %ld f %p} vmod %jx\n",
			    vp->priv->priv,
			    vp->priv->len,
			    vp->priv->free,
			    (uintmax_t)vp->vmod_id
			);
		}
	}
	VSB_indent(vsb, -2);
	VSB_cat(vsb, "},\n");
}


/*--------------------------------------------------------------------
 */

static void
VRTPRIV_init(struct vrt_privs *privs)
{

	VRBT_INIT(privs);
}

static inline int
vrt_priv_dyncmp(const struct vrt_priv *vp1, const struct vrt_priv *vp2)
{
	if (vp1->vmod_id < vp2->vmod_id)
		return (-1);
	if (vp1->vmod_id > vp2->vmod_id)
		return (1);
	return (0);
}

VRBT_GENERATE_STATIC(vrt_privs, vrt_priv, entry, vrt_priv_dyncmp)

static struct vmod_priv *
vrt_priv_dynamic(struct ws *ws, struct vrt_privs *privs, uintptr_t vmod_id)
{
	struct vrt_priv *vp;
	const struct vrt_priv needle = {.vmod_id = vmod_id};

	AN(vmod_id);

	vp = VRBT_FIND(vrt_privs, privs, &needle);
	if (vp) {
		CHECK_OBJ(vp, VRT_PRIV_MAGIC);
		assert(vp->vmod_id == vmod_id);
		return (vp->priv);
	}

	vp = WS_Alloc(ws, sizeof *vp);
	if (vp == NULL)
		return (NULL);
	INIT_OBJ(vp, VRT_PRIV_MAGIC);
	vp->vmod_id = vmod_id;
	VRBT_INSERT(vrt_privs, privs, vp);
	return (vp->priv);
}

struct vmod_priv *
VRT_priv_task(VRT_CTX, const void *vmod_id)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	assert(ctx->req == NULL || ctx->bo == NULL ||
	    ctx->method == VCL_MET_PIPE);

	if (ctx->req) {
		CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
		return (vrt_priv_dynamic(
		    ctx->ws,
		    ctx->req->privs,
		    (uintptr_t)vmod_id
		));
	}
	if (ctx->bo) {
		CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
		return (vrt_priv_dynamic(
		    ctx->ws,
		    ctx->bo->privs,
		    (uintptr_t)vmod_id
		));
	}
	ASSERT_CLI();

	return (vrt_priv_dynamic(
	    ctx->ws,
	    cli_task_privs,
	    (uintptr_t)vmod_id
	));
}

struct vmod_priv *
VRT_priv_top(VRT_CTX, const void *vmod_id)
{
	struct req *req;
	struct sess *sp;
	struct reqtop *top;
	struct vmod_priv *priv;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (ctx->req == NULL) {
		WRONG("PRIV_TOP is only accessible in client VCL context");
		NEEDLESS(return (NULL));
	}
	req = ctx->req;
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	sp = ctx->sp;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	top = req->top;
	CHECK_OBJ_NOTNULL(top, REQTOP_MAGIC);

	Lck_Lock(&sp->mtx);
	priv = vrt_priv_dynamic(req->ws, top->privs, (uintptr_t)vmod_id);
	Lck_Unlock(&sp->mtx);
	return (priv);
}

/*--------------------------------------------------------------------
 */

void
VRT_priv_fini(const struct vmod_priv *p)
{

	if (p->priv != NULL && p->free != NULL)
		p->free(p->priv);
}

/*--------------------------------------------------------------------*/

void
VCL_TaskEnter(struct vrt_privs *privs)
{

	VRTPRIV_init(privs);
}

void
VCL_TaskLeave(struct vrt_privs *privs)
{
	struct vrt_priv *vp, *vp1;

	/*
	 * NB: We don't bother removing entries as we finish them because it's
	 * a costly operation. Instead we safely walk the whole tree and clear
	 * the head at the very end.
	 */
	VRBT_FOREACH_SAFE(vp, vrt_privs, privs, vp1)
		VRT_priv_fini(vp->priv);
	ZERO_OBJ(privs, sizeof *privs);
}
