/*-
 * Copyright (c) 2013-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@FreeBSD.org>
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
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "cache/cache.h"
#include "cache/cache_director.h"

#include "vcc_if.h"

#include "vdir.h"

struct vmod_directors_round_robin {
	unsigned				magic;
#define VMOD_DIRECTORS_ROUND_ROBIN_MAGIC	0xa80970cf
	struct vdir				*vd;
	unsigned				nxt;
};

static unsigned v_matchproto_(vdi_healthy)
vmod_rr_healthy(const struct director *dir, const struct busyobj *bo,
    double *changed)
{
	struct vmod_directors_round_robin *rr;

	CAST_OBJ_NOTNULL(rr, dir->priv, VMOD_DIRECTORS_ROUND_ROBIN_MAGIC);
	return (vdir_any_healthy(rr->vd, bo, changed));
}

static const struct director * v_matchproto_(vdi_resolve_f)
vmod_rr_resolve(const struct director *dir, struct worker *wrk,
    struct busyobj *bo)
{
	struct vmod_directors_round_robin *rr;
	unsigned u;
	VCL_BACKEND be = NULL;
	unsigned nxt;

	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CAST_OBJ_NOTNULL(rr, dir->priv, VMOD_DIRECTORS_ROUND_ROBIN_MAGIC);
	vdir_rdlock(rr->vd);
	nxt = rr->nxt;
	for (u = 0; u < rr->vd->n_backend; u++) {
		be = rr->vd->backend[nxt];
		nxt++;
		nxt %= rr->vd->n_backend;
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
		if (be->healthy(be, bo, NULL))
			break;
	}
	rr->nxt = nxt;
	vdir_unlock(rr->vd);
	if (u == rr->vd->n_backend)
		be = NULL;
	return (be);
}

VCL_VOID v_matchproto_()
vmod_round_robin__init(VRT_CTX,
    struct vmod_directors_round_robin **rrp, const char *vcl_name)
{
	struct vmod_directors_round_robin *rr;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(rrp);
	AZ(*rrp);
	ALLOC_OBJ(rr, VMOD_DIRECTORS_ROUND_ROBIN_MAGIC);
	AN(rr);
	*rrp = rr;
	vdir_new(ctx, &rr->vd, "round-robin", vcl_name, vmod_rr_healthy,
	    vmod_rr_resolve, rr);
}

VCL_VOID v_matchproto_()
vmod_round_robin__fini(struct vmod_directors_round_robin **rrp)
{
	struct vmod_directors_round_robin *rr;

	// XXX 2297
	if (*rrp == NULL)
		return;

	TAKE_OBJ_NOTNULL(rr, rrp, VMOD_DIRECTORS_ROUND_ROBIN_MAGIC);
	vdir_delete(&rr->vd);
	FREE_OBJ(rr);
}

VCL_VOID v_matchproto_()
vmod_round_robin_add_backend(VRT_CTX,
    struct vmod_directors_round_robin *rr, VCL_BACKEND be)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(rr, VMOD_DIRECTORS_ROUND_ROBIN_MAGIC);
	vdir_add_backend(ctx, rr->vd, be, 0.0);
}

VCL_VOID v_matchproto_()
vmod_round_robin_remove_backend(VRT_CTX,
    struct vmod_directors_round_robin *rr, VCL_BACKEND be)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(rr, VMOD_DIRECTORS_ROUND_ROBIN_MAGIC);
	vdir_remove_backend(ctx, rr->vd, be, NULL);
}

VCL_BACKEND v_matchproto_()
vmod_round_robin_backend(VRT_CTX,
    struct vmod_directors_round_robin *rr)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(rr, VMOD_DIRECTORS_ROUND_ROBIN_MAGIC);
	return (rr->vd->dir);
}