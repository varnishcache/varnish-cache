/*-
 * Copyright (c) 2013-2015 Varnish Software AS
 * Copyright 2019 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@FreeBSD.org>
 * Author: Nils Goroll <nils.goroll@uplex.de>
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
#include <stdio.h>

#include "cache/cache.h"

#include "vbm.h"
#include "vcl.h"
#include "vsb.h"

#include "vcc_directors_if.h"

#include "vmod_directors.h"

VCL_BACKEND
VPFX(lookup)(VRT_CTX, VCL_STRING name)
{
	AN(ctx->method & VCL_MET_TASK_H);
	return (VRT_LookupDirector(ctx, name));
}

static void
vdir_expand(struct vdir *vd, unsigned n)
{
	CHECK_OBJ_NOTNULL(vd, VDIR_MAGIC);

	vd->backend = realloc(vd->backend, n * sizeof *vd->backend);
	AN(vd->backend);
	vd->weight = realloc(vd->weight, n * sizeof *vd->weight);
	AN(vd->weight);
	if (n > vd->healthy->nbits)
		vbit_expand(vd->healthy, n);
	AN(vd->healthy);
	vd->l_backend = n;
}

void
vdir_new(VRT_CTX, struct vdir **vdp, const char *vcl_name,
    const struct vdi_methods *m, void *priv)
{
	struct vdir *vd;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(m, VDI_METHODS_MAGIC);
	AN(vcl_name);
	AN(vdp);
	AZ(*vdp);
	ALLOC_OBJ(vd, VDIR_MAGIC);
	AN(vd);
	*vdp = vd;
	PTOK(pthread_rwlock_init(&vd->mtx, NULL));
	vd->dir = VRT_AddDirector(ctx, m, priv, "%s", vcl_name);
	vd->healthy = vbit_new(8);
	AN(vd->healthy);
}

void
vdir_release(struct vdir *vd)
{
	unsigned u;

	CHECK_OBJ_NOTNULL(vd, VDIR_MAGIC);

	vdir_wrlock(vd);
	for (u = 0; u < vd->n_backend; u++)
		VRT_Assign_Backend(&vd->backend[u], NULL);
	vd->n_backend = 0;
	vdir_unlock(vd);
}

void
vdir_delete(struct vdir **vdp)
{
	struct vdir *vd;

	TAKE_OBJ_NOTNULL(vd, vdp, VDIR_MAGIC);

	AZ(vd->dir);
	AZ(vd->n_backend);
	free(vd->backend);
	free(vd->weight);
	PTOK(pthread_rwlock_destroy(&vd->mtx));
	vbit_destroy(vd->healthy);
	FREE_OBJ(vd);
}

void
vdir_rdlock(struct vdir *vd)
{
	CHECK_OBJ_NOTNULL(vd, VDIR_MAGIC);
	PTOK(pthread_rwlock_rdlock(&vd->mtx));
}

void
vdir_wrlock(struct vdir *vd)
{
	CHECK_OBJ_NOTNULL(vd, VDIR_MAGIC);
	PTOK(pthread_rwlock_wrlock(&vd->mtx));
}

void
vdir_unlock(struct vdir *vd)
{
	CHECK_OBJ_NOTNULL(vd, VDIR_MAGIC);
	PTOK(pthread_rwlock_unlock(&vd->mtx));
}


void
vdir_add_backend(VRT_CTX, struct vdir *vd, VCL_BACKEND be, double weight)
{
	unsigned u;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vd, VDIR_MAGIC);
	if (be == NULL) {
		VRT_fail(ctx, "%s: None backend cannot be added",
		    VRT_BACKEND_string(vd->dir));
		return;
	}
	AN(be);
	vdir_wrlock(vd);
	if (vd->n_backend >= vd->l_backend)
		vdir_expand(vd, vd->l_backend + 16);
	assert(vd->n_backend < vd->l_backend);
	u = vd->n_backend++;
	vd->backend[u] = NULL;
	VRT_Assign_Backend(&vd->backend[u], be);
	vd->weight[u] = weight;
	vdir_unlock(vd);
}

void
vdir_remove_backend(VRT_CTX, struct vdir *vd, VCL_BACKEND be, unsigned *cur)
{
	unsigned u, n;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vd, VDIR_MAGIC);
	if (be == NULL) {
		VRT_fail(ctx, "%s: None backend cannot be removed",
		    VRT_BACKEND_string(vd->dir));
		return;
	}
	CHECK_OBJ(be, DIRECTOR_MAGIC);
	vdir_wrlock(vd);
	for (u = 0; u < vd->n_backend; u++)
		if (vd->backend[u] == be)
			break;
	if (u == vd->n_backend) {
		vdir_unlock(vd);
		return;
	}
	VRT_Assign_Backend(&vd->backend[u], NULL);
	n = (vd->n_backend - u) - 1;
	memmove(&vd->backend[u], &vd->backend[u+1], n * sizeof(vd->backend[0]));
	memmove(&vd->weight[u], &vd->weight[u+1], n * sizeof(vd->weight[0]));
	vd->n_backend--;

	if (cur) {
		assert(*cur <= vd->n_backend);
		if (u < *cur)
			(*cur)--;
		else if (*cur == vd->n_backend)
			*cur = 0;
	}
	vdir_unlock(vd);
}

VCL_BOOL
vdir_any_healthy(VRT_CTX, struct vdir *vd, VCL_TIME *changed)
{
	unsigned retval = 0;
	VCL_BACKEND be;
	unsigned u;
	vtim_real c;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vd, VDIR_MAGIC);
	vdir_rdlock(vd);
	if (changed != NULL)
		*changed = 0;
	for (u = 0; u < vd->n_backend; u++) {
		be = vd->backend[u];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
		retval = VRT_Healthy(ctx, be, &c);
		if (changed != NULL && c > *changed)
			*changed = c;
		if (retval)
			break;
	}
	vdir_unlock(vd);
	return (retval);
}

void
vdir_list(VRT_CTX, struct vdir *vd, struct vsb *vsb, int pflag, int jflag,
    int weight)
{
	VCL_BACKEND be;
	VCL_BOOL h;
	unsigned u, nh;
	double w;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vd, VDIR_MAGIC);

	if (pflag) {
		if (jflag) {
			VSB_cat(vsb, "{\n");
			VSB_indent(vsb, 2);
			if (weight)
				VSB_printf(vsb, "\"total_weight\": %f,\n",
				    vd->total_weight);
			VSB_cat(vsb, "\"backends\": {\n");
			VSB_indent(vsb, 2);
		} else {
			VSB_cat(vsb, "\n\n\tBackend\tWeight\tHealth\n");
		}
	}

	vdir_rdlock(vd);
	vdir_update_health(ctx, vd);
	for (u = 0; pflag && u < vd->n_backend; u++) {
		be = vd->backend[u];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);

		h = vbit_test(vd->healthy, u);

		w = h ? vd->weight[u] : 0.0;

		if (jflag) {
			if (u)
				VSB_cat(vsb, ",\n");
			VSB_printf(vsb, "\"%s\": {\n", be->vcl_name);
			VSB_indent(vsb, 2);

			if (weight)
				VSB_printf(vsb, "\"weight\": %f,\n", w);

			if (h)
				VSB_cat(vsb, "\"health\": \"healthy\"\n");
			else
				VSB_cat(vsb, "\"health\": \"sick\"\n");

			VSB_indent(vsb, -2);
			VSB_cat(vsb, "}");
		} else {
			VSB_cat(vsb, "\t");
			VSB_cat(vsb, be->vcl_name);
			if (weight)
				VSB_printf(vsb, "\t%6.2f%%\t",
				    100 * w / vd->total_weight);
			else
				VSB_cat(vsb, "\t-\t");
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

/*
 * iterate backends and update
 * - healthy bitmap
 * - number of healthy backends
 * - total_weight
 * - last change time of the VCL_BACKEND
 *
 * must be called under the vdir lock (read or write).
 *
 * A write lock is required if consistency between the individual attributes is
 * a must, e.g. when total_weight is required to be the exact sum of the weights
 *
 * The read lock is safe because add_backend expands the healthy bitmap and all
 * other members are atomic and may be used if consistency is not required.
 */
void
vdir_update_health(VRT_CTX, struct vdir *vd)
{
	VCL_TIME c, changed = 0;
	VCL_BOOL h;
	VCL_BACKEND be;
	unsigned u, nh = 0;
	double tw = 0.0;
	struct vbitmap *healthy;

	CHECK_OBJ_NOTNULL(vd, VDIR_MAGIC);
	healthy = vd->healthy;
	for (u = 0; u < vd->n_backend; u++) {
		be = vd->backend[u];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
		c = 0;
		h = VRT_Healthy(ctx, be, &c);
		if (h) {
			nh++;
			tw += vd->weight[u];
		}
		if (c > changed)
			changed = c;
		if (h != vbit_test(healthy, u)) {
			if (h)
				vbit_set(healthy, u);
			else
				vbit_clr(healthy, u);
		}
	}
	VRT_SetChanged(vd->dir, changed);
	vd->total_weight = tw;
	vd->n_healthy = nh;
}

static unsigned
vdir_pick_by_weight(const struct vdir *vd, double w)
{
	const struct vbitmap *healthy = vd->healthy;
	double a = 0.0;
	unsigned u;

	AN(healthy);
	for (u = 0; u < vd->n_backend; u++) {
		if (! vbit_test(healthy, u))
			continue;
		a += vd->weight[u];
		if (w < a)
			return (u);
	}
	WRONG("");
}

VCL_BACKEND
vdir_pick_be(VRT_CTX, struct vdir *vd, double w)
{
	unsigned u;
	VCL_BACKEND be = NULL;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vd, VDIR_MAGIC);
	vdir_wrlock(vd);
	vdir_update_health(ctx, vd);
	if (vd->total_weight > 0.0) {
		u = vdir_pick_by_weight(vd, w * vd->total_weight);
		assert(u < vd->n_backend);
		be = vd->backend[u];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
	}
	vdir_unlock(vd);
	return (be);
}
