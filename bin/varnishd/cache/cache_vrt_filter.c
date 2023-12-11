/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2016 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "cache_varnishd.h"
#include "cache_vcl.h"
#include "vrt_obj.h"

#include "vct.h"

#include "cache_filter.h"

/*--------------------------------------------------------------------
 */

struct vfilter {
	unsigned			magic;
#define VFILTER_MAGIC			0xd40894e9
	const struct vfp		*vfp;
	const struct vdp		*vdp;
	const char			*name;
	int				nlen;
	VTAILQ_ENTRY(vfilter)		list;
};

static struct vfilter_head vrt_filters =
    VTAILQ_HEAD_INITIALIZER(vrt_filters);

static const char *
is_dup_filter(const struct vfilter_head *head, const struct vfp * vfp,
    const struct vdp *vdp, const char *name)
{
	struct vfilter *vp;
	VTAILQ_FOREACH(vp, head, list) {
		if (vfp != NULL && vp->vfp != NULL) {
			if (vp->vfp == vfp)
				return ("VFP already registered");
			if (!strcasecmp(vp->name, name))
				return ("VFP name already used");
		}
		if (vdp != NULL && vp->vdp != NULL) {
			if (vp->vdp == vdp)
				return ("VDP already registered");
			if (!strcasecmp(vp->name, name))
				return ("VDP name already used");
		}
	}
	return (NULL);
}

static const char *
vrt_addfilter(VRT_CTX, const struct vfp *vfp, const struct vdp *vdp)
{
	struct vfilter *vp;
	struct vfilter_head *hd = &vrt_filters;
	const char *err, *name = NULL;

	CHECK_OBJ_ORNULL(ctx, VRT_CTX_MAGIC);
	assert(vfp != NULL || vdp != NULL);
	assert(vfp == NULL || vfp->name != NULL);
	assert(vdp == NULL || vdp->name != NULL);
	assert(vfp == NULL || vdp == NULL || !strcasecmp(vfp->name, vdp->name));
	if (vfp != NULL)
		name = vfp->name;
	else if (vdp != NULL)
		name = vdp->name;
	AN(name);

	err = is_dup_filter(hd, vfp, vdp, name);
	if (err != NULL) {
		if (ctx != NULL)
			VRT_fail(ctx, "%s: %s (global)", name, err);
		return (err);
	}
	if (ctx != NULL) {
		ASSERT_CLI();
		CHECK_OBJ_NOTNULL(ctx->vcl, VCL_MAGIC);
		hd = &ctx->vcl->filters;
		err = is_dup_filter(hd, vfp, vdp, name);
		if (err != NULL) {
			VRT_fail(ctx, "%s: %s (per-vcl)", name, err);
			return (err);
		}
	}

	ALLOC_OBJ(vp, VFILTER_MAGIC);
	AN(vp);
	vp->vfp = vfp;
	vp->vdp = vdp;
	vp->name = name;
	vp->nlen = strlen(name);
	VTAILQ_INSERT_TAIL(hd, vp, list);
	return (err);
}

const char *
VRT_AddFilter(VRT_CTX, const struct vfp *vfp, const struct vdp *vdp)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	return (vrt_addfilter(ctx, vfp, vdp));
}

void
VRT_AddVFP(VRT_CTX, const struct vfp *filter)
{
	AZ(VRT_AddFilter(ctx, filter, NULL));
}

void
VRT_AddVDP(VRT_CTX, const struct vdp *filter)
{
	AZ(VRT_AddFilter(ctx, NULL, filter));
}

void
VRT_RemoveFilter(VRT_CTX, const struct vfp *vfp, const struct vdp *vdp)
{
	struct vfilter *vp;
	struct vfilter_head *hd;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->vcl, VCL_MAGIC);
	hd = &ctx->vcl->filters;
	assert(vfp != NULL || vdp != NULL);
	assert(vfp == NULL || vfp->name != NULL);
	assert(vdp == NULL || vdp->name != NULL);
	assert(vfp == NULL || vdp == NULL || !strcasecmp(vfp->name, vdp->name));

	ASSERT_CLI();
	VTAILQ_FOREACH(vp, hd, list) {
		CHECK_OBJ_NOTNULL(vp, VFILTER_MAGIC);
		if (vp->vfp == vfp && vp->vdp == vdp)
			break;
	}
	AN(vp);
	assert(vfp == NULL || !strcasecmp(vfp->name, vp->name));
	assert(vdp == NULL || !strcasecmp(vdp->name, vp->name));
	VTAILQ_REMOVE(hd, vp, list);
	FREE_OBJ(vp);
}

void
VRT_RemoveVFP(VRT_CTX, const struct vfp *filter)
{

	VRT_RemoveFilter(ctx, filter, NULL);
}

void
VRT_RemoveVDP(VRT_CTX, const struct vdp *filter)
{

	VRT_RemoveFilter(ctx, NULL, filter);
}

static const struct vfilter vfilter_error[1];

// XXX: idea(fgs): Allow filters (...) arguments in the list
static const struct vfilter *
vcl_filter_list_iter(int want_vfp, const struct vfilter_head *h1,
    const struct vfilter_head *h2, const char **flp)
{
	const char *fl, *q;
	const struct vfilter *vp;

	AN(h1);
	AN(h2);
	AN(flp);

	fl = *flp;
	AN(fl);

	while (vct_isspace(*fl))
		fl++;
	if (*fl == '\0') {
		*flp = NULL;
		return (NULL);
	}
	for (q = fl; *q && !vct_isspace(*q); q++)
		continue;
	*flp = q;
	VTAILQ_FOREACH(vp, h1, list) {
		if (want_vfp && vp->vfp == NULL)
			continue;
		else if (!want_vfp && vp->vdp == NULL)
			continue;
		if (vp->nlen == q - fl && !memcmp(fl, vp->name, vp->nlen))
			return (vp);
	}
	VTAILQ_FOREACH(vp, h2, list) {
		if (want_vfp && vp->vfp == NULL)
			continue;
		else if (!want_vfp && vp->vdp == NULL)
			continue;
		if (vp->nlen == q - fl && !memcmp(fl, vp->name, vp->nlen))
			return (vp);
	}
	*flp = fl;
	return (vfilter_error);
}

int
VCL_StackVFP(struct vfp_ctx *vc, const struct vcl *vcl, const char *fl)
{
	const struct vfilter *vp;

	AN(fl);
	VSLbs(vc->wrk->vsl, SLT_Filters, TOSTRAND(fl));

	while (1) {
		vp = vcl_filter_list_iter(1, &vrt_filters, &vcl->filters, &fl);
		if (vp == NULL)
			return (0);
		if (vp == vfilter_error)
			return (VFP_Error(vc, "Filter '...%s' not found", fl));
		if (VFP_Push(vc, vp->vfp) == NULL)
			return (-1);
	}
}

int
VCL_StackVDP(struct vdp_ctx *vdc, const struct vcl *vcl, const char *fl,
    struct req *req, struct busyobj *bo, intmax_t *cl)
{
	const struct vfilter *vp;
	struct vrt_ctx ctx[1];
	struct objcore *oc;
	struct http *hd;

	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	AN(vcl);
	AN(fl);

	CHECK_OBJ_ORNULL(req, REQ_MAGIC);
	CHECK_OBJ_ORNULL(bo, BUSYOBJ_MAGIC);
	AN(cl);

	assert((req ? 1 : 0) ^ (bo ? 1 : 0));

	VSLbs(vdc->vsl, SLT_Filters, TOSTRAND(fl));
	INIT_OBJ(ctx, VRT_CTX_MAGIC);

	if (req) {
		VCL_Req2Ctx(ctx, req);
		oc = req->objcore;
		hd = req->resp;
	}
	else {
		VCL_Bo2Ctx(ctx, bo);
		oc = bo->bereq_body;
		hd = bo->bereq;
	}

	while (1) {
		vp = vcl_filter_list_iter(0, &vrt_filters, &vcl->filters, &fl);
		if (vp == NULL)
			return (0);
		if (vp == vfilter_error) {
			VSLb(vdc->vsl, SLT_Error,
			    "Filter '...%s' not found", fl);
			return (-1);
		}
		if (VDP_Push(ctx, vdc, ctx->ws, vp->vdp, NULL,
		    oc, req, hd, cl))
			return (-1);
	}
}

void
VCL_VRT_Init(void)
{
	AZ(vrt_addfilter(NULL, &VFP_testgunzip, NULL));
	AZ(vrt_addfilter(NULL, &VFP_gunzip, NULL));
	AZ(vrt_addfilter(NULL, &VFP_gzip, NULL));
	AZ(vrt_addfilter(NULL, &VFP_esi, NULL));
	AZ(vrt_addfilter(NULL, &VFP_esi_gzip, NULL));
	AZ(vrt_addfilter(NULL, NULL, &VDP_esi));
	AZ(vrt_addfilter(NULL, NULL, &VDP_gunzip));
	AZ(vrt_addfilter(NULL, NULL, &VDP_range));
}

/*--------------------------------------------------------------------
 */

typedef void filter_list_t(void *, struct vsb *vsb);

static const char *
filter_on_ws(struct ws *ws, filter_list_t *func, void *arg)
{
	struct vsb vsb[1];
	const char *p;

	AN(func);
	AN(arg);
	WS_VSB_new(vsb, ws);
	func(arg, vsb);
	p = WS_VSB_finish(vsb, ws, NULL);
	if (p == NULL)
		p = "";
	return (p);
}

/*--------------------------------------------------------------------
 */

static void v_matchproto_(filter_list_t)
vbf_default_filter_list(void *arg, struct vsb *vsb)
{
	const struct busyobj *bo;
	const char *p;
	int do_gzip, do_gunzip, is_gzip = 0, is_gunzip = 0;

	CAST_OBJ_NOTNULL(bo, arg, BUSYOBJ_MAGIC);

	do_gzip = bo->do_gzip;
	do_gunzip = bo->do_gunzip;

	/*
	 * The VCL variables beresp.do_g[un]zip tells us how we want the
	 * object processed before it is stored.
	 *
	 * The backend Content-Encoding header tells us what we are going
	 * to receive, which we classify in the following three classes:
	 *
	 *	"Content-Encoding: gzip"	--> object is gzip'ed.
	 *	no Content-Encoding		--> object is not gzip'ed.
	 *	anything else			--> do nothing wrt gzip
	 */

	/* No body -> done */
	if (bo->htc->body_status == BS_NONE || bo->htc->content_length == 0)
		return;

	if (!cache_param->http_gzip_support)
		do_gzip = do_gunzip = 0;

	if (http_GetHdr(bo->beresp, H_Content_Encoding, &p))
		is_gzip = !strcasecmp(p, "gzip");
	else
		is_gunzip = 1;

	/* We won't gunzip unless it is gzip'ed */
	if (do_gunzip && !is_gzip)
		do_gunzip = 0;

	/* We wont gzip unless if it already is gzip'ed */
	if (do_gzip && !is_gunzip)
		do_gzip = 0;

	if (do_gunzip || (is_gzip && bo->do_esi))
		VSB_cat(vsb, " gunzip");

	if (bo->do_esi && (do_gzip || (is_gzip && !do_gunzip))) {
		VSB_cat(vsb, " esi_gzip");
		return;
	}

	if (bo->do_esi) {
		VSB_cat(vsb, " esi");
		return;
	}

	if (do_gzip)
		VSB_cat(vsb, " gzip");

	if (is_gzip && !do_gunzip)
		VSB_cat(vsb, " testgunzip");
}

const char *
VBF_Get_Filter_List(struct busyobj *bo)
{

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	return (filter_on_ws(bo->ws, vbf_default_filter_list, bo));
}

static const char *
bereq_Empty_Filter(struct busyobj *bo)
{

	(void)bo;
	return ("");
}

/*--------------------------------------------------------------------
 */

static void v_matchproto_(filter_list_t)
resp_default_filter_list(void *arg, struct vsb *vsb)
{
	struct req *req;

	CAST_OBJ_NOTNULL(req, arg, REQ_MAGIC);

	if (!req->disable_esi && req->objcore != NULL &&
	    ObjHasAttr(req->wrk, req->objcore, OA_ESIDATA))
		VSB_cat(vsb, " esi");

	if (cache_param->http_gzip_support &&
	    req->objcore != NULL &&
	    ObjCheckFlag(req->wrk, req->objcore, OF_GZIPED) &&
	    !RFC2616_Req_Gzip(req->http))
		VSB_cat(vsb, " gunzip");

	if (cache_param->http_range_support &&
	    http_GetStatus(req->resp) == 200 &&
	    http_GetHdr(req->http, H_Range, NULL))
		VSB_cat(vsb, " range");
}

const char *
resp_Get_Filter_List(struct req *req)
{
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	return (filter_on_ws(req->ws, resp_default_filter_list, req));
}

static const char *
req_Empty_Filter(struct req *req)
{

	(void)req;
	return ("");
}

/*--------------------------------------------------------------------*/
static int
req_filter_can(struct req *req) {
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	return (req->req_body_status->avail == 1);
}

/*--------------------------------------------------------------------*/

#define FILTER_VAR(vcl, in, func, cond, fld)				\
	VCL_STRING							\
	VRT_r_##vcl##_filters(VRT_CTX)					\
	{								\
									\
		CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
		if (ctx->in->fld != NULL)				\
			return(ctx->in->fld);				\
		return (func(ctx->in));					\
	}								\
									\
	VCL_VOID							\
	VRT_l_##vcl##_filters(VRT_CTX, const char *str, VCL_STRANDS s)	\
	{								\
		const char *b;						\
									\
		(void)str;						\
		CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
		if (! (cond)) {						\
			VRT_fail(ctx, #vcl ".filters not settable");	\
			return;						\
		}							\
		b = VRT_StrandsWS(ctx->in->ws, str, s);			\
		if (b == NULL)						\
			WS_MarkOverflow(ctx->in->ws);			\
		else							\
			ctx->in->fld = b;				\
	}

FILTER_VAR(bereq, bo, bereq_Empty_Filter, 1, vdp_filter_list)
FILTER_VAR(beresp, bo, VBF_Get_Filter_List, 1, vfp_filter_list)
FILTER_VAR(req, req, req_Empty_Filter, req_filter_can(ctx->req), vfp_filter_list)
FILTER_VAR(resp, req, resp_Get_Filter_List, 1, vdp_filter_list)
