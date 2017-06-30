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
#include "cache/cache_director.h"

#include "vrt.h"
#include "vcc_if.h"

#include "vdir.h"

struct vmod_directors_fallback {
	unsigned				magic;
#define VMOD_DIRECTORS_FALLBACK_MAGIC		0xad4e26ba
	struct vdir				*vd;
	VCL_BOOL				st;
	unsigned				cur;
};

static unsigned __match_proto__(vdi_healthy)
vmod_fallback_healthy(const struct director *dir, const struct busyobj *bo,
    double *changed)
{
	struct vmod_directors_fallback *fb;

	CAST_OBJ_NOTNULL(fb, dir->priv, VMOD_DIRECTORS_FALLBACK_MAGIC);
	return (vdir_any_healthy(fb->vd, bo, changed));
}

static const struct director * __match_proto__(vdi_resolve_f)
vmod_fallback_resolve(const struct director *dir, struct worker *wrk,
    struct busyobj *bo)
{
	struct vmod_directors_fallback *fb;
	unsigned u;
	VCL_BACKEND be = NULL;

	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CAST_OBJ_NOTNULL(fb, dir->priv, VMOD_DIRECTORS_FALLBACK_MAGIC);

	vdir_wrlock(fb->vd);
	if (!fb->st)
		fb->cur = 0;
	for (u = 0; u < fb->vd->n_backend; u++) {
		be = fb->vd->backend[fb->cur];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
		if (be->healthy(be, bo, NULL))
			break;
		if (++fb->cur == fb->vd->n_backend)
			fb->cur = 0;
	}
	vdir_unlock(fb->vd);
	if (u == fb->vd->n_backend)
		be = NULL;
	return (be);
}

VCL_VOID __match_proto__()
vmod_fallback__init(VRT_CTX,
    struct vmod_directors_fallback **fbp, const char *vcl_name, VCL_BOOL sticky)
{
	struct vmod_directors_fallback *fb;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(fbp);
	AZ(*fbp);
	ALLOC_OBJ(fb, VMOD_DIRECTORS_FALLBACK_MAGIC);
	AN(fb);
	*fbp = fb;
	vdir_new(&fb->vd, "fallback", vcl_name, vmod_fallback_healthy,
	    vmod_fallback_resolve, fb);
	fb->st = sticky;
}

VCL_VOID __match_proto__()
vmod_fallback__fini(struct vmod_directors_fallback **fbp)
{
	struct vmod_directors_fallback *fb;

	fb = *fbp;
	*fbp = NULL;
	CHECK_OBJ_NOTNULL(fb, VMOD_DIRECTORS_FALLBACK_MAGIC);
	vdir_delete(&fb->vd);
	FREE_OBJ(fb);
}

VCL_VOID __match_proto__()
vmod_fallback_add_backend(VRT_CTX,
    struct vmod_directors_fallback *fb, VCL_BACKEND be)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(fb, VMOD_DIRECTORS_FALLBACK_MAGIC);
	(void)vdir_add_backend(fb->vd, be, 0.0);
}

VCL_VOID __match_proto__()
vmod_fallback_remove_backend(VRT_CTX,
    struct vmod_directors_fallback *fb, VCL_BACKEND be)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(fb, VMOD_DIRECTORS_FALLBACK_MAGIC);
	vdir_remove_backend(fb->vd, be, &fb->cur);
}

VCL_BACKEND __match_proto__()
vmod_fallback_backend(VRT_CTX,
    struct vmod_directors_fallback *fb)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(fb, VMOD_DIRECTORS_FALLBACK_MAGIC);
	return (fb->vd->dir);
}
