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

#include "vdir.h"

static void
vdir_expand(struct vdir *vd, unsigned n)
{
	CHECK_OBJ_NOTNULL(vd, VDIR_MAGIC);

	vd->backend = realloc(vd->backend, n * sizeof *vd->backend);
	AN(vd->backend);
	vd->weight = realloc(vd->weight, n * sizeof *vd->weight);
	AN(vd->weight);
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
	AZ(pthread_rwlock_init(&vd->mtx, NULL));
	vd->dir = VRT_AddDirector(ctx, m, priv, "%s", vcl_name);
	vd->vbm = vbit_new(8);
	AN(vd->vbm);
}

void
vdir_delete(struct vdir **vdp)
{
	struct vdir *vd;

	TAKE_OBJ_NOTNULL(vd, vdp, VDIR_MAGIC);

	AZ(vd->dir);
	free(vd->backend);
	free(vd->weight);
	AZ(pthread_rwlock_destroy(&vd->mtx));
	vbit_destroy(vd->vbm);
	FREE_OBJ(vd);
}

void
vdir_rdlock(struct vdir *vd)
{
	CHECK_OBJ_NOTNULL(vd, VDIR_MAGIC);
	AZ(pthread_rwlock_rdlock(&vd->mtx));
}

void
vdir_wrlock(struct vdir *vd)
{
	CHECK_OBJ_NOTNULL(vd, VDIR_MAGIC);
	AZ(pthread_rwlock_wrlock(&vd->mtx));
}

void
vdir_unlock(struct vdir *vd)
{
	CHECK_OBJ_NOTNULL(vd, VDIR_MAGIC);
	AZ(pthread_rwlock_unlock(&vd->mtx));
}


void
vdir_add_backend(VRT_CTX, struct vdir *vd, VCL_BACKEND be, double weight)
{
	unsigned u;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vd, VDIR_MAGIC);
	if (be == NULL) {
		VRT_fail(ctx, "%s: NULL backend cannot be added",
		    VRT_BACKEND_string(vd->dir));
		return;
	}
	AN(be);
	vdir_wrlock(vd);
	if (vd->n_backend >= vd->l_backend)
		vdir_expand(vd, vd->l_backend + 16);
	assert(vd->n_backend < vd->l_backend);
	u = vd->n_backend++;
	vd->backend[u] = be;
	vd->weight[u] = weight;
	vd->total_weight += weight;
	vdir_unlock(vd);
}

void
vdir_remove_backend(VRT_CTX, struct vdir *vd, VCL_BACKEND be, unsigned *cur)
{
	unsigned u, n;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vd, VDIR_MAGIC);
	if (be == NULL) {
		VRT_fail(ctx, "%s: NULL backend cannot be removed",
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
	vd->total_weight -= vd->weight[u];
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
	double c;

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

static unsigned
vdir_pick_by_weight(const struct vdir *vd, double w,
    const struct vbitmap *blacklist)
{
	double a = 0.0;
	VCL_BACKEND be = NULL;
	unsigned u;

	AN(blacklist);
	for (u = 0; u < vd->n_backend; u++) {
		be = vd->backend[u];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
		if (vbit_test(blacklist, u))
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
	double tw = 0.0;
	VCL_BACKEND be = NULL;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vd, VDIR_MAGIC);
	vdir_wrlock(vd);
	for (u = 0; u < vd->n_backend; u++) {
		if (VRT_Healthy(ctx, vd->backend[u], NULL)) {
			vbit_clr(vd->vbm, u);
			tw += vd->weight[u];
		} else
			vbit_set(vd->vbm, u);
	}
	if (tw > 0.0) {
		u = vdir_pick_by_weight(vd, w * tw, vd->vbm);
		assert(u < vd->n_backend);
		be = vd->backend[u];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
	}
	vdir_unlock(vd);
	return (be);
}
