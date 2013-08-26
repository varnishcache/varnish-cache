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

#include <math.h>
#include <stdlib.h>

#include "cache/cache.h"
#include "cache/cache_backend.h"

#include "vrt.h"
#include "vbm.h"
#include "vend.h"
#include "vsha256.h"

#include "vdir.h"

#include "vcc_if.h"

struct vmod_directors_hash {
	unsigned				magic;
#define VMOD_DIRECTORS_HASH_MAGIC		0xc08dd611
	struct vdir				*vd;
	unsigned				nloops;
	struct vbitmap				*vbm;
};

VCL_VOID __match_proto__()
vmod_hash__init(const struct vrt_ctx *ctx, struct vmod_directors_hash **rrp,
    const char *vcl_name)
{
	struct vmod_directors_hash *rr;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(rrp);
	AZ(*rrp);
	ALLOC_OBJ(rr, VMOD_DIRECTORS_HASH_MAGIC);
	AN(rr);
	rr->vbm = vbit_init(8);
	AN(rr->vbm);
	rr->nloops = 3; //
	*rrp = rr;
	vdir_new(&rr->vd, vcl_name, NULL, NULL, rr);
}

VCL_VOID __match_proto__()
vmod_hash__fini(struct vmod_directors_hash **rrp)
{
	struct vmod_directors_hash *rr;

	rr = *rrp;
	*rrp = NULL;
	CHECK_OBJ_NOTNULL(rr, VMOD_DIRECTORS_HASH_MAGIC);
	vdir_delete(&rr->vd);
	vbit_destroy(rr->vbm);
	FREE_OBJ(rr);
}

VCL_VOID __match_proto__()
vmod_hash_add_backend(const struct vrt_ctx *ctx,
    struct vmod_directors_hash *rr, VCL_BACKEND be, double w)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(rr, VMOD_DIRECTORS_HASH_MAGIC);
	(void)vdir_add_backend(rr->vd, be, w);
}

VCL_BACKEND __match_proto__()
vmod_hash_backend(const struct vrt_ctx *ctx, struct vmod_directors_hash *rr,
    const char *arg, ...)
{
	struct SHA256Context sha_ctx;
	va_list ap;
	const char *p;
	unsigned char sha256[SHA256_LEN];
	VCL_BACKEND be;
	double r;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	CHECK_OBJ_NOTNULL(rr, VMOD_DIRECTORS_HASH_MAGIC);
	SHA256_Init(&sha_ctx);
	va_start(ap, arg);
	p = arg;
	while (p != vrt_magic_string_end) {
		SHA256_Update(&sha_ctx, arg, strlen(arg));
		p = va_arg(ap, const char *);
	}
	va_end(ap);
	SHA256_Final(sha256, &sha_ctx);

	r = vbe32dec(sha256);
	r = scalbn(r, -32);
	assert(r >= 0 && r <= 1.0);
	be = vdir_pick_be(rr->vd, r, rr->nloops);
	return (be);
}
