/*-
 * Copyright (c) 2013-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@FreeBSD.org>
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
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "cache/cache.h"

#include "vmod_directors.h"

#include "vcc_directors_if.h"

struct vmod_directors_hash {
	unsigned				magic;
#define VMOD_DIRECTORS_HASH_MAGIC		0xc08dd611
	struct vdir				*vd;
};

static void v_matchproto_(vdi_destroy_f)
vmod_hash_destroy(VCL_BACKEND dir)
{
	struct vmod_directors_hash *rr;

	CAST_OBJ_NOTNULL(rr, dir->priv, VMOD_DIRECTORS_HASH_MAGIC);
	vdir_delete(&rr->vd);
	FREE_OBJ(rr);
}

static const struct vdi_methods vmod_hash_methods[1] = {{
	.magic =		VDI_METHODS_MAGIC,
	.type =			"hash",
	.destroy =		vmod_hash_destroy
}};


VCL_VOID v_matchproto_()
vmod_hash__init(VRT_CTX, struct vmod_directors_hash **rrp,
    const char *vcl_name)
{
	struct vmod_directors_hash *rr;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(rrp);
	AZ(*rrp);
	ALLOC_OBJ(rr, VMOD_DIRECTORS_HASH_MAGIC);
	AN(rr);
	*rrp = rr;
	vdir_new(ctx, &rr->vd, vcl_name, vmod_hash_methods, rr);
}

VCL_VOID v_matchproto_()
vmod_hash__fini(struct vmod_directors_hash **rrp)
{
	struct vmod_directors_hash *rr;

	TAKE_OBJ_NOTNULL(rr, rrp, VMOD_DIRECTORS_HASH_MAGIC);
	VRT_DelDirector(&rr->vd->dir);
}

VCL_VOID v_matchproto_()
vmod_hash_add_backend(VRT_CTX,
    struct vmod_directors_hash *rr, VCL_BACKEND be, double w)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(rr, VMOD_DIRECTORS_HASH_MAGIC);
	vdir_add_backend(ctx, rr->vd, be, w);
}

VCL_VOID v_matchproto_()
vmod_hash_remove_backend(VRT_CTX,
    struct vmod_directors_hash *rr, VCL_BACKEND be)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(rr, VMOD_DIRECTORS_HASH_MAGIC);
	vdir_remove_backend(ctx, rr->vd, be, NULL);
}

VCL_BACKEND v_matchproto_()
vmod_hash_backend(VRT_CTX, struct vmod_directors_hash *rr, VCL_STRANDS s)
{
	VCL_BACKEND be;
	double r;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_ORNULL(ctx->bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(rr, VMOD_DIRECTORS_HASH_MAGIC);
	AN(s);

	r = VRT_HashStrands32(s);
	r = scalbn(r, -32);
	assert(r >= 0 && r <= 1.0);
	be = vdir_pick_be(ctx, rr->vd, r);
	return (be);
}
