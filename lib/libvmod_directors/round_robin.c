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
#include "vcc_if.h"

#include "vdir.h"

struct vmod_directors_round_robin {
	unsigned				magic;
#define VMOD_DIRECTORS_ROUND_ROBIN_MAGIC	0xa80970cf
	struct vdir				*vd;
	unsigned				nxt;
};

static unsigned __match_proto__(vdi_healthy)
vmod_rr_healthy(const struct director *dir, const uint8_t *digest)
{
	struct vmod_directors_round_robin *rr;

	CAST_OBJ_NOTNULL(rr, dir->priv, VMOD_DIRECTORS_ROUND_ROBIN_MAGIC);
	return (vdir_any_healthy(rr->vd, digest));
}

static struct vbc * __match_proto__(vdi_getfd_f)
vmod_rr_getfd(const struct director *dir, struct req *req)
{
	struct vmod_directors_round_robin *rr;
	unsigned u;
	VCL_BACKEND be = NULL;

	CAST_OBJ_NOTNULL(rr, dir->priv, VMOD_DIRECTORS_ROUND_ROBIN_MAGIC);
	vdir_lock(rr->vd);
	for (u = 0; u < rr->vd->n_backend; u++) {
		rr->nxt %= rr->vd->n_backend;
		be = rr->vd->backend[rr->nxt];
		rr->nxt++;
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
		if (be->healthy(be, req->digest))
			break;
	}
	vdir_unlock(rr->vd);
	if (u == rr->vd->n_backend || be == NULL)
		return (NULL);
	return (be->getfd(be, req));
}

VCL_VOID __match_proto__()
vmod_round_robin__init(struct req *req, struct vmod_directors_round_robin **rrp,
    const char *vcl_name)
{
	struct vmod_directors_round_robin *rr;

	AZ(req);
	AN(rrp);
	AZ(*rrp);
	ALLOC_OBJ(rr, VMOD_DIRECTORS_ROUND_ROBIN_MAGIC);
	AN(rr);
	*rrp = rr;
	vdir_new(&rr->vd, vcl_name, vmod_rr_healthy, vmod_rr_getfd, rr);
}

VCL_VOID __match_proto__()
vmod_round_robin__fini(struct req *req, struct vmod_directors_round_robin **rrp)
{
	struct vmod_directors_round_robin *rr;

	AZ(req);
	rr = *rrp;
	*rrp = NULL;
	CHECK_OBJ_NOTNULL(rr, VMOD_DIRECTORS_ROUND_ROBIN_MAGIC);
	vdir_delete(&rr->vd);
	FREE_OBJ(rr);
}

VCL_VOID __match_proto__()
vmod_round_robin_add_backend(struct req *req,
    struct vmod_directors_round_robin *rr, VCL_BACKEND be)
{

	(void)req;
	CHECK_OBJ_NOTNULL(rr, VMOD_DIRECTORS_ROUND_ROBIN_MAGIC);
	(void)vdir_add_backend(rr->vd, be, 0.0);
}

VCL_BACKEND __match_proto__()
vmod_round_robin_backend(struct req *req, struct vmod_directors_round_robin *rr)
{
	(void)req;
	CHECK_OBJ_NOTNULL(rr, VMOD_DIRECTORS_ROUND_ROBIN_MAGIC);
	return (rr->vd->dir);
}
