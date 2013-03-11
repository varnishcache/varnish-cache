/*-
 * Copyright (c) 2013 Varnish Software AS
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
#include "cache/cache_backend.h"

#include "vrt.h"

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
vdir_new(struct vdir **vdp, const char *vcl_name, vdi_healthy *healthy,
    vdi_getfd_f *getfd, void *priv)
{
	struct vdir *vd;

	AN(vcl_name);
	AN(vdp);
	AZ(*vdp);
	ALLOC_OBJ(vd, VDIR_MAGIC);
	AN(vd);
	*vdp = vd;
	AZ(pthread_mutex_init(&vd->mtx, NULL));

	ALLOC_OBJ(vd->dir, DIRECTOR_MAGIC);
	AN(vd->dir);
	REPLACE(vd->dir->vcl_name, vcl_name);
	vd->dir->priv = priv;
	vd->dir->healthy = healthy;
	vd->dir->getfd = getfd;
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
	AZ(pthread_mutex_destroy(&vd->mtx));
	FREE_OBJ(vd->dir);
	FREE_OBJ(vd);
}

void
vdir_lock(struct vdir *vd)
{
	CHECK_OBJ_NOTNULL(vd, VDIR_MAGIC);
	AZ(pthread_mutex_lock(&vd->mtx));
}

void
vdir_unlock(struct vdir *vd)
{
	CHECK_OBJ_NOTNULL(vd, VDIR_MAGIC);
	AZ(pthread_mutex_unlock(&vd->mtx));
}


unsigned
vdir_add_backend(struct vdir *vd, VCL_BACKEND be, double weight)
{
	unsigned u;

	CHECK_OBJ_NOTNULL(vd, VDIR_MAGIC);
	AN(be);
	vdir_lock(vd);
	if (vd->n_backend >= vd->l_backend)
		vdir_expand(vd, vd->l_backend + 16);
	assert(vd->n_backend < vd->l_backend);
	u = vd->n_backend++;
	vd->backend[u] = be;
	vd->weight[u] = weight;
	vdir_unlock(vd);
	return (u);
}

unsigned
vdir_any_healthy(struct vdir *vd, const struct req *req)
{
	unsigned retval = 0;
	VCL_BACKEND be;
	unsigned u;

	CHECK_OBJ_NOTNULL(vd, VDIR_MAGIC);
	vdir_lock(vd);
	for (u = 0; u < vd->n_backend; u++) {
		be = vd->backend[u];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
		if (be->healthy(be, req)) {
			retval = 1;
			break;
		}
	}
	vdir_unlock(vd);
	return (retval);
}
