/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 * Request management
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include "cache_varnishd.h"
#include "cache_filter.h"
#include "cache_pool.h"
#include "cache_transport.h"

#include "common/heritage.h"
#include "vtim.h"

void
Req_AcctLogCharge(struct VSC_main_wrk *ds, struct req *req)
{
	struct acct_req *a;

	AN(ds);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	a = &req->acct;

	if (!IS_NO_VXID(req->vsl->wid) && !(req->res_mode & RES_PIPE)) {
		VSLb(req->vsl, SLT_ReqAcct, "%ju %ju %ju %ju %ju %ju",
		    (uintmax_t)a->req_hdrbytes,
		    (uintmax_t)a->req_bodybytes,
		    (uintmax_t)(a->req_hdrbytes + a->req_bodybytes),
		    (uintmax_t)a->resp_hdrbytes,
		    (uintmax_t)a->resp_bodybytes,
		    (uintmax_t)(a->resp_hdrbytes + a->resp_bodybytes));
	}

	if (IS_TOPREQ(req)) {
#define ACCT(foo) ds->s_##foo += a->foo;
#include "tbl/acct_fields_req.h"
	}
	memset(a, 0, sizeof *a);
}

void
Req_LogHit(struct worker *wrk, struct req *req, struct objcore *oc,
    intmax_t fetch_progress)
{
	const char *clen, *sep;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	if (fetch_progress >= 0) {
		clen = HTTP_GetHdrPack(wrk, oc, H_Content_Length);
		if (clen == NULL)
			clen = sep = "";
		else
			sep = " ";
		VSLb(req->vsl, SLT_Hit, "%ju %.6f %.6f %.6f %jd%s%s",
		    VXID(ObjGetXID(wrk, oc)), EXP_Dttl(req, oc),
		    oc->grace, oc->keep,
		    fetch_progress, sep, clen);
	} else {
		VSLb(req->vsl, SLT_Hit, "%ju %.6f %.6f %.6f",
		    VXID(ObjGetXID(wrk, oc)), EXP_Dttl(req, oc),
		    oc->grace, oc->keep);
	}
}

const char *
Req_LogStart(const struct worker *wrk, struct req *req)
{
	const char *ci, *cp, *endpname;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->sp, SESS_MAGIC);

	ci = SES_Get_String_Attr(req->sp, SA_CLIENT_IP);
	cp = SES_Get_String_Attr(req->sp, SA_CLIENT_PORT);
	CHECK_OBJ_NOTNULL(req->sp->listen_sock, LISTEN_SOCK_MAGIC);
	endpname = req->sp->listen_sock->name;
	AN(endpname);
	VSLb(req->vsl, SLT_ReqStart, "%s %s %s", ci, cp, endpname);

	return (ci);
}

/*--------------------------------------------------------------------
 * Alloc/Free a request
 */

struct req *
Req_New(struct sess *sp)
{
	struct pool *pp;
	struct req *req;
	uint16_t nhttp;
	unsigned sz, hl;
	char *p, *e;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	pp = sp->pool;
	CHECK_OBJ_NOTNULL(pp, POOL_MAGIC);

	req = MPL_Get(pp->mpl_req, &sz);
	AN(req);
	req->magic = REQ_MAGIC;
	req->sp = sp;

	e = (char*)req + sz;
	p = (char*)(req + 1);
	p = (void*)PRNDUP(p);
	assert(p < e);

	nhttp = (uint16_t)cache_param->http_max_hdr;
	hl = HTTP_estimate(nhttp);

	req->http = HTTP_create(p, nhttp, hl);
	p += hl;
	p = (void*)PRNDUP(p);
	assert(p < e);

	req->http0 = HTTP_create(p, nhttp, hl);
	p += hl;
	p = (void*)PRNDUP(p);
	assert(p < e);

	req->resp = HTTP_create(p, nhttp, hl);
	p += hl;
	p = (void*)PRNDUP(p);
	assert(p < e);

	sz = cache_param->vsl_buffer;
	VSL_Setup(req->vsl, p, sz);
	p += sz;
	p = (void*)PRNDUP(p);

	req->vfc = (void*)p;
	INIT_OBJ(req->vfc, VFP_CTX_MAGIC);
	p = (void*)PRNDUP(p + sizeof(*req->vfc));

	req->vdc = (void*)p;
	ZERO_OBJ(req->vdc, sizeof *req->vdc);
	p = (void*)PRNDUP(p + sizeof(*req->vdc));

	req->htc = (void*)p;
	INIT_OBJ(req->htc, HTTP_CONN_MAGIC);
	req->htc->doclose = SC_NULL;
	p = (void*)PRNDUP(p + sizeof(*req->htc));

	req->top = (void*)p;
	INIT_OBJ(req->top, REQTOP_MAGIC);
	req->top->topreq = req;
	p = (void*)PRNDUP(p + sizeof(*req->top));

	assert(p < e);

	WS_Init(req->ws, "req", p, e - p);

	req->t_first = NAN;
	req->t_prev = NAN;
	req->t_req = NAN;
	req->req_step = R_STP_TRANSPORT;
	req->doclose = SC_NULL;

	return (req);
}

void
Req_Release(struct req *req)
{
	struct sess *sp;
	struct pool *pp;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	/* Make sure the request counters have all been zeroed */
#define ACCT(foo) \
	AZ(req->acct.foo);
#include "tbl/acct_fields_req.h"

	AZ(req->vcl);
	if (!IS_NO_VXID(req->vsl->wid))
		VSL_End(req->vsl);
#ifdef ENABLE_WORKSPACE_EMULATOR
	WS_Rollback(req->ws, 0);
#endif
	TAKE_OBJ_NOTNULL(sp, &req->sp, SESS_MAGIC);
	pp = sp->pool;
	CHECK_OBJ_NOTNULL(pp, POOL_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	MPL_AssertSane(req);
	VSL_Flush(req->vsl, 0);
	MPL_Free(pp->mpl_req, req);
}

/*----------------------------------------------------------------------
 * TODO:
 * - check for code duplication with cnt_recv_prep
 * - re-check if complete
 * - XXX should PRIV_TOP use vcl0?
 * - XXX PRIV_TOP does not get rolled back, should it for !IS_TOPREQ ?
 */

void
Req_Rollback(VRT_CTX)
{
	struct req *req;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	req = ctx->req;
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	if (IS_TOPREQ(req))
		VCL_TaskLeave(ctx, req->top->privs);
	VCL_TaskLeave(ctx, req->privs);
	VCL_TaskEnter(req->privs);
	if (IS_TOPREQ(req))
		VCL_TaskEnter(req->top->privs);
	HTTP_Clone(req->http, req->http0);
	req->vdp_filter_list = NULL;
	req->vcf = NULL;
	if (WS_Overflowed(req->ws))
		req->wrk->stats->ws_client_overflow++;
	AN(req->ws_req);
	WS_Rollback(req->ws, req->ws_req);
}

/*----------------------------------------------------------------------
 * TODO: remove code duplication with cnt_recv_prep
 */

void
Req_Cleanup(struct sess *sp, struct worker *wrk, struct req *req)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	assert(sp == req->sp);
	if (IS_TOPREQ(req))
		AZ(req->top->vcl0);

	AZ(req->director_hint);
	req->restarts = 0;

	if (req->vcl != NULL)
		VCL_Recache(wrk, &req->vcl);

	/* Charge and log byte counters */
	if (!IS_NO_VXID(req->vsl->wid)) {
		Req_AcctLogCharge(wrk->stats, req);
		if (!IS_SAME_VXID(req->vsl->wid, sp->vxid))
			VSL_End(req->vsl);
		else
			req->vsl->wid = NO_VXID; /* ending an h2 stream 0 */
	}

	if (!isnan(req->t_prev) && req->t_prev > 0. && req->t_prev > sp->t_idle)
		sp->t_idle = req->t_prev;
	else
		sp->t_idle = W_TIM_real(wrk);

	req->t_first = NAN;
	req->t_prev = NAN;
	req->t_req = NAN;
	req->req_body_status = NULL;

	req->hash_always_miss = 0;
	req->hash_ignore_busy = 0;
	req->hash_ignore_vary = 0;
	req->esi_level = 0;
	req->is_hit = 0;
	req->req_step = R_STP_TRANSPORT;
	req->vcf = NULL;
	req->doclose = SC_NULL;
	req->htc->doclose = SC_NULL;

	if (WS_Overflowed(req->ws))
		wrk->stats->ws_client_overflow++;

	wrk->seen_methods = 0;

	VDP_Fini(req->vdc);
}

/*----------------------------------------------------------------------
 */

void v_matchproto_(vtr_req_fail_f)
Req_Fail(struct req *req, stream_close_t reason)
{
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	AN(req->transport->req_fail);
	req->transport->req_fail(req, reason);
}
