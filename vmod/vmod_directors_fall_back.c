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

#include "vcc_directors_if.h"

#include "vmod_directors.h"
#include "vsb.h"
#include "vbm.h"

struct vmod_directors_fallback {
	unsigned				magic;
#define VMOD_DIRECTORS_FALLBACK_MAGIC		0xad4e26ba
	struct vdir				*vd;
	VCL_BOOL				st;
	unsigned				cur;
};

static VCL_BOOL v_matchproto_(vdi_healthy)
vmod_fallback_healthy(VRT_CTX, VCL_BACKEND dir, VCL_TIME *changed)
{
	struct vmod_directors_fallback *fb;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(fb, dir->priv, VMOD_DIRECTORS_FALLBACK_MAGIC);
	return (vdir_any_healthy(ctx, fb->vd, changed));
}

static void v_matchproto_(vdi_list_f)
vmod_fallback_list(VRT_CTX, VCL_BACKEND dir, struct vsb *vsb, int pflag,
    int jflag)
{
	struct vmod_directors_fallback *fb;
	struct vdir *vd;
	VCL_BACKEND be;
	VCL_BOOL h;
	unsigned u, nh;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(fb, dir->priv, VMOD_DIRECTORS_FALLBACK_MAGIC);
	CAST_OBJ_NOTNULL(vd, fb->vd, VDIR_MAGIC);

	if (pflag) {
		if (jflag) {
			VSB_cat(vsb, "{\n");
			VSB_indent(vsb, 2);
			VSB_printf(vsb, "\"sticky\": %s,\n",
			    fb->st ? "true" : "false");
			VSB_cat(vsb, "\"backends\": {\n");
			VSB_indent(vsb, 2);
		} else {
			VSB_cat(vsb, "\n\n\tBackend\tCurrent\tHealth\n");
		}
	}

	vdir_rdlock(vd);
	vdir_update_health(ctx, vd);
	for (u = 0; pflag && u < vd->n_backend; u++) {
		be = vd->backend[u];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);

		h = vbit_test(vd->healthy, u);

		if (jflag) {
			if (u)
				VSB_cat(vsb, ",\n");
			VSB_printf(vsb, "\"%s\": {\n", be->vcl_name);
			VSB_indent(vsb, 2);

			if (fb->cur == u)
				VSB_cat(vsb, "\"current\": true,\n");
			else
				VSB_cat(vsb, "\"current\": false,\n");

			if (h)
				VSB_cat(vsb, "\"health\": \"healthy\"\n");
			else
				VSB_cat(vsb, "\"health\": \"sick\"\n");

			VSB_indent(vsb, -2);
			VSB_cat(vsb, "}");
		} else {
			VSB_cat(vsb, "\t");
			VSB_cat(vsb, be->vcl_name);
			if (fb->cur == u)
				VSB_cat(vsb, "\t*\t");
			else
				VSB_cat(vsb, "\t\t");
			VSB_cat(vsb, h ? "healthy" : "sick");
			VSB_cat(vsb, "\n");
		}
	}
	nh = vd->n_healthy;
	u = vd->n_backend;
	vdir_unlock(vd);

	if (jflag && (pflag)) {
		VSB_cat(vsb, "\n");
		VSB_indent(vsb, -2);
		VSB_cat(vsb, "}\n");
		VSB_indent(vsb, -2);
		VSB_cat(vsb, "},\n");
	}

	if (pflag)
		return;

	if (jflag)
		VSB_printf(vsb, "[%u, %u, \"%s\"]", nh, u,
		    nh ? "healthy" : "sick");
	else
		VSB_printf(vsb, "%u/%u\t%s", nh, u, nh ? "healthy" : "sick");
}

static VCL_BACKEND v_matchproto_(vdi_resolve_f)
vmod_fallback_resolve(VRT_CTX, VCL_BACKEND dir)
{
	struct vmod_directors_fallback *fb;
	unsigned u;
	VCL_BACKEND be = NULL;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(fb, dir->priv, VMOD_DIRECTORS_FALLBACK_MAGIC);

	vdir_wrlock(fb->vd);
	if (!fb->st)
		fb->cur = 0;
	for (u = 0; u < fb->vd->n_backend; u++) {
		be = fb->vd->backend[fb->cur];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
		if (VRT_Healthy(ctx, be, NULL))
			break;
		if (++fb->cur == fb->vd->n_backend)
			fb->cur = 0;
	}
	if (u == fb->vd->n_backend)
		be = NULL;
	vdir_unlock(fb->vd);
	return (be);
}

static void v_matchproto_(vdi_release_f)
vmod_fallback_release(VCL_BACKEND dir)
{
	struct vmod_directors_fallback *fallback;

	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(fallback, dir->priv, VMOD_DIRECTORS_FALLBACK_MAGIC);
	vdir_release(fallback->vd);
}

static void v_matchproto_(vdi_destroy_f)
vmod_fallback_destroy(VCL_BACKEND dir)
{
	struct vmod_directors_fallback *fallback;

	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(fallback, dir->priv, VMOD_DIRECTORS_FALLBACK_MAGIC);
	vdir_delete(&fallback->vd);
	FREE_OBJ(fallback);
}

static const struct vdi_methods vmod_fallback_methods[1] = {{
	.magic =		VDI_METHODS_MAGIC,
	.type =			"fallback",
	.healthy =		vmod_fallback_healthy,
	.resolve =		vmod_fallback_resolve,
	.release =		vmod_fallback_release,
	.destroy =		vmod_fallback_destroy,
	.list =			vmod_fallback_list
}};


VCL_VOID v_matchproto_()
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
	vdir_new(ctx, &fb->vd, vcl_name, vmod_fallback_methods, fb);
	fb->st = sticky;
}

VCL_VOID v_matchproto_()
vmod_fallback__fini(struct vmod_directors_fallback **fbp)
{
	struct vmod_directors_fallback *fb;

	TAKE_OBJ_NOTNULL(fb, fbp, VMOD_DIRECTORS_FALLBACK_MAGIC);
	VRT_DelDirector(&fb->vd->dir);
}

VCL_VOID v_matchproto_()
vmod_fallback_add_backend(VRT_CTX,
    struct vmod_directors_fallback *fb, VCL_BACKEND be)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(fb, VMOD_DIRECTORS_FALLBACK_MAGIC);
	vdir_add_backend(ctx, fb->vd, be, 0.0);
}

VCL_VOID v_matchproto_()
vmod_fallback_remove_backend(VRT_CTX,
    struct vmod_directors_fallback *fb, VCL_BACKEND be)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(fb, VMOD_DIRECTORS_FALLBACK_MAGIC);
	vdir_remove_backend(ctx, fb->vd, be, &fb->cur);
}

VCL_VOID v_matchproto_()
vmod_fallback_reset_stickiness(VRT_CTX, struct vmod_directors_fallback *fb)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(fb, VMOD_DIRECTORS_FALLBACK_MAGIC);
	vdir_wrlock(fb->vd);
	fb->cur = 0;
	vdir_unlock(fb->vd);
}

VCL_BACKEND v_matchproto_()
vmod_fallback_backend(VRT_CTX,
    struct vmod_directors_fallback *fb)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(fb, VMOD_DIRECTORS_FALLBACK_MAGIC);
	return (fb->vd->dir);
}
