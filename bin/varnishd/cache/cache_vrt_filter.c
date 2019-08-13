/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2016 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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

static struct vfilter_head vfp_filters =
    VTAILQ_HEAD_INITIALIZER(vfp_filters);

static struct vfilter_head vdp_filters =
    VTAILQ_HEAD_INITIALIZER(vdp_filters);

void
VRT_AddVFP(VRT_CTX, const struct vfp *filter)
{
	struct vfilter *vp;
	struct vfilter_head *hd = &vfp_filters;

	CHECK_OBJ_ORNULL(ctx, VRT_CTX_MAGIC);
	VTAILQ_FOREACH(vp, hd, list) {
		xxxassert(vp->vfp != filter);
		xxxassert(strcasecmp(vp->name, filter->name));
	}
	if (ctx != NULL) {
		ASSERT_CLI();
		hd = &ctx->vcl->vfps;
		VTAILQ_FOREACH(vp, hd, list) {
			xxxassert(vp->vfp != filter);
			xxxassert(strcasecmp(vp->name, filter->name));
		}
	}
	ALLOC_OBJ(vp, VFILTER_MAGIC);
	AN(vp);
	vp->vfp = filter;
	vp->name = filter->name;
	vp->nlen = strlen(vp->name);
	VTAILQ_INSERT_TAIL(hd, vp, list);
}

void
VRT_AddVDP(VRT_CTX, const struct vdp *filter)
{
	struct vfilter *vp;
	struct vfilter_head *hd = &vdp_filters;

	CHECK_OBJ_ORNULL(ctx, VRT_CTX_MAGIC);
	VTAILQ_FOREACH(vp, hd, list) {
		xxxassert(vp->vdp != filter);
		xxxassert(strcasecmp(vp->name, filter->name));
	}
	if (ctx != NULL) {
		ASSERT_CLI();
		hd = &ctx->vcl->vdps;
		VTAILQ_FOREACH(vp, hd, list) {
			xxxassert(vp->vdp != filter);
			xxxassert(strcasecmp(vp->name, filter->name));
		}
	}
	ALLOC_OBJ(vp, VFILTER_MAGIC);
	AN(vp);
	vp->vdp = filter;
	vp->name = filter->name;
	vp->nlen = strlen(vp->name);
	VTAILQ_INSERT_TAIL(hd, vp, list);
}

void
VRT_RemoveVFP(VRT_CTX, const struct vfp *filter)
{
	struct vfilter *vp;
	struct vfilter_head *hd = &ctx->vcl->vfps;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	ASSERT_CLI();
	VTAILQ_FOREACH(vp, hd, list) {
		if (vp->vfp == filter)
			break;
	}
	XXXAN(vp);
	VTAILQ_REMOVE(hd, vp, list);
	FREE_OBJ(vp);
}

void
VRT_RemoveVDP(VRT_CTX, const struct vdp *filter)
{
	struct vfilter *vp;
	struct vfilter_head *hd = &ctx->vcl->vdps;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	ASSERT_CLI();
	VTAILQ_FOREACH(vp, hd, list) {
		if (vp->vdp == filter)
			break;
	}
	XXXAN(vp);
	VTAILQ_REMOVE(hd, vp, list);
	FREE_OBJ(vp);
}

static const struct vfilter vfilter_error[1];

// XXX: idea(fgs): Allow filters (...) arguments in the list
static const struct vfilter *
vcl_filter_list_iter(const struct vfilter_head *h1,
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
	VTAILQ_FOREACH(vp, h1, list)
		if (vp->nlen == q - fl && !memcmp(fl, vp->name, vp->nlen))
			return (vp);
	VTAILQ_FOREACH(vp, h2, list)
		if (vp->nlen == q - fl && !memcmp(fl, vp->name, vp->nlen))
			return (vp);
	*flp = fl;
	return (vfilter_error);
}

int
VCL_StackVFP(struct vfp_ctx *vc, const struct vcl *vcl, const char *fl)
{
	const struct vfilter *vp;

	AN(fl);
	VSLb(vc->wrk->vsl, SLT_Filters, "%s", fl);

	while (1) {
		vp = vcl_filter_list_iter(&vfp_filters, &vcl->vfps, &fl);
		if (vp == NULL)
			return (0);
		if (vp == vfilter_error)
			return (VFP_Error(vc, "Filter '...%s' not found", fl));
		if (VFP_Push(vc, vp->vfp) == NULL)
			return (-1);
	}
}

int
VCL_StackVDP(struct req *req, const struct vcl *vcl, const char *fl)
{
	const struct vfilter *vp;

	AN(fl);
	VSLb(req->vsl, SLT_Filters, "%s", fl);
	while (1) {
		vp = vcl_filter_list_iter(&vdp_filters, &vcl->vfps, &fl);
		if (vp == NULL)
			return (0);
		if (vp == vfilter_error) {
			VSLb(req->vsl, SLT_Error,
			    "Filter '...%s' not found", fl);
			return (-1);
		}
		if (VDP_Push(req, vp->vdp, NULL))
			return (-1);
	}
}

void
VCL_VRT_Init(void)
{
	VRT_AddVFP(NULL, &VFP_testgunzip);
	VRT_AddVFP(NULL, &VFP_gunzip);
	VRT_AddVFP(NULL, &VFP_gzip);
	VRT_AddVFP(NULL, &VFP_esi);
	VRT_AddVFP(NULL, &VFP_esi_gzip);
	VRT_AddVDP(NULL, &VDP_esi);
	VRT_AddVDP(NULL, &VDP_gunzip);
	VRT_AddVDP(NULL, &VDP_range);
}

/*--------------------------------------------------------------------
 */

typedef void filter_list_t(void *, struct vsb *vsb);

static const char *
filter_on_ws(struct ws *ws, filter_list_t *func, void *arg)
{
	unsigned u;
	struct vsb vsb[1];

	AN(func);
	AN(arg);
	u = WS_Reserve(ws, 0);
	if (u == 0) {
		WS_Release(ws, 0);
		WS_MarkOverflow(ws);
		return (NULL);
	}
	AN(VSB_new(vsb, ws->f, u, VSB_FIXEDLEN));
	func(arg, vsb);
	if (VSB_finish(vsb)) {
		WS_Release(ws, 0);
		WS_MarkOverflow(ws);
		return (NULL);
	}
	if (VSB_len(vsb)) {
		WS_Release(ws, VSB_len(vsb) + 1);
		return (VSB_data(vsb) + 1);
	}
	WS_Release(ws, 0);
	return ("");
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

/*--------------------------------------------------------------------
 */

static void v_matchproto_(filter_list_t)
resp_default_filter_list(void *arg, struct vsb *vsb)
{
	struct req *req;

	CAST_OBJ_NOTNULL(req, arg, REQ_MAGIC);

	if (!req->disable_esi && req->resp_len != 0 &&
	    ObjHasAttr(req->wrk, req->objcore, OA_ESIDATA))
		VSB_cat(vsb, " esi");

	if (cache_param->http_gzip_support &&
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

/*--------------------------------------------------------------------*/

#define FILTER_VAR(vcl, in, func)					\
	VCL_STRING							\
	VRT_r_##vcl##_filters(VRT_CTX)					\
	{								\
									\
		CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
		if (ctx->in->filter_list != NULL)			\
			return(ctx->in->filter_list);			\
		return (func(ctx->in));					\
	}								\
									\
	VCL_VOID							\
	VRT_l_##vcl##_filters(VRT_CTX, const char *str, ...)		\
	{								\
		va_list ap;						\
		const char *b;						\
									\
		CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
		va_start(ap, str);					\
		b = VRT_String(ctx->in->ws, NULL, str, ap);		\
		va_end(ap);						\
		if (b == NULL)						\
			WS_MarkOverflow(ctx->in->ws);			\
		else							\
			ctx->in->filter_list = b;			\
	}

FILTER_VAR(beresp, bo, VBF_Get_Filter_List)
FILTER_VAR(resp, req, resp_Get_Filter_List)
