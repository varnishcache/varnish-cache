/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 */

#include "config.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache.h"

#include "hash/hash_slinger.h"

#include "cache_backend.h"
#include "vcli_priv.h"
#include "vcl.h"
#include "vtim.h"

/*--------------------------------------------------------------------
 * Copy req->bereq and run it by VCL::vcl_backend_fetch{}
 */

static enum fetch_step
vbf_stp_mkbereq(struct worker *wrk, struct busyobj *bo, const struct req *req)
{
	int i;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	AN(bo->director);
	AZ(bo->vbc);
	AZ(bo->should_close);
	AZ(bo->storage_hint);

	HTTP_Setup(bo->bereq, bo->ws, bo->vsl, HTTP_Bereq);
	http_FilterReq(bo->bereq, req->http,
	    bo->do_pass ? HTTPH_R_PASS : HTTPH_R_FETCH);
	if (!bo->do_pass) {
		// XXX: Forcing GET should happen in vcl_miss{} ?
		http_ForceGet(bo->bereq);
		if (cache_param->http_gzip_support) {
			/*
			 * We always ask the backend for gzip, even if the
			 * client doesn't grok it.  We will uncompress for
			 * the minority of clients which don't.
			 */
			http_Unset(bo->bereq, H_Accept_Encoding);
			http_SetHeader(bo->bereq, "Accept-Encoding: gzip");
		}
	}

	// Don't let VCL reset do_pass
	i = bo->do_pass;
	VCL_backend_fetch_method(bo->vcl, wrk, NULL, bo, bo->bereq->ws);
	bo->do_pass |= i;

	http_PrintfHeader(bo->bereq,
	    "X-Varnish: %u", bo->vsl->wid & VSL_IDENTMASK);
	/* XXX: Missing ABANDON */
	return (F_STP_FETCHHDR);
}
/*--------------------------------------------------------------------
 */

static enum fetch_step
vbf_stp_fetchhdr(struct worker *wrk, struct busyobj *bo, struct req **reqp)
{
	int i;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AN(reqp);
	CHECK_OBJ_NOTNULL((*reqp), REQ_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	xxxassert (wrk->handling == VCL_RET_FETCH);

	HTTP_Setup(bo->beresp, bo->ws, bo->vsl, HTTP_Beresp);

	if (!bo->do_pass)
		*reqp = NULL;

	i = V1F_fetch_hdr(wrk, bo, *reqp);
	/*
	 * If we recycle a backend connection, there is a finite chance
	 * that the backend closed it before we get a request to it.
	 * Do a single retry in that case.
	 */
	if (i == 1) {
		VSC_C_main->backend_retry++;
		i = V1F_fetch_hdr(wrk, bo, *reqp);
	}

	if (bo->do_pass)
		*reqp = NULL;

	if (i) {
		AZ(bo->vbc);
		bo->err_code = 503;
		http_SetH(bo->beresp, HTTP_HDR_PROTO, "HTTP/1.1");
		http_SetResp(bo->beresp,
		    "HTTP/1.1", 503, "Backend fetch failed");
		http_SetHeader(bo->beresp, "Content-Length: 0");
		http_SetHeader(bo->beresp, "Connection: close");
	} else {
		AN(bo->vbc);
	}

	/*
	 * These two headers can be spread over multiple actual headers
	 * and we rely on their content outside of VCL, so collect them
	 * into one line here.
	 */
	http_CollectHdr(bo->beresp, H_Cache_Control);
	http_CollectHdr(bo->beresp, H_Vary);

	/*
	 * Figure out how the fetch is supposed to happen, before the
	 * headers are adultered by VCL
	 * NB: Also sets other wrk variables
	 */
	bo->htc.body_status = RFC2616_Body(bo, &wrk->stats);

	bo->err_code = http_GetStatus(bo->beresp);

	/*
	 * What does RFC2616 think about TTL ?
	 */
	EXP_Clr(&bo->exp);
	bo->exp.entered = W_TIM_real(wrk);
	RFC2616_Ttl(bo);

	/* pass from vclrecv{} has negative TTL */
	if (bo->fetch_objcore->objhead == NULL)
		bo->exp.ttl = -1.;

	AZ(bo->do_esi);

	// Don't let VCL reset do_pass
	i = bo->do_pass;
	VCL_backend_response_method(bo->vcl, wrk, NULL, bo, bo->beresp->ws);
	bo->do_pass |= i;

	if (bo->do_pass)
		bo->fetch_objcore->flags |= OC_F_PASS;

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
	 *
	 * XXX: BS_NONE/cl==0 should avoid gzip/gunzip
	 */

	/* We do nothing unless the param is set */
	if (!cache_param->http_gzip_support)
		bo->do_gzip = bo->do_gunzip = 0;

	bo->is_gzip = http_HdrIs(bo->beresp, H_Content_Encoding, "gzip");

	bo->is_gunzip = !http_GetHdr(bo->beresp, H_Content_Encoding, NULL);

	/* It can't be both */
	assert(bo->is_gzip == 0 || bo->is_gunzip == 0);

	/* We won't gunzip unless it is gzip'ed */
	if (bo->do_gunzip && !bo->is_gzip)
		bo->do_gunzip = 0;

	/* If we do gunzip, remove the C-E header */
	if (bo->do_gunzip)
		http_Unset(bo->beresp, H_Content_Encoding);

	/* We wont gzip unless it is ungziped */
	if (bo->do_gzip && !bo->is_gunzip)
		bo->do_gzip = 0;

	/* If we do gzip, add the C-E header */
	if (bo->do_gzip)
		http_SetHeader(bo->beresp, "Content-Encoding: gzip");

	/* But we can't do both at the same time */
	assert(bo->do_gzip == 0 || bo->do_gunzip == 0);

	/* ESI takes precedence and handles gzip/gunzip itself */
	if (bo->do_esi)
		bo->vfp = &vfp_esi;
	else if (bo->do_gunzip)
		bo->vfp = &vfp_gunzip;
	else if (bo->do_gzip)
		bo->vfp = &vfp_gzip;
	else if (bo->is_gzip)
		bo->vfp = &vfp_testgzip;

	if (wrk->handling == VCL_RET_DELIVER)
		return (F_STP_FETCH);

	return (F_STP_NOTYET);
}

/*--------------------------------------------------------------------
 */

static enum fetch_step
vbf_stp_fetch(struct worker *wrk, struct busyobj *bo)
{
	struct http *hp, *hp2;
	char *b;
	uint16_t nhttp;
	unsigned l;
	struct vsb *vary = NULL;
	int varyl = 0;
	struct object *obj;


	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	assert(wrk->handling == VCL_RET_DELIVER);
#if 0
	if (wrk->handling != VCL_RET_DELIVER)
		VDI_CloseFd(&bo->vbc);

	if (wrk->handling != VCL_RET_DELIVER) {
		/* Clean up partial fetch */
		AZ(bo->vbc);

		if (bo->fetch_objcore->objhead != NULL ||
		    wrk->handling == VCL_RET_RESTART ||
		    wrk->handling == VCL_RET_ERROR) {
			CHECK_OBJ_NOTNULL(bo->fetch_objcore, OBJCORE_MAGIC);
			AZ(HSH_Deref(&wrk->stats, bo->fetch_objcore, NULL));
			bo->fetch_objcore = NULL;
		}
		assert(bo->refcount == 2);
		bo->storage_hint = NULL;
		bo->director = NULL;

		switch (wrk->handling) {
		case VCL_RET_ERROR:
			bo->state = BOS_FAILED;
			VBO_DerefBusyObj(wrk, &bo);	// XXX ?
			return (F_STP_DONE);
		case VCL_RET_RESTART:
			INCOMPL();
		default:
			WRONG("Illegal action in vcl_fetch{}");
		}
	}
#endif

	if (bo->fetch_objcore->objhead == NULL)
		AN(bo->do_pass);

	/* No reason to try streaming a non-existing body */
	if (bo->htc.body_status == BS_NONE)
		bo->do_stream = 0;

	l = http_EstimateWS(bo->beresp,
	    bo->do_pass ? HTTPH_R_PASS : HTTPH_A_INS, &nhttp);

	/* Create Vary instructions */
	if (bo->fetch_objcore->objhead != NULL) {
		varyl = VRY_Create(bo, &vary);
		if (varyl > 0) {
			AN(vary);
			assert(varyl == VSB_len(vary));
			l += varyl;
		} else if (varyl < 0) {
			/* Vary parse error */
			AZ(vary);
			AZ(HSH_Deref(&wrk->stats, bo->fetch_objcore, NULL));
			bo->fetch_objcore = NULL;
			VDI_CloseFd(&bo->vbc);
			return (F_STP_ABANDON);
		} else
			/* No vary */
			AZ(vary);
	}

	if (bo->exp.ttl < cache_param->shortlived || bo->do_pass == 1)
		bo->storage_hint = TRANSIENT_STORAGE;

	/*
	 * Space for producing a Content-Length: header including padding
	 * A billion gigabytes is enough for anybody.
	 */
	l += strlen("Content-Length: XxxXxxXxxXxxXxxXxx") + sizeof(void *);

	AZ(bo->stats);
	bo->stats = &wrk->stats;
	obj = STV_NewObject(bo, bo->storage_hint, l, nhttp);
	if (obj == NULL) {
		/*
		 * Try to salvage the transaction by allocating a
		 * shortlived object on Transient storage.
		 */
		if (bo->exp.ttl > cache_param->shortlived)
			bo->exp.ttl = cache_param->shortlived;
		bo->exp.grace = 0.0;
		bo->exp.keep = 0.0;
		obj = STV_NewObject(bo, TRANSIENT_STORAGE, l, nhttp);
	}
	bo->stats = NULL;
	if (obj == NULL) {
		AZ(HSH_Deref(&wrk->stats, bo->fetch_objcore, NULL));
		bo->fetch_objcore = NULL;
		VDI_CloseFd(&bo->vbc);
		return (F_STP_ABANDON);
	}
	CHECK_OBJ_NOTNULL(obj, OBJECT_MAGIC);

	bo->storage_hint = NULL;

	AZ(bo->fetch_obj);
	bo->fetch_obj = obj;

	if (bo->do_gzip || (bo->is_gzip && !bo->do_gunzip))
		obj->gziped = 1;

	if (vary != NULL) {
		obj->vary = (void *)WS_Copy(obj->http->ws,
		    VSB_data(vary), varyl);
		AN(obj->vary);
		VRY_Validate(obj->vary);
		VSB_delete(vary);
	}

	obj->vxid = bo->vsl->wid;
	obj->response = bo->err_code;
	WS_Assert(obj->ws_o);

	/* Filter into object */
	hp = bo->beresp;
	hp2 = obj->http;

	hp2->logtag = HTTP_Obj;
	http_FilterResp(hp, hp2, bo->do_pass ? HTTPH_R_PASS : HTTPH_A_INS);
	http_CopyHome(hp2);

	if (http_GetHdr(hp, H_Last_Modified, &b))
		obj->last_modified = VTIM_parse(b);
	else
		obj->last_modified = floor(bo->exp.entered);

	assert(WRW_IsReleased(wrk));

	/*
	 * Ready to fetch the body
	 */

	assert(bo->refcount >= 2);	/* one for each thread */

	if (obj->objcore->objhead != NULL) {
		EXP_Insert(obj);
		AN(obj->objcore->ban);
		AZ(obj->ws_o->overflow);
		HSH_Unbusy(&wrk->stats, obj->objcore);
	}

	if (bo->vfp == NULL)
		bo->vfp = &VFP_nop;

	V1F_fetch_body(wrk, bo);

	assert(bo->refcount >= 1);

	if (obj->objcore->objhead != NULL)
		HSH_Ref(obj->objcore);

	if (bo->state == BOS_FAILED) {
		/* handle early failures */
		(void)HSH_Deref(&wrk->stats, NULL, &obj);
		return (F_STP_ABANDON);
	}

	VBO_DerefBusyObj(wrk, &bo);	// XXX ?
	return (F_STP_DONE);
}

/*--------------------------------------------------------------------
 */

static enum fetch_step
vbf_stp_abandon(struct worker *wrk, struct busyobj *bo)
{
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	bo->state = BOS_FAILED;
	VBO_DerefBusyObj(wrk, &bo);	// XXX ?
	return (F_STP_DONE);
}

/*--------------------------------------------------------------------
 */

static enum fetch_step
vbf_stp_notyet(void)
{
	WRONG("Patience, grashopper, patience...");
}

static enum fetch_step
vbf_stp_done(void)
{
	WRONG("Just plain wrong");
}

/*--------------------------------------------------------------------
 */

struct vbf_secret_handshake {
	unsigned		magic;
#define VBF_SECRET_HANDSHAKE_MAGIC	0x98c95172
	struct busyobj		*bo;
	struct req		**reqp;
};

static void
vbf_fetch_thread(struct worker *wrk, void *priv)
{
	struct vbf_secret_handshake *vsh;
	struct busyobj *bo;
	struct req **reqp;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(vsh, priv, VBF_SECRET_HANDSHAKE_MAGIC);
	AN(vsh->reqp);
	reqp = vsh->reqp;
	CHECK_OBJ_NOTNULL((*vsh->reqp), REQ_MAGIC);

	bo = vsh->bo;
	THR_SetBusyobj(bo);
	bo->step = F_STP_MKBEREQ;

	while (bo->step != F_STP_DONE) {
		switch(bo->step) {
#define FETCH_STEP(l, U, arg)						\
		case F_STP_##U:						\
			bo->step = vbf_stp_##l arg;			\
			break;
#include "tbl/steps.h"
#undef FETCH_STEP
		default:
			WRONG("Illegal fetch_step");
		}
	}
	assert(WRW_IsReleased(wrk));
	THR_SetBusyobj(NULL);
}

/*--------------------------------------------------------------------
 */

void
VBF_Fetch(struct worker *wrk, struct req *req)
{
	struct vbf_secret_handshake vsh;
	struct busyobj *bo;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);

	bo = req->busyobj;
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	assert(bo->refcount == 2);
	CHECK_OBJ_NOTNULL(bo->vcl, VCL_CONF_MAGIC);

	bo->fetch_objcore = req->objcore;
	req->objcore = NULL;

	vsh.magic = VBF_SECRET_HANDSHAKE_MAGIC;
	vsh.bo = bo;
	vsh.reqp = &req;

	bo->fetch_task.priv = &vsh;
	bo->fetch_task.func = vbf_fetch_thread;

	if (Pool_Task(wrk->pool, &bo->fetch_task, POOL_QUEUE_FRONT))
		vbf_fetch_thread(wrk, &vsh);
	while (req != NULL) {
		printf("XXX\n");
		(void)usleep(100000);
	}
}
