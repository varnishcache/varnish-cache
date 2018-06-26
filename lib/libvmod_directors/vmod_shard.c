/*-
 * Copyright 2009-2018 UPLEX - Nils Goroll Systemoptimierung
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
#include <string.h>

#include "cache/cache.h"
#include "vcl.h"

#include "vend.h"

#include "vcc_if.h"
#include "shard_dir.h"
#include "shard_cfg.h"

/* -------------------------------------------------------------------------
 *  shard director: LAZY mode (vdi resolve function), parameter objects
 *
 *  By associating a parameter object with a shard director, we enable LAZY
 *  lookups as with the other directors. Parameter objects are defined with VCL
 *  scope (normal vmod objects), but can be overridden per backend request using
 *  a task priv.
 *
 *  We use the same concept to carry shard.backend() parameters to vdi resolve
 *  for LAZY mode: They get saved in a per-director task scope parameter object.
 *
 *  Each object points to another object providing defaults for values which are
 *  not defined.
 *
 *  Actual resolution of the various parameter objects does not happen before
 *  they are used, which enabled changing them independently (ie, shard
 *  .backend() parameters have precedence over an associated parameter object,
 *  which by itself can be overridden).
 *
 *  Overview of parameter objects (pointers are alternatives)
 *
 *  shard() director        shard_param() object    default praram
 *
 *               --------------------------------->   vmod static
 *    VCL obj   /                                ->
 *    .param  -+--------->    VCL obj           /  _
 *                            .default  --------   /|
 *                                                /
 *                               ^               /
 *                               |              /
 *                                             /
 *                            .default        /
 *          ------------->    TASK priv      /
 *         /                                /
 *    .default -----------------------------
 *    TASK priv
 */

/* -------------------------------------------------------------------------
 * method arguments and set parameters bitmask in vmod_directors_shard_param
 */

#define arg_by		((uint32_t)1)
#define arg_key		((uint32_t)1 << 1)
#define arg_key_blob	((uint32_t)1 << 2)
#define arg_alt		((uint32_t)1 << 3)
#define arg_warmup	((uint32_t)1 << 4)
#define arg_rampup	((uint32_t)1 << 5)
#define arg_healthy	((uint32_t)1 << 6)
#define arg_param	((uint32_t)1 << 7)
#define arg_resolve	((uint32_t)1 << 8)
#define _arg_mask	((arg_resolve << 1) - 1)
/* allowed in shard_param.set */
#define _arg_mask_set	(arg_param - 1)
/* allowed in shard_param */
#define _arg_mask_param ( _arg_mask_set		\
			  & ~arg_key			\
			  & ~arg_key_blob )

/* -------------------------------------------------------------------------
 * shard parameters - declaration & defaults
 */
enum vmod_directors_shard_param_scope {
	_SCOPE_INVALID = 0,
	SCOPE_VMOD,
	SCOPE_VCL,
	SCOPE_TASK,
	SCOPE_STACK
};

struct vmod_directors_shard_param;

struct vmod_directors_shard_param {
	unsigned				magic;
#define VMOD_SHARD_SHARD_PARAM_MAGIC		0xdf5ca117

	/* internals */
	uint32_t				key;
	const char				*vcl_name;
	const struct vmod_directors_shard_param *defaults;
	enum vmod_directors_shard_param_scope	scope;

	/* parameters */
	enum by_e				by;
	enum healthy_e				healthy;
	uint32_t				mask;
	VCL_BOOL				rampup;
	VCL_INT					alt;
	VCL_REAL				warmup;
};

static const struct vmod_directors_shard_param shard_param_default = {
	.magic		= VMOD_SHARD_SHARD_PARAM_MAGIC,

	.key		= 0,
	.vcl_name	= "builtin defaults",
	.defaults	= NULL,
	.scope		= SCOPE_VMOD,

	.mask		= _arg_mask_param,
	.by		= BY_HASH,
	.healthy	= CHOSEN,
	.rampup	= 1,
	.alt		= 0,
	.warmup		= -1,
};

static struct vmod_directors_shard_param *
shard_param_stack(struct vmod_directors_shard_param *p,
    const struct vmod_directors_shard_param *pa, const char *who);

static struct vmod_directors_shard_param *
shard_param_task(VRT_CTX, const void *id,
    const struct vmod_directors_shard_param *pa);

static const struct vmod_directors_shard_param *
shard_param_blob(const VCL_BLOB blob);

static const struct vmod_directors_shard_param *
vmod_shard_param_read(VRT_CTX, const void *id,
    const struct vmod_directors_shard_param *p,
    struct vmod_directors_shard_param *pstk, const char *who);

/* -------------------------------------------------------------------------
 * shard vmod interface
 */
static vdi_healthy_f vmod_shard_healthy;
static vdi_resolve_f vmod_shard_resolve;

struct vmod_directors_shard {
	unsigned				magic;
#define VMOD_SHARD_SHARD_MAGIC			0x6e63e1bf
	struct sharddir				*shardd;
	VCL_BACKEND				dir;
};

static enum by_e
parse_by_e(VCL_ENUM e)
{
#define VMODENUM(n) if (e == vmod_enum_ ## n) return(BY_ ## n);
#include "tbl_by.h"
       WRONG("illegal by enum");
}

static enum healthy_e
parse_healthy_e(VCL_ENUM e)
{
#define VMODENUM(n) if (e == vmod_enum_ ## n) return(n);
#include "tbl_healthy.h"
       WRONG("illegal healthy enum");
}

static enum resolve_e
parse_resolve_e(VCL_ENUM e)
{
#define VMODENUM(n) if (e == vmod_enum_ ## n) return(n);
#include "tbl_resolve.h"
       WRONG("illegal resolve enum");
}

static const char * const by_str[_BY_E_MAX] = {
	[_BY_E_INVALID] = "*INVALID*",
#define VMODENUM(n) [BY_ ## n] = #n,
#include "tbl_by.h"
};

static const char * const healthy_str[_HEALTHY_E_MAX] = {
	[_HEALTHY_E_INVALID] = "*INVALID*",
#define VMODENUM(n) [n] = #n,
#include "tbl_healthy.h"
};

static void
shard__assert(void)
{
	VCL_INT t1;
	uint32_t t2a, t2b;

	/* we put our uint32 key in a VCL_INT container */
	assert(sizeof(VCL_INT) >= sizeof(uint32_t));
	t2a = UINT32_MAX;
	t1 = (VCL_INT)t2a;
	t2b = (uint32_t)t1;
	assert(t2a == t2b);
}

static void v_matchproto_(vdi_destroy_f)
vmod_shard_destroy(VCL_BACKEND dir)
{
	struct sharddir *shardd;

	CAST_OBJ_NOTNULL(shardd, dir->priv, SHARDDIR_MAGIC);
	sharddir_delete(&shardd);
}

static const struct vdi_methods vmod_shard_methods[1] = {{
	.magic =	VDI_METHODS_MAGIC,
	.type =		"shard",
	.resolve =	vmod_shard_resolve,
	.healthy =	vmod_shard_healthy,
	.destroy =	vmod_shard_destroy
}};


VCL_VOID v_matchproto_(td_directors_shard__init)
vmod_shard__init(VRT_CTX, struct vmod_directors_shard **vshardp,
    const char *vcl_name)
{
	struct vmod_directors_shard *vshard;

	shard__assert();

	AN(vshardp);
	AZ(*vshardp);
	ALLOC_OBJ(vshard, VMOD_SHARD_SHARD_MAGIC);
	AN(vshard);

	*vshardp = vshard;
	sharddir_new(&vshard->shardd, vcl_name, &shard_param_default);

	vshard->dir = VRT_AddDirector(ctx, vmod_shard_methods, vshard->shardd,
	    "%s", vcl_name);
}

VCL_VOID v_matchproto_(td_directors_shard__fini)
vmod_shard__fini(struct vmod_directors_shard **vshardp)
{
	struct vmod_directors_shard *vshard;

	TAKE_OBJ_NOTNULL(vshard, vshardp, VMOD_SHARD_SHARD_MAGIC);
	VRT_DelDirector(&vshard->dir);
	FREE_OBJ(vshard);
}

VCL_INT v_matchproto_(td_directors_shard_key)
vmod_shard_key(VRT_CTX, struct vmod_directors_shard *vshard, const char *s, ...)
{
	va_list ap;
	uint32_t r;

	(void)ctx;
	(void)vshard;

	va_start(ap, s);
	r = sharddir_sha256v(s, ap);
	va_end(ap);

	return ((VCL_INT)r);
}

VCL_VOID v_matchproto_(td_directors_set_warmup)
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

VCL_VOID v_matchproto_(td_directors_set_rampup)
vmod_shard_set_rampup(VRT_CTX, struct vmod_directors_shard *vshard,
    VCL_DURATION duration)
{
	(void)ctx;
	CHECK_OBJ_NOTNULL(vshard, VMOD_SHARD_SHARD_MAGIC);
	shardcfg_set_rampup(vshard->shardd, duration);
}

VCL_VOID v_matchproto_(td_directors_shard_associate)
vmod_shard_associate(VRT_CTX,
    struct vmod_directors_shard *vshard, VCL_BLOB b)
{
	const struct vmod_directors_shard_param *ppt;
	CHECK_OBJ_NOTNULL(vshard, VMOD_SHARD_SHARD_MAGIC);

	if (b == NULL) {
		sharddir_set_param(vshard->shardd, &shard_param_default);
		return;
	}

	ppt = shard_param_blob(b);

	if (ppt == NULL) {
		VRT_fail(ctx, "shard .associate param invalid");
		return;
	}

	sharddir_set_param(vshard->shardd, ppt);
}

VCL_BOOL v_matchproto_(td_directors_shard_add_backend)
vmod_shard_add_backend(VRT_CTX, struct vmod_directors_shard *vshard,
    struct vmod_shard_add_backend_arg *args)
{
	CHECK_OBJ_NOTNULL(vshard, VMOD_SHARD_SHARD_MAGIC);

	if (args->backend == NULL) {
		shard_err0(ctx, vshard->shardd,
		    ".backend_add() NULL backend given");
		return (0);
	}

	return shardcfg_add_backend(ctx, args->arg1,
	    vshard->shardd, args->backend,
	    args->valid_ident ? args->ident : NULL,
	    args->valid_rampup ? args->rampup : nan(""));
}

VCL_BOOL v_matchproto_(td_directors_shard_remove_backend)
vmod_shard_remove_backend(VRT_CTX, struct vmod_directors_shard *vshard,
    struct vmod_shard_remove_backend_arg *args)
{
	VCL_BACKEND be = args->valid_backend ? args->backend : NULL;
	VCL_STRING ident = args->valid_ident ? args->ident : NULL;

	CHECK_OBJ_NOTNULL(vshard, VMOD_SHARD_SHARD_MAGIC);

	if (be == NULL && ident == NULL) {
		shard_err0(ctx, vshard->shardd,
		    ".backend_remove() at least one of backend "
		    "and ident must be given");
		return 0;
	}

	return shardcfg_remove_backend(ctx, args->arg1, vshard->shardd,
	    be, ident);
}

VCL_BOOL v_matchproto_(td_directors_shard_clear)
vmod_shard_clear(VRT_CTX, struct vmod_directors_shard *vshard,
    struct vmod_priv *priv)
{
	CHECK_OBJ_NOTNULL(vshard, VMOD_SHARD_SHARD_MAGIC);
	return shardcfg_clear(ctx, priv, vshard->shardd);
}

VCL_BOOL v_matchproto_(td_directors_shard_reconfigure)
vmod_shard_reconfigure(VRT_CTX, struct vmod_directors_shard *vshard,
    struct vmod_priv *priv, VCL_INT replicas)
{
	return shardcfg_reconfigure(ctx, priv, vshard->shardd, replicas);
}

static inline uint32_t
shard_get_key(VRT_CTX, const struct vmod_directors_shard_param *p)
{
	struct http *http;

	switch (p->by) {
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
		return (sharddir_sha256(http->hd[HTTP_HDR_URL].b,
					vrt_magic_string_end));
	case BY_KEY:
	case BY_BLOB:
		return (p->key);
	default:
		WRONG("by enum");
	}
}

/*
 * merge parameters to resolve all undef values
 * key is to be calculated after merging
 */
static void
shard_param_merge(struct vmod_directors_shard_param *to,
		  const struct vmod_directors_shard_param *from)
{
	CHECK_OBJ_NOTNULL(to, VMOD_SHARD_SHARD_PARAM_MAGIC);
	assert((to->mask & ~_arg_mask_param) == 0);

	if (to->mask == _arg_mask_param)
		return;

	CHECK_OBJ_NOTNULL(from, VMOD_SHARD_SHARD_PARAM_MAGIC);
	assert((from->mask & ~_arg_mask_param) == 0);

	if ((to->mask & arg_by) == 0 && (from->mask & arg_by) != 0) {
		to->by = from->by;
		if (from->by == BY_KEY || from->by == BY_BLOB)
			to->key = from->key;
	}

#define mrg(to, from, field) do {					\
		if (((to)->mask & arg_ ## field) == 0 &&		\
		    ((from)->mask & arg_ ## field) != 0)		\
			(to)->field = (from)->field;			\
	} while(0)

	mrg(to, from, healthy);
	mrg(to, from, rampup);
	mrg(to, from, alt);
	mrg(to, from, warmup);
#undef mrg

	to->mask |= from->mask;

	if (to->mask == _arg_mask_param)
		return;

	AN(from->defaults);
	shard_param_merge(to, from->defaults);
}

static uint32_t
shard_blob_key(VCL_BLOB key_blob)
{
	uint8_t k[4] = { 0 };
	uint8_t *b;
	int i, ki;

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
}

/*
 * convert vmod interface valid_* to our bitmask
 */

#define tobit(args, name) ((args)->valid_##name ? arg_##name : 0)

static uint32_t
shard_backend_arg_mask(const struct vmod_shard_backend_arg * const a)
{
	return (tobit(a, by)		|
		tobit(a, key)		|
		tobit(a, key_blob)	|
		tobit(a, alt)		|
		tobit(a, warmup)	|
		tobit(a, rampup)	|
		tobit(a, healthy)	|
		tobit(a, param)		|
		tobit(a, resolve));
}
static uint32_t
shard_param_set_mask(const struct vmod_shard_param_set_arg * const a)
{
	return (tobit(a, by)		|
		tobit(a, key)		|
		tobit(a, key_blob)	|
		tobit(a, alt)		|
		tobit(a, warmup)	|
		tobit(a, rampup)	|
		tobit(a, healthy));
}
#undef tobit

/*
 * check arguments and return in a struct param
 */
static struct vmod_directors_shard_param *
shard_param_args(VRT_CTX,
    struct vmod_directors_shard_param *p, const char *who,
    uint32_t args, VCL_ENUM by_s, VCL_INT key_int, VCL_BLOB key_blob,
    VCL_INT alt, VCL_REAL warmup, VCL_BOOL rampup, VCL_ENUM healthy_s)
{
	enum by_e	by;
	enum healthy_e	healthy;

	CHECK_OBJ_NOTNULL(p, VMOD_SHARD_SHARD_PARAM_MAGIC);
	AN(p->vcl_name);

	assert((args & ~_arg_mask_set) == 0);

	by = (args & arg_by) ? parse_by_e(by_s) : BY_HASH;
	healthy = (args & arg_healthy) ? parse_healthy_e(healthy_s) : CHOSEN;

	/* by_s / key_int / key_blob */
	if (args & arg_by) {
		switch (by) {
		case BY_KEY:
			if ((args & arg_key) == 0) {
				VRT_fail(ctx, "%s %s: "
					 "missing key argument with by=%s",
					 who, p->vcl_name, by_s);
				return (NULL);
			}
			if (key_int < 0 || key_int > UINT32_MAX) {
				VRT_fail(ctx, "%s %s: "
					 "invalid key argument %jd with by=%s",
					 who, p->vcl_name,
					 (intmax_t)key_int, by_s);
				return (NULL);
			}
			assert(key_int >= 0);
			assert(key_int <= UINT32_MAX);
			p->key = (uint32_t)key_int;
			break;
		case BY_BLOB:
			if ((args & arg_key_blob) == 0) {
				VRT_fail(ctx, "%s %s: "
					 "missing key_blob argument with by=%s",
					 who, p->vcl_name, by_s);
				return (NULL);
			}
			if (key_blob == NULL || key_blob->len <= 0 ||
			    key_blob->priv == NULL) {
				sharddir_err(ctx, SLT_Error, "%s %s: "
					     "by=BLOB but no or empty key_blob "
					     "- using key 0",
					     who, p->vcl_name);
				p->key = 0;
			} else
				p->key = shard_blob_key(key_blob);
			break;
		case BY_HASH:
		case BY_URL:
			if (args & (arg_key|arg_key_blob)) {
				VRT_fail(ctx, "%s %s: "
					 "key and key_blob arguments are "
					 "invalid with by=%s",
					 who, p->vcl_name, by_s);
				return (NULL);
			}
			break;
		default:
			WRONG("by enum");
		}
		p->by = by;
	} else {
		/* (args & arg_by) == 0 */
		p->by = BY_HASH;

		if (args & (arg_key|arg_key_blob)) {
			VRT_fail(ctx, "%s %s: "
				 "key and key_blob arguments are "
				 "invalid with by=HASH (default)",
				 who, p->vcl_name);
			return (NULL);
		}
	}

	if (args & arg_alt) {
		if (alt < 0) {
			VRT_fail(ctx, "%s %s: "
				 "invalid alt argument %jd",
				 who, p->vcl_name, (intmax_t)alt);
			return (NULL);
		}
		p->alt = alt;
	}

	if (args & arg_warmup) {
		if ((warmup < 0 && warmup != -1) || warmup > 1) {
			VRT_fail(ctx, "%s %s: "
				 "invalid warmup argument %f",
				 who, p->vcl_name, warmup);
			return (NULL);
		}
		p->warmup = warmup;
	}

	if (args & arg_rampup)
		p->rampup = !!rampup;

	if (args & arg_healthy)
		p->healthy = healthy;

	p->mask = args & _arg_mask_param;
	return (p);
}

VCL_BACKEND v_matchproto_(td_directors_shard_backend)
vmod_shard_backend(VRT_CTX, struct vmod_directors_shard *vshard,
		   struct vmod_shard_backend_arg *a)
{
	struct vmod_directors_shard_param pstk;
	struct vmod_directors_shard_param *pp = NULL;
	const struct vmod_directors_shard_param *ppt;
	enum resolve_e resolve;
	uint32_t args = shard_backend_arg_mask(a);

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vshard, VMOD_SHARD_SHARD_MAGIC);
	assert((args & ~_arg_mask) == 0);

	if (args & arg_resolve)
		resolve = parse_resolve_e(a->resolve);
	else if (ctx->method & VCL_MET_TASK_H)
		resolve = LAZY;
	else
		resolve = NOW;

	switch (resolve) {
	case LAZY:
		if ((args & ~arg_resolve) == 0) {
			AN(vshard->dir);
			return (vshard->dir);
		}

		if ((ctx->method & VCL_MET_TASK_B) == 0) {
			VRT_fail(ctx, "shard .backend resolve=LAZY with other "
				 "parameters can only be used in backend "
				 "context");
			return (NULL);
		}

		assert(ctx->method & VCL_MET_TASK_B);

		pp = shard_param_task(ctx, vshard->shardd,
				      vshard->shardd->param);
		if (pp == NULL)
			return (NULL);
		pp->vcl_name = vshard->shardd->name;
		break;
	case NOW:
		if (ctx->method & VCL_MET_TASK_H) {
			VRT_fail(ctx,
				 "shard .backend resolve=NOW can not be "
				 "used in vcl_init{}/vcl_fini{}");
			return (NULL);
		}
		pp = shard_param_stack(&pstk, vshard->shardd->param,
				       vshard->shardd->name);
		break;
	default:
		WRONG("resolve enum");
	}

	AN(pp);
	if (args & arg_param) {
		ppt = shard_param_blob(a->param);
		if (ppt == NULL) {
			VRT_fail(ctx, "shard .backend param invalid");
			return (NULL);
		}
		pp->defaults = ppt;
	}

	pp = shard_param_args(ctx, pp, "shard.backend()",
			      args & _arg_mask_set,
			      a->by, a->key, a->key_blob, a->alt, a->warmup,
			      a->rampup, a->healthy);
	if (pp == NULL)
		return (NULL);

	if (resolve == LAZY)
		return (vshard->dir);

	assert(resolve == NOW);
	shard_param_merge(pp, pp->defaults);
	return (sharddir_pick_be(ctx, vshard->shardd,
				 shard_get_key(ctx, pp), pp->alt, pp->warmup,
				 pp->rampup, pp->healthy));
}

static VCL_BOOL v_matchproto_(vdi_healthy)
vmod_shard_healthy(VRT_CTX, VCL_BACKEND dir, VCL_TIME *changed)
{
	struct sharddir *shardd;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(shardd, dir->priv, SHARDDIR_MAGIC);
	return (sharddir_any_healthy(ctx, shardd, changed));
}

static VCL_BACKEND v_matchproto_(vdi_resolve_f)
vmod_shard_resolve(VRT_CTX, VCL_BACKEND dir)
{
	struct sharddir *shardd;
	struct vmod_directors_shard_param pstk[1];
	const struct vmod_directors_shard_param *pp;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(shardd, dir->priv, SHARDDIR_MAGIC);

	pp = vmod_shard_param_read(ctx, shardd, shardd->param,
				   pstk, "shard_resolve");
	if (pp == NULL)
		return (NULL);

	return (sharddir_pick_be(ctx, shardd,
				 shard_get_key(ctx, pp), pp->alt, pp->warmup,
				 pp->rampup, pp->healthy));
}

VCL_VOID v_matchproto_(td_directors_shard_backend)
vmod_shard_debug(VRT_CTX, struct vmod_directors_shard *vshard,
    VCL_INT i)
{
	CHECK_OBJ_NOTNULL(vshard, VMOD_SHARD_SHARD_MAGIC);

	(void)ctx;
	sharddir_debug(vshard->shardd, i & UINT32_MAX);
}

/* =============================================================
 * shard_param
 */

VCL_VOID v_matchproto_(td_directors_shard_param__init)
vmod_shard_param__init(VRT_CTX,
    struct vmod_directors_shard_param **pp, const char *vcl_name)
{
	struct vmod_directors_shard_param *p;

	(void) ctx;
	AN(pp);
	AZ(*pp);
	ALLOC_OBJ(p, VMOD_SHARD_SHARD_PARAM_MAGIC);
	AN(p);
	p->vcl_name = vcl_name;
	p->scope = SCOPE_VCL;
	p->defaults = &shard_param_default;

	*pp = p;
}

VCL_VOID v_matchproto_(td_directors_shard_param__fini)
vmod_shard_param__fini(struct vmod_directors_shard_param **pp)
{
	struct vmod_directors_shard_param *p;

	if (*pp == NULL)
		return;
	TAKE_OBJ_NOTNULL(p, pp, VMOD_SHARD_SHARD_PARAM_MAGIC);
	FREE_OBJ(p);
}

/*
 * init a stack param struct defaulting to pa with the given name
 */
static struct vmod_directors_shard_param *
shard_param_stack(struct vmod_directors_shard_param *p,
    const struct vmod_directors_shard_param *pa, const char *who)
{
	CHECK_OBJ_NOTNULL(pa, VMOD_SHARD_SHARD_PARAM_MAGIC);
	assert(pa->scope > _SCOPE_INVALID);

	AN(p);
	INIT_OBJ(p, VMOD_SHARD_SHARD_PARAM_MAGIC);
	p->vcl_name = who;
	p->scope = SCOPE_STACK;
	p->defaults = pa;

	return (p);
}
/*
 * get a task scoped param struct for id defaulting to pa
 * if id != pa and pa has VCL scope, also get a task scoped param struct for pa
 */
static struct vmod_directors_shard_param *
shard_param_task(VRT_CTX, const void *id,
   const struct vmod_directors_shard_param *pa)
{
	struct vmod_directors_shard_param *p;
	struct vmod_priv *task;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(pa, VMOD_SHARD_SHARD_PARAM_MAGIC);
	assert(pa->scope > _SCOPE_INVALID);

	task = VRT_priv_task(ctx, id);

	if (task == NULL) {
		VRT_fail(ctx, "no priv_task");
		return (NULL);
	}

	if (task->priv) {
		CAST_OBJ_NOTNULL(p, task->priv, VMOD_SHARD_SHARD_PARAM_MAGIC);
		assert(p->scope == SCOPE_TASK);
		/* XXX
		VSL(SLT_Debug, 0,
		    "shard_param_task(id %p, pa %p) = %p (found, ws=%p)",
		    id, pa, p, ctx->ws);
		*/
		return (p);
	}

	p = WS_Alloc(ctx->ws, sizeof *p);
	if (p == NULL) {
		VRT_fail(ctx, "shard_param_task WS_Alloc failed");
		return (NULL);
	}
	task->priv = p;
	INIT_OBJ(p, VMOD_SHARD_SHARD_PARAM_MAGIC);
	p->vcl_name = pa->vcl_name;
	p->scope = SCOPE_TASK;

	if (id == pa || pa->scope != SCOPE_VCL)
		p->defaults = pa;
	else
		p->defaults = shard_param_task(ctx, pa, pa);

	/* XXX
	VSL(SLT_Debug, 0,
	    "shard_param_task(id %p, pa %p) = %p (new, defaults = %p, ws=%p)",
	    id, pa, p, p->defaults, ctx->ws);
	*/
	return (p);
}

static struct vmod_directors_shard_param *
shard_param_prep(VRT_CTX, struct vmod_directors_shard_param *p,
    const char *who)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(p, VMOD_SHARD_SHARD_PARAM_MAGIC);

	if (ctx->method & VCL_MET_TASK_C) {
		VRT_fail(ctx, "%s may only be used "
			 "in vcl_init and in backend context", who);
		return (NULL);
	} else if (ctx->method & VCL_MET_TASK_B)
		p = shard_param_task(ctx, p, p);
	else
		assert(ctx->method & VCL_MET_TASK_H);

	return (p);
}

VCL_VOID v_matchproto_(td_directors_shard_param_set)
vmod_shard_param_set(VRT_CTX, struct vmod_directors_shard_param *p,
		     struct vmod_shard_param_set_arg *a)
{
	uint32_t args = shard_param_set_mask(a);

	assert((args & ~_arg_mask_set) == 0);

	p = shard_param_prep(ctx, p, "shard_param.set()");
	if (p == NULL)
		return;
	(void) shard_param_args(ctx, p, "shard_param.set()", args,
				a->by, a->key, a->key_blob, a->alt, a->warmup,
				a->rampup, a->healthy);
}

VCL_VOID v_matchproto_(td_directors_shard_param_clear)
vmod_shard_param_clear(VRT_CTX,
    struct vmod_directors_shard_param *p)
{
	p = shard_param_prep(ctx, p, "shard_param.clear()");
	if (p == NULL)
		return;
	p->mask = 0;
}

static const struct vmod_directors_shard_param *
vmod_shard_param_read(VRT_CTX, const void *id,
    const struct vmod_directors_shard_param *p,
    struct vmod_directors_shard_param *pstk, const char *who)
{
	struct vmod_directors_shard_param *pp;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(p, VMOD_SHARD_SHARD_PARAM_MAGIC);
	(void) who; // XXX

	if (ctx->method == 0 || (ctx->method & VCL_MET_TASK_B))
		p = shard_param_task(ctx, id, p);

	if (p == NULL)
		return (NULL);

	pp = shard_param_stack(pstk, p, p->vcl_name);
	AN(pp);
	shard_param_merge(pp, p);
	return (pp);
}

VCL_STRING v_matchproto_(td_directors_shard_param_get_by)
vmod_shard_param_get_by(VRT_CTX,
    struct vmod_directors_shard_param *p)
{
	struct vmod_directors_shard_param pstk;
	const struct vmod_directors_shard_param *pp;

	pp = vmod_shard_param_read(ctx, p, p, &pstk, "shard_param.get_by()");
	if (pp == NULL)
		return (NULL);
	assert(pp->by > _BY_E_INVALID);
	return (by_str[pp->by]);
}

VCL_INT v_matchproto_(td_directors_shard_param_get_key)
vmod_shard_param_get_key(VRT_CTX,
    struct vmod_directors_shard_param *p)
{
	struct vmod_directors_shard_param pstk;
	const struct vmod_directors_shard_param *pp;

	pp = vmod_shard_param_read(ctx, p, p, &pstk, "shard_param.get_key()");
	if (pp == NULL)
		return (-1);
	return ((VCL_INT)shard_get_key(ctx, pp));
}
VCL_INT v_matchproto_(td_directors_shard_param_get_alt)
vmod_shard_param_get_alt(VRT_CTX,
    struct vmod_directors_shard_param *p)
{
	struct vmod_directors_shard_param pstk;
	const struct vmod_directors_shard_param *pp;

	pp = vmod_shard_param_read(ctx, p, p, &pstk,
				   "shard_param.get_alt()");
	if (pp == NULL)
		return (-1);
	return (pp->alt);
}

VCL_REAL v_matchproto_(td_directors_shard_param_get_warmup)
vmod_shard_param_get_warmup(VRT_CTX,
    struct vmod_directors_shard_param *p)
{
	struct vmod_directors_shard_param pstk;
	const struct vmod_directors_shard_param *pp;

	pp = vmod_shard_param_read(ctx, p, p, &pstk,
				   "shard_param.get_warmup()");
	if (pp == NULL)
		return (-2);
	return (pp->warmup);
}

VCL_BOOL v_matchproto_(td_directors_shard_param_get_rampup)
vmod_shard_param_get_rampup(VRT_CTX,
    struct vmod_directors_shard_param *p)
{
	struct vmod_directors_shard_param pstk;
	const struct vmod_directors_shard_param *pp;

	pp = vmod_shard_param_read(ctx, p, p, &pstk,
				   "shard_param.get_rampup()");
	if (pp == NULL)
		return (0);
	return (pp->rampup);
}

VCL_STRING v_matchproto_(td_directors_shard_param_get_healthy)
vmod_shard_param_get_healthy(VRT_CTX,
    struct vmod_directors_shard_param *p)
{
	struct vmod_directors_shard_param pstk;
	const struct vmod_directors_shard_param *pp;

	pp = vmod_shard_param_read(ctx, p, p, &pstk,
				   "shard_param.get_healthy()");
	if (pp == NULL)
		return (NULL);
	assert(pp->healthy > _HEALTHY_E_INVALID);
	return (healthy_str[pp->healthy]);

}

static const struct vmod_directors_shard_param *
shard_param_blob(const VCL_BLOB blob)
{
	if (blob && blob->priv &&
	    blob->len == sizeof(struct vmod_directors_shard_param) &&
	    *(unsigned *)blob->priv == VMOD_SHARD_SHARD_PARAM_MAGIC)
		return (blob->priv);
	return (NULL);
}

VCL_BLOB v_matchproto_(td_directors_shard_param_use)
vmod_shard_param_use(VRT_CTX,
    struct vmod_directors_shard_param *p)
{
	struct vmod_priv *blob;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(p, VMOD_SHARD_SHARD_PARAM_MAGIC);

	blob = (void *)WS_Alloc(ctx->ws, sizeof *blob);
	if (blob == NULL) {
		VRT_fail(ctx, "Workspace overflow (param.use())");
		return (NULL);
	}

	memset(blob, 0, sizeof *blob);
	blob->len = sizeof *p;
	blob->priv = p;

	return (blob);
}
