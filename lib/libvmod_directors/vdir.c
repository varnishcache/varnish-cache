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
vdir_new(struct vdir **vdp, const char *name, const char *vcl_name,
    vdi_healthy_f *healthy, vdi_resolve_f *resolve, void *priv)
{
	struct vdir *vd;

	AN(name);
	AN(vcl_name);
	AN(vdp);
	AZ(*vdp);
	ALLOC_OBJ(vd, VDIR_MAGIC);
	AN(vd);
	*vdp = vd;
	AZ(pthread_rwlock_init(&vd->mtx, NULL));

	ALLOC_OBJ(vd->dir, DIRECTOR_MAGIC);
	AN(vd->dir);
	vd->dir->name = name;
	REPLACE(vd->dir->vcl_name, vcl_name);
	vd->dir->priv = priv;
	vd->dir->healthy = healthy;
	vd->dir->resolve = resolve;
	vd->vbm = vbit_init(8);
	AN(vd->vbm);
}

void
vdir_delete(struct vdir **vdp)
{
	struct vdir *vd;

	AN(vdp);
	vd = *vdp;
	*vdp = NULL;

	CHECK_OBJ_NOTNULL(vd, VDIR_MAGIC);

	free(vd->backend);
	free(vd->weight);
	AZ(pthread_rwlock_destroy(&vd->mtx));
	free(vd->dir->vcl_name);
	FREE_OBJ(vd->dir);
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


unsigned
vdir_add_backend(struct vdir *vd, VCL_BACKEND be, double weight)
{
	unsigned u;

	CHECK_OBJ_NOTNULL(vd, VDIR_MAGIC);
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
	return (u);
}

unsigned
vdir_remove_backend(struct vdir *vd, VCL_BACKEND be)
{
	unsigned u, n;

	CHECK_OBJ_NOTNULL(vd, VDIR_MAGIC);
	if (be == NULL)
		return (vd->n_backend);
	CHECK_OBJ(be, DIRECTOR_MAGIC);
	vdir_wrlock(vd);
	for (u = 0; u < vd->n_backend; u++)
		if (vd->backend[u] == be)
			break;
	if (u == vd->n_backend) {
		vdir_unlock(vd);
		return (vd->n_backend);
	}
	vd->total_weight -= vd->weight[u];
	n = (vd->n_backend - u) - 1;
	memmove(&vd->backend[u], &vd->backend[u+1], n * sizeof(vd->backend[0]));
	memmove(&vd->weight[u], &vd->weight[u+1], n * sizeof(vd->weight[0]));
	vd->n_backend--;
	vdir_unlock(vd);
	return (vd->n_backend);
}

unsigned
vdir_any_healthy(struct vdir *vd, const struct busyobj *bo, double *changed)
{
	unsigned retval = 0;
	VCL_BACKEND be;
	unsigned u;
	double c;

	CHECK_OBJ_NOTNULL(vd, VDIR_MAGIC);
	CHECK_OBJ_ORNULL(bo, BUSYOBJ_MAGIC);
	vdir_rdlock(vd);
	if (changed != NULL)
		*changed = 0;
	for (u = 0; u < vd->n_backend; u++) {
		be = vd->backend[u];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
		retval = be->healthy(be, bo, &c);
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
vdir_pick_be(struct vdir *vd, double w, const struct busyobj *bo)
{
	unsigned u;
	double tw = 0.0;
	VCL_BACKEND be = NULL;

	vdir_wrlock(vd);
	for (u = 0; u < vd->n_backend; u++) {
		if (vd->backend[u]->healthy(vd->backend[u], bo, NULL)) {
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
