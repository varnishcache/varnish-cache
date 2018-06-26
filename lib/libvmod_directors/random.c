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

#include "cache/cache.h"

#include "vbm.h"
#include "vrnd.h"

#include "vdir.h"

#include "vcc_if.h"

struct vmod_directors_random {
	unsigned				magic;
#define VMOD_DIRECTORS_RANDOM_MAGIC		0x4732d092
	struct vdir				*vd;
};

static VCL_BOOL v_matchproto_(vdi_healthy)
vmod_random_healthy(VRT_CTX, VCL_BACKEND dir, VCL_TIME *changed)
{
	struct vmod_directors_random *rr;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(rr, dir->priv, VMOD_DIRECTORS_RANDOM_MAGIC);
	return (vdir_any_healthy(ctx, rr->vd, changed));
}

static VCL_BACKEND v_matchproto_(vdi_resolve_f)
vmod_random_resolve(VRT_CTX, VCL_BACKEND dir)
{
	struct vmod_directors_random *rr;
	VCL_BACKEND be;
	double r;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(rr, dir->priv, VMOD_DIRECTORS_RANDOM_MAGIC);
	r = scalbn(VRND_RandomTestable(), -31);
	assert(r >= 0 && r < 1.0);
	be = vdir_pick_be(ctx, rr->vd, r);
	return (be);
}

static void v_matchproto_(vdi_destroy_f)
vmod_random_destroy(VCL_BACKEND dir)
{
	struct vmod_directors_random *random;

	CAST_OBJ_NOTNULL(random, dir->priv, VMOD_DIRECTORS_RANDOM_MAGIC);
	vdir_delete(&random->vd);
	FREE_OBJ(random);
}

static const struct vdi_methods vmod_random_methods[1] = {{
	.magic =		VDI_METHODS_MAGIC,
	.type =			"random",
	.healthy =		vmod_random_healthy,
	.resolve =		vmod_random_resolve,
	.destroy =		vmod_random_destroy
}};


VCL_VOID v_matchproto_()
vmod_random__init(VRT_CTX, struct vmod_directors_random **rrp,
    const char *vcl_name)
{
	struct vmod_directors_random *rr;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(rrp);
	AZ(*rrp);
	ALLOC_OBJ(rr, VMOD_DIRECTORS_RANDOM_MAGIC);
	AN(rr);
	*rrp = rr;
	vdir_new(ctx, &rr->vd, vcl_name, vmod_random_methods, rr);
}

VCL_VOID v_matchproto_()
vmod_random__fini(struct vmod_directors_random **rrp)
{
	struct vmod_directors_random *rr;

	TAKE_OBJ_NOTNULL(rr, rrp, VMOD_DIRECTORS_RANDOM_MAGIC);
	VRT_DelDirector(&rr->vd->dir);
}

VCL_VOID v_matchproto_()
vmod_random_add_backend(VRT_CTX,
    struct vmod_directors_random *rr, VCL_BACKEND be, double w)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(rr, VMOD_DIRECTORS_RANDOM_MAGIC);
	vdir_add_backend(ctx, rr->vd, be, w);
}

VCL_VOID vmod_random_remove_backend(VRT_CTX,
    struct vmod_directors_random *rr, VCL_BACKEND be) v_matchproto_()
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(rr, VMOD_DIRECTORS_RANDOM_MAGIC);
	vdir_remove_backend(ctx, rr->vd, be, NULL);
}

VCL_BACKEND v_matchproto_()
vmod_random_backend(VRT_CTX, struct vmod_directors_random *rr)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(rr, VMOD_DIRECTORS_RANDOM_MAGIC);
	return (rr->vd->dir);
}
