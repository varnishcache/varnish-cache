/*-
 * Copyright (c) 2013-2021 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@FreeBSD.org>
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
#include "vsb.h"

#include "vcc_debug_if.h"

// vmod_debug.c
extern void mylog(struct vsl_log *vsl, enum VSL_tag_e tag,
    const char *fmt, ...) v_printflike_(3,4);

struct xyzzy_debug_obj {
	unsigned		magic;
#define VMOD_DEBUG_OBJ_MAGIC	0xccbd9b77
	int			foobar;
	const char		*string;
	const char		*number;
	VCL_STRING		vcl_name;
};

VCL_VOID
xyzzy_obj__init(VRT_CTX, struct xyzzy_debug_obj **op,
    const char *vcl_name, VCL_STRING s, VCL_ENUM e)
{
	struct xyzzy_debug_obj *o;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(op);
	AZ(*op);

	if (! strcmp(s, "fail")) {
		VRT_fail(ctx, "failing as requested");
	}
	ALLOC_OBJ(o, VMOD_DEBUG_OBJ_MAGIC);
	AN(o);
	*op = o;
	o->foobar = 42;
	o->string = s;
	o->number = e;
	o->vcl_name = vcl_name;
	AN(*op);
}

VCL_VOID v_matchproto_(td_xyzzy_obj__fini)
xyzzy_obj__fini(struct xyzzy_debug_obj **op)
{
	struct xyzzy_debug_obj *o;

	TAKE_OBJ_NOTNULL(o, op, VMOD_DEBUG_OBJ_MAGIC);
	FREE_OBJ(o);
}

VCL_INT v_matchproto_(td_xyzzy_objcli)
xyzzy_objcli(VRT_CTX, struct xyzzy_debug_obj *o, VCL_STRANDS s)
{
	int i;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(o, VMOD_DEBUG_OBJ_MAGIC);

	if (s->n > 0 && ! strcmp(s->p[0], "fail")) {
		VSB_cat(ctx->msg, "You asked me to fail");
		return (300);
	}
	VSB_cat(ctx->msg, o->vcl_name);
	VSB_putc(ctx->msg, ':');

	for (i = 0; i < s->n; i++) {
		VSB_putc(ctx->msg, ' ');
		VSB_cat(ctx->msg, s->p[i]);
	}

	return (200);
}

VCL_VOID v_matchproto_(td_xyzzy_obj_enum)
xyzzy_obj_enum(VRT_CTX, struct xyzzy_debug_obj *o, VCL_ENUM e)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(o, VMOD_DEBUG_OBJ_MAGIC);
	assert(!strcmp(e, "martin"));
}

VCL_VOID v_matchproto_(td_xyzzy_obj_enum)
xyzzy_obj_obj(VRT_CTX, struct xyzzy_debug_obj *o)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(o, VMOD_DEBUG_OBJ_MAGIC);
}

VCL_STRING v_matchproto_()
xyzzy_obj_foo(VRT_CTX, struct xyzzy_debug_obj *o, VCL_STRING s)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	(void)s;
	CHECK_OBJ_NOTNULL(o, VMOD_DEBUG_OBJ_MAGIC);
	assert(o->foobar == 42);
	return ("BOO");
}

VCL_TIME v_matchproto_()
xyzzy_obj_date(VRT_CTX, struct xyzzy_debug_obj *o)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(o, VMOD_DEBUG_OBJ_MAGIC);
	assert(o->foobar == 42);
	return (21.4);
}

VCL_STRING v_matchproto_()
xyzzy_obj_string(VRT_CTX, struct xyzzy_debug_obj *o)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(o, VMOD_DEBUG_OBJ_MAGIC);
	assert(o->foobar == 42);
	return (o->string);
}

VCL_STRING v_matchproto_()
xyzzy_obj_number(VRT_CTX, struct xyzzy_debug_obj *o)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(o, VMOD_DEBUG_OBJ_MAGIC);
	assert(o->foobar == 42);
	return (o->number);
}

VCL_VOID v_matchproto_()
xyzzy_obj_test_priv_call(VRT_CTX,
    struct xyzzy_debug_obj *o, struct vmod_priv *priv)
{
	(void)o;
	xyzzy_test_priv_call(ctx, priv);
}

VCL_VOID v_matchproto_()
xyzzy_obj_test_priv_vcl(VRT_CTX,
    struct xyzzy_debug_obj *o, struct vmod_priv *priv)
{
	(void)o;
	xyzzy_test_priv_vcl(ctx, priv);
}

#define PRIV_FINI(name)						\
static void v_matchproto_(vmod_priv_fini_f)				\
obj_priv_ ## name ## _fini(VRT_CTX, void *ptr)				\
{									\
	const char * const fmt = "obj_priv_" #name "_fini(%p)";		\
									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	AN(ptr);							\
	mylog(ctx->vsl, SLT_Debug, fmt, ptr);				\
}									\
									\
static const struct vmod_priv_methods					\
xyzzy_obj_test_priv_ ## name ## _methods[1] = {{			\
		.magic = VMOD_PRIV_METHODS_MAGIC,			\
		.type = "debug_obj_test_priv_" #name,			\
		.fini = obj_priv_ ## name ## _fini			\
	}};
PRIV_FINI(task)
PRIV_FINI(top)
#undef PRIV_FINI

VCL_STRING v_matchproto_()
xyzzy_obj_test_priv_task(VRT_CTX, struct xyzzy_debug_obj *o, VCL_STRING s)
{
	struct vmod_priv *p;
	struct vsl_log *vsl;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (ctx->method & VCL_MET_TASK_H)
		vsl = NULL;
	else
		vsl = ctx->vsl;

	if (s == NULL || *s == '\0') {
		p = VRT_priv_task_get(ctx, o);
		if (p == NULL) {
			mylog(vsl, SLT_Debug, "%s.priv_task() = NULL",
			    o->vcl_name);
			return ("");
		}
		assert(p->methods == xyzzy_obj_test_priv_task_methods);
		mylog(vsl, SLT_Debug,
		    "%s.priv_task() = %p .priv = %p (\"%s\")",
		    o->vcl_name, p, p->priv, (char *)p->priv);
		return (p->priv);
	}

	p = VRT_priv_task(ctx, o);

	if (p == NULL) {
		mylog(vsl, SLT_Debug, "%s.priv_task() = NULL [err]",
		    o->vcl_name);
		VRT_fail(ctx, "no priv task - out of ws?");
		return ("");
	}

	mylog(vsl, SLT_Debug,
	    "%s.priv_task() = %p .priv = %p (\"%s\") [%s]",
	    o->vcl_name, p, s, s, p->priv ? "update" : "new");

	if (p->priv == NULL)
		p->methods = xyzzy_obj_test_priv_task_methods;
	else
		assert(p->methods == xyzzy_obj_test_priv_task_methods);

	/* minimum scope of s and priv is the task - no need to copy */
	p->priv = TRUST_ME(s);

	return (p->priv);
}

VCL_STRING v_matchproto_()
xyzzy_obj_test_priv_top(VRT_CTX, struct xyzzy_debug_obj *o, VCL_STRING s)
{
	struct vmod_priv *p;
	struct req *req;
	struct ws *ws;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	req = ctx->req;
	if (req == NULL) {
		VRT_fail(ctx, "%s.priv_top() can only be used "
		    "in client VCL context", o->vcl_name);
		return ("");
	}
	CHECK_OBJ(req, REQ_MAGIC);

	if (s == NULL || *s == '\0') {
		p = VRT_priv_top_get(ctx, o);
		if (p == NULL) {
			VSLb(ctx->vsl, SLT_Debug, "%s.priv_top() = NULL",
			    o->vcl_name);
			return ("");
		}
		assert(p->methods == xyzzy_obj_test_priv_top_methods);
		VSLb(ctx->vsl, SLT_Debug,
		    "%s.priv_top() = %p .priv = %p (\"%s\")",
		     o->vcl_name, p, p->priv, (char *)p->priv);
		return (p->priv);
	}

	p = VRT_priv_top(ctx, o);
	if (p == NULL)
		VSLb(ctx->vsl, SLT_Debug, "%s.priv_top() = NULL [err]",
		    o->vcl_name);

	CHECK_OBJ_NOTNULL(req->top, REQTOP_MAGIC);
	req = req->top->topreq;
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	ws = req->ws;

	/* copy to top req's workspace if need to */
	if (ctx->ws != ws && WS_Allocated(ctx->ws, s, -1))
		s = WS_Copy(ws, s, -1);

	if (p == NULL || s == NULL) {
		VRT_fail(ctx, "out of ws?");
		return ("");
	}

	VSLb(ctx->vsl, SLT_Debug,
	    "%s.priv_top() = %p .priv = %p (\"%s\") [%s]",
	    o->vcl_name, p, s, s, p->priv ? "update" : "new");

	if (p->priv == NULL)
		p->methods = xyzzy_obj_test_priv_top_methods;
	else
		assert(p->methods == xyzzy_obj_test_priv_top_methods);

	p->priv = TRUST_ME(s);

	return (p->priv);
}

/* ----------------------------------------------------------------------------
 * obj_opt (optional arguments and privs)
 */
struct xyzzy_debug_obj_opt {
	unsigned			magic;
#define VMOD_DEBUG_OBJ_OPT_MAGIC	0xccbd9b78
	char				*name;
	struct VARGS(obj_opt_meth_opt)	args;
	void				*freeptr;
};

VCL_VOID v_matchproto_()
xyzzy_obj_opt__init(VRT_CTX,
    struct xyzzy_debug_obj_opt **op, const char *vcl_name,
    struct VARGS(obj_opt__init) *args)
{
	struct xyzzy_debug_obj_opt *o;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(args);

	AN(args->arg1); // priv_call
	AN(args->arg2); // priv_vcl
	AN(args->arg3); // priv_task
	assert(args->arg1 != args->arg2);
	assert(args->arg2 != args->arg3);

	if (args->valid_s)
		AN(args->s);
	(void)args->valid_b;
	(void)args->b;

	AN(op);
	AZ(*op);
	ALLOC_OBJ(o, VMOD_DEBUG_OBJ_OPT_MAGIC);
	AN(o);
	*op = o;
	REPLACE(o->name, vcl_name);
	memcpy(&o->args, args, sizeof o->args);
	if (args->valid_s) {
		REPLACE(o->freeptr, args->s);
		o->args.s = o->freeptr;
	}
}

VCL_VOID v_matchproto_()
xyzzy_obj_opt__fini(struct xyzzy_debug_obj_opt **op)
{
	struct xyzzy_debug_obj_opt *o;

	TAKE_OBJ_NOTNULL(o, op, VMOD_DEBUG_OBJ_OPT_MAGIC);

	REPLACE(o->name, NULL);
	if (o->freeptr) {
		AN(o->args.valid_s);
		REPLACE(o->freeptr, NULL);
	}
	FREE_OBJ(o);
}

VCL_STRING v_matchproto_()
xyzzy_obj_opt_meth_opt(VRT_CTX,
    struct xyzzy_debug_obj_opt *o,
    struct VARGS(obj_opt_meth_opt) *args)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(o, VMOD_DEBUG_OBJ_OPT_MAGIC);

	AN(args);
	AN(args->arg1); // priv_call
	AN(args->arg2); // priv_vcl
	AN(args->arg3); // priv_task
	assert(args->arg1 != args->arg2);
	assert(args->arg2 != args->arg3);

	return (WS_Printf(ctx->ws,
	    "obj %s obj_s %s obj_b %s met_s %s met_b %s",
	    o->name,
	    (o->args.valid_s ? o->args.s : "*undef*"),
	    (o->args.valid_b
			? (o->args.b ? "true" : "false" )
			: "*undef*"),
	    (args->valid_s ? args->s : "*undef*"),
	    (args->valid_b
			? (args->b ? "true" : "false" )
			: "*undef*")
	));
}
