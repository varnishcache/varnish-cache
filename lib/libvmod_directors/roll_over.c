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

struct vmod_directors_rollover {
	unsigned				magic;
#define VMOD_DIRECTORS_ROLL_OVER_MAGIC	0xd992217a
	struct vdir				*vd;
	unsigned				cur;
};

static unsigned __match_proto__(vdi_healthy)
vmod_ro_healthy(const struct director *dir, const struct busyobj *bo,
    double *changed)
{
	struct vmod_directors_rollover *ro;

	CAST_OBJ_NOTNULL(ro, dir->priv, VMOD_DIRECTORS_ROLL_OVER_MAGIC);
	return (vdir_any_healthy(ro->vd, bo, changed));
}

static const struct director * __match_proto__(vdi_resolve_f)
vmod_ro_resolve(const struct director *dir, struct worker *wrk,
    struct busyobj *bo)
{
	struct vmod_directors_rollover *ro;
	unsigned u;
	VCL_BACKEND be = NULL;

	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CAST_OBJ_NOTNULL(ro, dir->priv, VMOD_DIRECTORS_ROLL_OVER_MAGIC);
	vdir_rdlock(ro->vd);
	assert(ro->cur < ro->vd->n_backend);
	for (u = 0; u < ro->vd->n_backend; u++) {
		be = ro->vd->backend[ro->cur];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
		if (be->healthy(be, bo, NULL))
			break;
		ro->cur++;
		if (ro->cur >= ro->vd->n_backend)
			ro->cur -= ro->vd->n_backend;
	}
	vdir_unlock(ro->vd);
	if (u == ro->vd->n_backend)
		be = NULL;
	return (be);
}

VCL_VOID __match_proto__()
vmod_rollover__init(VRT_CTX,
    struct vmod_directors_rollover **rop, const char *vcl_name)
{
	struct vmod_directors_rollover *ro;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(rop);
	AZ(*rop);
	ALLOC_OBJ(ro, VMOD_DIRECTORS_ROLL_OVER_MAGIC);
	AN(ro);
	*rop = ro;
	vdir_new(&ro->vd, "rollover", vcl_name, vmod_ro_healthy,
	    vmod_ro_resolve, ro);
}

VCL_VOID __match_proto__()
vmod_rollover__fini(struct vmod_directors_rollover **rop)
{
	struct vmod_directors_rollover *ro;

	ro = *rop;
	*rop = NULL;
	CHECK_OBJ_NOTNULL(ro, VMOD_DIRECTORS_ROLL_OVER_MAGIC);
	vdir_delete(&ro->vd);
	FREE_OBJ(ro);
}

VCL_VOID __match_proto__()
vmod_rollover_add_backend(VRT_CTX,
    struct vmod_directors_rollover *ro, VCL_BACKEND be)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ro, VMOD_DIRECTORS_ROLL_OVER_MAGIC);
	(void)vdir_add_backend(ro->vd, be, 0.0);
}

/* this one is vdir_remove_backend but set ro->cur if needed */
VCL_VOID __match_proto__()
vmod_rollover_remove_backend(VRT_CTX,
    struct vmod_directors_rollover *ro, VCL_BACKEND be)
{
	unsigned u, n;
	struct vdir *vd;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ro, VMOD_DIRECTORS_ROLL_OVER_MAGIC);

	vd = ro->vd;
	if (be == NULL)
		return;
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
	if (u < ro->cur)
		ro->cur--;
	vdir_unlock(vd);
	return;

}

VCL_BACKEND __match_proto__()
vmod_rollover_backend(VRT_CTX,
    struct vmod_directors_rollover *ro)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ro, VMOD_DIRECTORS_ROLL_OVER_MAGIC);
	return (ro->vd->dir);
}
