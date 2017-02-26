/*-
 * Copyright 2009-2016 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Authors: Julian Wiesener <jw@uplex.de>
 *	    Nils Goroll <slink@uplex.de>
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

#include "vrt.h"
#include "vend.h"

#include "vcc_if.h"
#include "shard_dir.h"
#include "shard_cfg.h"
#include "shard_hash.h"

struct vmod_directors_shard {
	unsigned		magic;
#define VMOD_SHARD_SHARD_MAGIC	0x6e63e1bf
	struct sharddir	*shardd;
};

VCL_VOID __match_proto__(td_directors_shard__init)
vmod_shard__init(VRT_CTX, struct vmod_directors_shard **vshardp,
    const char *vcl_name)
{
	struct vmod_directors_shard *vshard;
	VCL_INT t1;
	uint32_t t2a, t2b;

	/* see vmod_key comment */
	assert(sizeof(VCL_INT) >= sizeof(uint32_t));
	t2a = UINT32_MAX;
	t1 = (VCL_INT)t2a;
	t2b = (uint32_t)t1;
	assert(t2a == t2b);

	(void)ctx;
	AN(vshardp);
	AZ(*vshardp);
	ALLOC_OBJ(vshard, VMOD_SHARD_SHARD_MAGIC);
	AN(vshard);

	*vshardp = vshard;
	sharddir_new(&vshard->shardd, vcl_name);
}

VCL_VOID __match_proto__(td_directors_shard__fini)
vmod_shard__fini(struct vmod_directors_shard **vshardp)
{
	struct vmod_directors_shard *vshard = *vshardp;

	*vshardp = NULL;
	CHECK_OBJ_NOTNULL(vshard, VMOD_SHARD_SHARD_MAGIC);
	sharddir_delete(&vshard->shardd);
	FREE_OBJ(vshard);
}

/*
 * our key is a uint32_t, but VCL_INT is a (signed) long.  We cast back and
 * forth, asserting in vmod_shard__init() that VCL_INT is a large enough
 * container
 */
VCL_INT __match_proto__(td_directors_shard_key)
    vmod_shard_key(VRT_CTX, struct vmod_directors_shard *vshard,
    VCL_STRING s, VCL_ENUM alg_s)
{
	enum alg_e alg = parse_alg_e(alg_s);
	hash_func hash_fp = shard_hash_f[alg];

	(void)ctx;
	(void)vshard;;

	return (VCL_INT)hash_fp(s ? s : "");
}

VCL_VOID __match_proto__(td_directors_set_warmup)
vmod_shard_set_warmup(VRT_CTX, struct vmod_directors_shard *vshard,
    VCL_REAL probability)
{
	CHECK_OBJ_NOTNULL(vshard, VMOD_SHARD_SHARD_MAGIC);
	if (probability < 0 || probability >= 1) {
		shard_err(ctx, vshard->shardd,
		    ".set_warmup(%f) ignored", probability);
		return;
	}
	shardcfg_set_warmup(vshard->shardd, probability);
}

VCL_VOID __match_proto__(td_directors_set_rampup)
vmod_shard_set_rampup(VRT_CTX, struct vmod_directors_shard *vshard,
    VCL_DURATION duration)
{
	(void)ctx;
	CHECK_OBJ_NOTNULL(vshard, VMOD_SHARD_SHARD_MAGIC);
	shardcfg_set_rampup(vshard->shardd, duration);
}

VCL_BOOL __match_proto__(td_directors_shard_add_backend)
vmod_shard_add_backend(VRT_CTX, struct vmod_directors_shard *vshard,
    struct vmod_priv *priv,
    VCL_BACKEND be, VCL_STRING ident, VCL_DURATION rampup)
{
	CHECK_OBJ_NOTNULL(vshard, VMOD_SHARD_SHARD_MAGIC);

	if (be == NULL) {
		shard_err0(ctx, vshard->shardd,
		    ".backend_add() NULL backend given");
		return 0;
	}

	return shardcfg_add_backend(ctx, priv, vshard->shardd,
	    be, ident, rampup);
}

VCL_BOOL __match_proto__(td_directors_shard_remove_backend)
vmod_shard_remove_backend(VRT_CTX, struct vmod_directors_shard *vshard,
    struct vmod_priv *priv,
    VCL_BACKEND be, VCL_STRING ident)
{
	CHECK_OBJ_NOTNULL(vshard, VMOD_SHARD_SHARD_MAGIC);

	if (be == NULL && ident == NULL) {
		shard_err0(ctx, vshard->shardd,
		    ".backend_remove() at least one of backend "
		    "and ident must be given");
		return 0;
	}

	return shardcfg_remove_backend(ctx, priv, vshard->shardd,
	    be, ident);
}

VCL_BOOL __match_proto__(td_directors_shard_clear)
vmod_shard_clear(VRT_CTX, struct vmod_directors_shard *vshard,
    struct vmod_priv *priv)
{
	CHECK_OBJ_NOTNULL(vshard, VMOD_SHARD_SHARD_MAGIC);
	return shardcfg_clear(ctx, priv, vshard->shardd);
}

VCL_BOOL __match_proto__(td_directors_shard_reconfigure)
vmod_shard_reconfigure(VRT_CTX, struct vmod_directors_shard *vshard,
    struct vmod_priv *priv, VCL_INT replicas, VCL_ENUM alg_s)
{
	enum alg_e alg = parse_alg_e(alg_s);

	return shardcfg_reconfigure(ctx, priv, vshard->shardd, replicas, alg);
}

static inline uint32_t
get_key(VRT_CTX, enum by_e by, VCL_INT key_int, VCL_BLOB key_blob)
{
	struct http *http;
	uint8_t k[4] = { 0 };
	uint8_t *b;
	int i, ki;

	switch (by) {
	case BY_HASH:
		if (ctx->bo) {
			CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
			return (vbe32dec(ctx->bo->digest));
		}
		/* FALLTHROUGH */
	case BY_URL:
		if (ctx->http_req) {
			AN(http = ctx->http_req);
		} else {
			AN(ctx->http_bereq);
			AN(http = ctx->http_bereq);
		}
		return (shard_hash_f[SHA256](http->hd[HTTP_HDR_URL].b));
	case BY_KEY:
		return ((uint32_t)key_int);
	case BY_BLOB:
		assert(key_blob);
		assert(key_blob->len > 0);
		assert(key_blob->priv != NULL);

		if (key_blob->len >= 4)
			ki = 0;
		else
			ki = 4 - key_blob->len;

		b = key_blob->priv;
		for (i = 0; ki < 4; i++, ki++)
			k[ki] = b[i];
		assert(i <= key_blob->len);

		return (vbe32dec(k));
	default:
		WRONG("by value");
	}
}

VCL_BACKEND __match_proto__(td_directors_shard_backend)
vmod_shard_backend(VRT_CTX, struct vmod_directors_shard *vshard,
    VCL_ENUM by_s, VCL_INT key_int, VCL_BLOB key_blob, VCL_INT alt,
    VCL_REAL warmup, VCL_BOOL rampup, VCL_ENUM healthy_s)
{
	enum by_e	by	= parse_by_e(by_s);
	enum healthy_e	healthy = parse_healthy_e(healthy_s);

	uint32_t	key;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vshard, VMOD_SHARD_SHARD_MAGIC);

	if (key_int && by != BY_KEY) {
		shard_err(ctx, vshard->shardd,
		    "by=%s but key argument used", by_s);
		return NULL;
	}

	if (key_blob && by != BY_BLOB) {
		shard_err(ctx, vshard->shardd,
		    "by=%s but key_blob argument used", by_s);
		return NULL;
	}

	if (by == BY_BLOB) {
		if (key_blob == NULL ||
		    key_blob->len <= 0 ||
		    key_blob->priv == NULL) {
			shard_err0(ctx, vshard->shardd,
			    "by=BLOB but no or empty key_blob "
			    "- using key 0");
			by = BY_KEY;
			key_int = 0;
		}
	}

	key = get_key(ctx, by, key_int, key_blob);

	return (sharddir_pick_be(ctx, vshard->shardd,
		key, alt, warmup, rampup, healthy));
}

VCL_VOID __match_proto__(td_directors_shard_backend)
vmod_shard_debug(VRT_CTX, struct vmod_directors_shard *vshard,
    VCL_INT i)
{
	CHECK_OBJ_NOTNULL(vshard, VMOD_SHARD_SHARD_MAGIC);

	(void)ctx;
	sharddir_debug(vshard->shardd, i & UINT32_MAX);
}
