/*-
 * Copyright 2009-2018 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Authors: Julian Wiesener <jw@uplex.de>
 *	    Nils Goroll <slink@uplex.de>
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
#include "vcl.h"

#include "vend.h"

#include "vcc_directors_if.h"
#include "vmod_directors_shard_dir.h"
#include "vmod_directors_shard_cfg.h"
#include "vsb.h"

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
 *  they are used, which enables changing them independently (ie, shard
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
#define arg_mask_	((arg_resolve << 1) - 1)
/* allowed in shard_param.set */
#define arg_mask_set_	(arg_param - 1)
/* allowed in shard_param */
#define arg_mask_param_ ( arg_mask_set_		\
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

#define VMOD_SHARD_SHARD_PARAM_BLOB		0xdf5ca116

struct vmod_directors_shard_param {
	unsigned				magic;
#define VMOD_SHARD_SHARD_PARAM_MAGIC		0xdf5ca117

	/* internals */
	uint32_t				key;
	const char				*vcl_name;
	const struct vmod_directors_shard_param *defaults;
	enum vmod_directors_shard_param_scope	scope;

	/* parameters */
	VCL_ENUM				by;
	VCL_ENUM				healthy;
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

	.mask		= arg_mask_param_,
	.rampup	= 1,
	.alt		= 0,
	.warmup		= -1,
};

#define default_by(ptr) (ptr == NULL ? VENUM(HASH) : ptr)
#define default_healthy(ptr) (ptr == NULL ? VENUM(CHOSEN) : ptr)

static struct vmod_directors_shard_param *
shard_param_stack(struct vmod_directors_shard_param *p,
    const struct vmod_directors_shard_param *pa, const char *who);

static const struct vmod_directors_shard_param *
shard_param_task_r(VRT_CTX, const void *id, const char *who,
    const struct vmod_directors_shard_param *pa);

static struct vmod_directors_shard_param *
shard_param_task_l(VRT_CTX, const void *id, const char *who,
    const struct vmod_directors_shard_param *pa);

static const struct vmod_directors_shard_param *
shard_param_blob(VCL_BLOB blob);

static const struct vmod_directors_shard_param *
vmod_shard_param_read(VRT_CTX, const void *id, const char *who,
    const struct vmod_directors_shard_param *p,
    struct vmod_directors_shard_param *pstk);

// XXX #3329 #3330 revisit - for now, treat pipe like backend
#define SHARD_VCL_TASK_REQ (VCL_MET_TASK_C & ~VCL_MET_PIPE)
#define SHARD_VCL_TASK_BEREQ (VCL_MET_TASK_B | VCL_MET_PIPE)
/* -------------------------------------------------------------------------
 * shard vmod interface
 */
static vdi_healthy_f vmod_shard_healthy;
static vdi_resolve_f vmod_shard_resolve;
static vdi_list_f vmod_shard_list;

struct vmod_directors_shard {
	unsigned				magic;
#define VMOD_SHARD_SHARD_MAGIC			0x6e63e1bf
	struct sharddir				*shardd;
	VCL_BACKEND				dir;
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

static void v_matchproto_(vdi_release_f)
vmod_shard_release(VCL_BACKEND dir)
{
	struct sharddir *shardd;

	CAST_OBJ_NOTNULL(shardd, dir->priv, SHARDDIR_MAGIC);
	sharddir_release(shardd);
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
	.release =	vmod_shard_release,
	.destroy =	vmod_shard_destroy,
	.list =		vmod_shard_list
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
vmod_shard_key(VRT_CTX, struct vmod_directors_shard *vshard, VCL_STRANDS s)
{

	(void)ctx;
	(void)vshard;

	return ((VCL_INT)VRT_HashStrands32(s));
}

VCL_VOID v_matchproto_(td_directors_set_warmup)
vmod_shard_set_warmup(VRT_CTX, struct vmod_directors_shard *vshard,
    VCL_REAL probability)
{
	CHECK_OBJ_NOTNULL(vshard, VMOD_SHARD_SHARD_MAGIC);
	if (probability < 0 || probability >= 1) {
		shard_notice(ctx->vsl, vshard->shardd->name,
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
		shard_fail(ctx, vshard->shardd->name, "%s",
		    "shard .associate param invalid");
		return;
	}

	sharddir_set_param(vshard->shardd, ppt);
}

VCL_BOOL v_matchproto_(td_directors_shard_add_backend)
vmod_shard_add_backend(VRT_CTX, struct vmod_directors_shard *vshard,
    struct VARGS(shard_add_backend) *args)
{
	VCL_REAL weight = 1;

	CHECK_OBJ_NOTNULL(vshard, VMOD_SHARD_SHARD_MAGIC);

	if (args->backend == NULL) {
		shard_fail(ctx, vshard->shardd->name, "%s",
		    "None backend cannot be added");
		return (0);
	}

	if (args->valid_weight) {
		if (args->weight >= 1)
			weight = args->weight;
		else
			shard_notice(ctx->vsl, vshard->shardd->name,
			    ".add_backend(weight=%f) ignored", args->weight);
	}

	return (shardcfg_add_backend(ctx, vshard->shardd, args->backend,
	    args->valid_ident ? args->ident : NULL,
	    args->valid_rampup ? args->rampup : nan(""),
	    weight));
}

VCL_BOOL v_matchproto_(td_directors_shard_remove_backend)
vmod_shard_remove_backend(VRT_CTX, struct vmod_directors_shard *vshard,
    struct VARGS(shard_remove_backend) *args)
{
	VCL_BACKEND be = args->valid_backend ? args->backend : NULL;
	VCL_STRING ident = args->valid_ident ? args->ident : NULL;

	CHECK_OBJ_NOTNULL(vshard, VMOD_SHARD_SHARD_MAGIC);

	if (be == NULL && ident == NULL) {
		shard_fail(ctx, vshard->shardd->name, "%s",
		    ".remove_backend(): either backend or ident are required");
		return (0);
	}

	return (shardcfg_remove_backend(ctx, vshard->shardd, be, ident));
}

VCL_BOOL v_matchproto_(td_directors_shard_clear)
vmod_shard_clear(VRT_CTX, struct vmod_directors_shard *vshard)
{
	CHECK_OBJ_NOTNULL(vshard, VMOD_SHARD_SHARD_MAGIC);
	return (shardcfg_clear(ctx, vshard->shardd));
}

VCL_BOOL v_matchproto_(td_directors_shard_reconfigure)
vmod_shard_reconfigure(VRT_CTX, struct vmod_directors_shard *vshard,
    VCL_INT replicas)
{
	return (shardcfg_reconfigure(ctx, vshard->shardd, replicas));
}

static inline uint32_t
shard_get_key(VRT_CTX, const struct vmod_directors_shard_param *p)
{
	struct http *http;
	VCL_ENUM by = default_by(p->by);

	if (by == VENUM(KEY) || by == VENUM(BLOB))
		return (p->key);
	if (by == VENUM(HASH) && ctx->bo != NULL) {
		CHECK_OBJ(ctx->bo, BUSYOBJ_MAGIC);
		return (vbe32dec(ctx->bo->digest));
	}
	if (by == VENUM(HASH) || by == VENUM(URL)) {
		if (ctx->http_req) {
			AN(http = ctx->http_req);
		} else {
			AN(ctx->http_bereq);
			AN(http = ctx->http_bereq);
		}
		return (VRT_HashStrands32(TOSTRAND(http->hd[HTTP_HDR_URL].b)));
	}
	WRONG("by enum");
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
	assert((to->mask & ~arg_mask_param_) == 0);

	if (to->mask == arg_mask_param_)
		return;

	CHECK_OBJ_NOTNULL(from, VMOD_SHARD_SHARD_PARAM_MAGIC);
	assert((from->mask & ~arg_mask_param_) == 0);

	if ((to->mask & arg_by) == 0 && (from->mask & arg_by) != 0) {
		to->by = from->by;
		if (from->by == VENUM(KEY) || from->by == VENUM(BLOB))
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

	if (to->mask == arg_mask_param_)
		return;

	AN(from->defaults);
	shard_param_merge(to, from->defaults);
}

static uint32_t
shard_blob_key(VCL_BLOB key_blob)
{
	uint8_t k[4] = { 0 };
	const uint8_t *b;
	size_t i, ki;

	AN(key_blob);
	AN(key_blob->blob);
	assert(key_blob->len > 0);

	if (key_blob->len >= 4)
		ki = 0;
	else
		ki = 4 - key_blob->len;

	b = key_blob->blob;
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
shard_backendarg_mask_(const struct VARGS(shard_backend) * const a)
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
shard_param_set_mask(const struct VARGS(shard_param_set) * const a)
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
    struct vmod_directors_shard_param *p, const char *func,
    uint32_t args, VCL_ENUM by_s, VCL_INT key_int, VCL_BLOB key_blob,
    VCL_INT alt, VCL_REAL warmup, VCL_BOOL rampup, VCL_ENUM healthy_s)
{

	CHECK_OBJ_NOTNULL(p, VMOD_SHARD_SHARD_PARAM_MAGIC);
	AN(p->vcl_name);

	assert((args & ~arg_mask_set_) == 0);

	if (!(args & arg_by))
		by_s = NULL;
	by_s = default_by(by_s);

	/* by_s / key_int / key_blob */
	if (by_s == VENUM(KEY)) {
		if ((args & arg_key) == 0) {
			shard_fail(ctx, p->vcl_name,
			    "%s missing key argument with by=%s",
			    func, by_s);
			return (NULL);
		}
		if (key_int < 0 || key_int > UINT32_MAX) {
			shard_fail(ctx, p->vcl_name,
			    "%s invalid key argument %jd with by=%s",
			    func, (intmax_t)key_int, by_s);
			return (NULL);
		}
		assert(key_int >= 0);
		assert(key_int <= UINT32_MAX);
		p->key = (uint32_t)key_int;
	} else if (by_s == VENUM(BLOB)) {
		if ((args & arg_key_blob) == 0) {
			shard_fail(ctx, p->vcl_name,
			    "%s missing key_blob argument with by=%s",
			    func, by_s);
			return (NULL);
		}
		if (key_blob == NULL || key_blob->len == 0 ||
		    key_blob->blob == NULL) {
			shard_err(ctx->vsl, p->vcl_name,
			    "%s by=BLOB but no or empty key_blob - using key 0",
			    func);
			p->key = 0;
		} else
			p->key = shard_blob_key(key_blob);
	} else if (by_s == VENUM(HASH) || by_s == VENUM(URL)) {
		if (args & (arg_key|arg_key_blob)) {
			shard_fail(ctx, p->vcl_name,
			    "%s key and key_blob arguments are "
			    "invalid with by=%s", func, by_s);
			return (NULL);
		}
	} else {
		WRONG("by enum");
	}
	p->by = by_s;

	if (args & arg_alt) {
		if (alt < 0) {
			shard_fail(ctx, p->vcl_name,
			    "%s invalid alt argument %jd",
			    func, (intmax_t)alt);
			return (NULL);
		}
		p->alt = alt;
	}

	if (args & arg_warmup) {
		if ((warmup < 0 && warmup != -1) || warmup > 1) {
			shard_fail(ctx, p->vcl_name,
			    "%s invalid warmup argument %f",
			    func, warmup);
			return (NULL);
		}
		p->warmup = warmup;
	}

	if (args & arg_rampup)
		p->rampup = !!rampup;

	if (args & arg_healthy)
		p->healthy = healthy_s;

	p->mask = args & arg_mask_param_;
	return (p);
}

VCL_BACKEND v_matchproto_(td_directors_shard_backend)
vmod_shard_backend(VRT_CTX, struct vmod_directors_shard *vshard,
		   struct VARGS(shard_backend) *a)
{
	struct sharddir *shardd;
	struct vmod_directors_shard_param pstk;
	struct vmod_directors_shard_param *pp = NULL;
	const struct vmod_directors_shard_param *ppt;
	VCL_ENUM resolve;
	uint32_t args = shard_backendarg_mask_(a);

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vshard, VMOD_SHARD_SHARD_MAGIC);
	shardd = vshard->shardd;
	CHECK_OBJ_NOTNULL(shardd, SHARDDIR_MAGIC);
	assert((args & ~arg_mask_) == 0);

	if (args & arg_resolve)
		resolve = a->resolve;
	else if (ctx->method & VCL_MET_TASK_H)
		resolve = VENUM(LAZY);
	else
		resolve = VENUM(NOW);

	if (resolve == VENUM(LAZY)) {
		if ((args & ~arg_resolve) == 0) {
			AN(vshard->dir);
			return (vshard->dir);
		}

		if ((ctx->method & SHARD_VCL_TASK_BEREQ) == 0) {
			shard_fail(ctx, shardd->name, "%s",
			    ".backend(resolve=LAZY) with other "
			    "parameters can only be used in backend/pipe "
			    "context");
			return (NULL);
		}

		pp = shard_param_task_l(ctx, shardd, shardd->name,
		    shardd->param);
		if (pp == NULL)
			return (NULL);
	} else if (resolve == VENUM(NOW)) {
		if (ctx->method & VCL_MET_TASK_H) {
			shard_fail(ctx, shardd->name, "%s",
			    ".backend(resolve=NOW) cannot be "
			    "used in vcl_init{}/vcl_fini{}");
			return (NULL);
		}
		ppt = shard_param_task_r(ctx, shardd, shardd->name,
		    shardd->param);
		AN(ppt);
		pp = shard_param_stack(&pstk, ppt, shardd->name);
	} else {
		WRONG("resolve enum");
	}

	AN(pp);

	if (args & arg_param) {
		ppt = shard_param_blob(a->param);
		if (ppt == NULL) {
			shard_fail(ctx, shardd->name, "%s",
			    ".backend(key_blob) param invalid");
			return (NULL);
		}
		pp->defaults = ppt;
	}

	pp = shard_param_args(ctx, pp, "shard.backend()",
			      args & arg_mask_set_,
			      a->by, a->key, a->key_blob, a->alt, a->warmup,
			      a->rampup, a->healthy);
	if (pp == NULL)
		return (NULL);

	if (resolve == VENUM(LAZY))
		return (vshard->dir);

	assert(resolve == VENUM(NOW));
	shard_param_merge(pp, pp->defaults);
	return (sharddir_pick_be(ctx, shardd, shard_get_key(ctx, pp),
	    pp->alt, pp->warmup, pp->rampup, pp->healthy));
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

	pp = vmod_shard_param_read(ctx, shardd, shardd->name,
	    shardd->param, pstk);
	CHECK_OBJ_NOTNULL(pp, VMOD_SHARD_SHARD_PARAM_MAGIC);

	return (sharddir_pick_be(ctx, shardd,
				 shard_get_key(ctx, pp), pp->alt, pp->warmup,
				 pp->rampup, pp->healthy));
}

static void v_matchproto_(vdi_list_f)
vmod_shard_list(VRT_CTX, VCL_BACKEND dir, struct vsb *vsb, int pflag, int jflag)
{
	struct sharddir *shardd;
	struct shard_backend *sbe;
	VCL_TIME c, changed = 0;
	VCL_DURATION rampup_d, d;
	VCL_BACKEND be;
	VCL_BOOL h;
	unsigned i, nh = 0;
	double rampup_p;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(shardd, dir->priv, SHARDDIR_MAGIC);

	if (pflag) {
		if (jflag) {
			VSB_cat(vsb, "{\n");
			VSB_indent(vsb, 2);
			VSB_printf(vsb, "\"warmup\": %f,\n", shardd->warmup);
			VSB_printf(vsb, "\"rampup_duration\": %f,\n",
			    shardd->rampup_duration);
			VSB_cat(vsb, "\"backends\": {\n");
			VSB_indent(vsb, 2);
		} else {
			VSB_cat(vsb, "\n\n\tBackend\tIdent\tHealth\t"
			    "Rampup  Remaining\n");
		}
	}

	sharddir_rdlock(shardd);
	for (i = 0; i < shardd->n_backend; i++) {
		sbe = &shardd->backend[i];
		AN(sbe);
		be = sbe->backend;
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);

		c = 0;
		h = VRT_Healthy(ctx, be, &c);
		if (h)
			nh++;
		if (c > changed)
			changed = c;
		if ((pflag) == 0)
			continue;

		d = ctx->now - c;
		rampup_d = shardcfg_get_rampup(shardd, i);
		if (! h) {
			rampup_p = 0.0;
			rampup_d = 0.0;
		} else if (d < rampup_d) {
			rampup_p = d / rampup_d;
			rampup_d -= d;
		} else {
			rampup_p = 1.0;
			rampup_d = 0.0;
		}

		if (jflag) {
			if (i)
				VSB_cat(vsb, ",\n");
			VSB_printf(vsb, "\"%s\": {\n",
			    be->vcl_name);
			VSB_indent(vsb, 2);
			VSB_printf(vsb, "\"ident\": \"%s\",\n",
			    sbe->ident ? sbe->ident : be->vcl_name);
			VSB_printf(vsb, "\"health\": \"%s\",\n",
			    h ? "healthy" : "sick");
			VSB_printf(vsb, "\"rampup\": %f,\n", rampup_p);
			VSB_printf(vsb, "\"rampup_remaining\": %.3f\n",
			    rampup_d);
			VSB_indent(vsb, -2);
			VSB_cat(vsb, "}");
		} else {
			VSB_printf(vsb, "\t%s\t%s\t%s\t%6.2f%% %8.3fs\n",
			    be->vcl_name,
			    sbe->ident ? sbe->ident : be->vcl_name,
			    h ? "healthy" : "sick",
			    rampup_p * 100, rampup_d);
		}
	}
	sharddir_unlock(shardd);

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
		VSB_printf(vsb, "[%u, %u, \"%s\"]", nh, i,
		    nh ? "healthy" : "sick");
	else
		VSB_printf(vsb, "%u/%u\t%s", nh, i, nh ? "healthy" : "sick");
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

static const struct vmod_directors_shard_param *
shard_param_task_r(VRT_CTX, const void *id, const char *who,
   const struct vmod_directors_shard_param *pa)
{
	const struct vmod_directors_shard_param *p;
	const struct vmod_priv *task;
	const void *task_id;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(pa, VMOD_SHARD_SHARD_PARAM_MAGIC);
	assert(pa->scope > _SCOPE_INVALID);

	task_id = (const char *)id + task_off_param;
	task = VRT_priv_task_get(ctx, task_id);

	if (task) {
		CAST_OBJ_NOTNULL(p, task->priv, VMOD_SHARD_SHARD_PARAM_MAGIC);
		assert(p->scope == SCOPE_TASK);
		assert(who == p->vcl_name);
		return (p);
	}

	if (id == pa || pa->scope != SCOPE_VCL)
		return (pa);

	return (shard_param_task_r(ctx, pa, pa->vcl_name, pa));
}

/*
 * get a task scoped param struct for id defaulting to pa
 * if id != pa and pa has VCL scope, also get a task scoped param struct for pa
 */
static struct vmod_directors_shard_param *
shard_param_task_l(VRT_CTX, const void *id, const char *who,
   const struct vmod_directors_shard_param *pa)
{
	struct vmod_directors_shard_param *p;
	struct vmod_priv *task;
	const void *task_id;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(pa, VMOD_SHARD_SHARD_PARAM_MAGIC);
	assert(pa->scope > _SCOPE_INVALID);

	task_id = (const char *)id + task_off_param;
	task = VRT_priv_task(ctx, task_id);

	if (task == NULL) {
		shard_fail(ctx, who, "%s", "no priv_task");
		return (NULL);
	}

	if (task->priv) {
		CAST_OBJ_NOTNULL(p, task->priv, VMOD_SHARD_SHARD_PARAM_MAGIC);
		assert(p->scope == SCOPE_TASK);
		assert(who == p->vcl_name);
		return (p);
	}

	WS_TASK_ALLOC_OBJ(ctx, p, VMOD_SHARD_SHARD_PARAM_MAGIC);
	if (p == NULL)
		return (NULL);
	task->priv = p;
	p->vcl_name = who;
	p->scope = SCOPE_TASK;

	if (id == pa || pa->scope != SCOPE_VCL)
		p->defaults = pa;
	else
		p->defaults = shard_param_task_l(ctx, pa, pa->vcl_name, pa);

	if (p->defaults == NULL)
		return (NULL);

	return (p);
}

static struct vmod_directors_shard_param *
shard_param_prep(VRT_CTX, struct vmod_directors_shard_param *p,
    const char *who)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(p, VMOD_SHARD_SHARD_PARAM_MAGIC);

	if (ctx->method & SHARD_VCL_TASK_REQ) {
		shard_fail(ctx, p->vcl_name, "%s may only be used "
		    "in vcl_init and in backend/pipe context", who);
		return (NULL);
	} else if (ctx->method & SHARD_VCL_TASK_BEREQ)
		p = shard_param_task_l(ctx, p, p->vcl_name, p);
	else
		assert(ctx->method & VCL_MET_TASK_H);

	return (p);
}

VCL_VOID v_matchproto_(td_directors_shard_param_set)
vmod_shard_param_set(VRT_CTX, struct vmod_directors_shard_param *p,
		     struct VARGS(shard_param_set) *a)
{
	uint32_t args = shard_param_set_mask(a);

	assert((args & ~arg_mask_set_) == 0);

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
vmod_shard_param_read(VRT_CTX, const void *id, const char *who,
    const struct vmod_directors_shard_param *p,
    struct vmod_directors_shard_param *pstk)
{
	struct vmod_directors_shard_param *pp;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(p, VMOD_SHARD_SHARD_PARAM_MAGIC);

	if (ctx->method == 0 || (ctx->method & SHARD_VCL_TASK_BEREQ))
		p = shard_param_task_r(ctx, id, who, p);

	CHECK_OBJ_NOTNULL(p, VMOD_SHARD_SHARD_PARAM_MAGIC);
	pp = shard_param_stack(pstk, p, p->vcl_name);
	shard_param_merge(pp, p);
	return (pp);
}

VCL_STRING v_matchproto_(td_directors_shard_param_get_by)
vmod_shard_param_get_by(VRT_CTX,
    struct vmod_directors_shard_param *p)
{
	struct vmod_directors_shard_param pstk;
	const struct vmod_directors_shard_param *pp;

	pp = vmod_shard_param_read(ctx, p, p->vcl_name, p, &pstk);
	CHECK_OBJ_NOTNULL(pp, VMOD_SHARD_SHARD_PARAM_MAGIC);
	return (default_by(pp->by));
}

VCL_INT v_matchproto_(td_directors_shard_param_get_key)
vmod_shard_param_get_key(VRT_CTX,
    struct vmod_directors_shard_param *p)
{
	struct vmod_directors_shard_param pstk;
	const struct vmod_directors_shard_param *pp;

	pp = vmod_shard_param_read(ctx, p, p->vcl_name, p, &pstk);
	CHECK_OBJ_NOTNULL(pp, VMOD_SHARD_SHARD_PARAM_MAGIC);
	return ((VCL_INT)shard_get_key(ctx, pp));
}
VCL_INT v_matchproto_(td_directors_shard_param_get_alt)
vmod_shard_param_get_alt(VRT_CTX,
    struct vmod_directors_shard_param *p)
{
	struct vmod_directors_shard_param pstk;
	const struct vmod_directors_shard_param *pp;

	pp = vmod_shard_param_read(ctx, p, p->vcl_name, p, &pstk);
	CHECK_OBJ_NOTNULL(pp, VMOD_SHARD_SHARD_PARAM_MAGIC);
	return (pp->alt);
}

VCL_REAL v_matchproto_(td_directors_shard_param_get_warmup)
vmod_shard_param_get_warmup(VRT_CTX,
    struct vmod_directors_shard_param *p)
{
	struct vmod_directors_shard_param pstk;
	const struct vmod_directors_shard_param *pp;

	pp = vmod_shard_param_read(ctx, p, p->vcl_name, p, &pstk);
	CHECK_OBJ_NOTNULL(pp, VMOD_SHARD_SHARD_PARAM_MAGIC);
	return (pp->warmup);
}

VCL_BOOL v_matchproto_(td_directors_shard_param_get_rampup)
vmod_shard_param_get_rampup(VRT_CTX,
    struct vmod_directors_shard_param *p)
{
	struct vmod_directors_shard_param pstk;
	const struct vmod_directors_shard_param *pp;

	pp = vmod_shard_param_read(ctx, p, p->vcl_name, p, &pstk);
	CHECK_OBJ_NOTNULL(pp, VMOD_SHARD_SHARD_PARAM_MAGIC);
	return (pp->rampup);
}

VCL_STRING v_matchproto_(td_directors_shard_param_get_healthy)
vmod_shard_param_get_healthy(VRT_CTX,
    struct vmod_directors_shard_param *p)
{
	struct vmod_directors_shard_param pstk;
	const struct vmod_directors_shard_param *pp;

	pp = vmod_shard_param_read(ctx, p, p->vcl_name, p, &pstk);
	CHECK_OBJ_NOTNULL(pp, VMOD_SHARD_SHARD_PARAM_MAGIC);
	return (default_healthy(pp->healthy));
}

static const struct vmod_directors_shard_param *
shard_param_blob(VCL_BLOB blob)
{
	const struct vmod_directors_shard_param *p;

	if (blob && blob->type == VMOD_SHARD_SHARD_PARAM_BLOB &&
	    blob->blob != NULL &&
	    blob->len == sizeof(struct vmod_directors_shard_param)) {
		CAST_OBJ_NOTNULL(p, blob->blob, VMOD_SHARD_SHARD_PARAM_MAGIC);
		return (p);
	}

	return (NULL);
}

VCL_BLOB v_matchproto_(td_directors_shard_param_use)
vmod_shard_param_use(VRT_CTX,
    struct vmod_directors_shard_param *p)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(p, VMOD_SHARD_SHARD_PARAM_MAGIC);

	return (VRT_blob(ctx, "xshard_param.use()", p, sizeof *p,
	    VMOD_SHARD_SHARD_PARAM_BLOB));
}
