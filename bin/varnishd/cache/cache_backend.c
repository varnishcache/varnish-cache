/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 * The director implementation for VCL backends.
 *
 */

#include "config.h"

#include <stdlib.h>

#include "cache_varnishd.h"
#include "cache_director.h"

#include "vtcp.h"
#include "vtim.h"
#include "vsa.h"

#include "cache_backend.h"
#include "cache_conn_pool.h"
#include "cache_transport.h"
#include "cache_vcl.h"
#include "http1/cache_http1.h"
#include "proxy/cache_proxy.h"

#include "VSC_vbe.h"

/*--------------------------------------------------------------------*/

enum connwait_e {
	CW_DO_CONNECT = 1,
	CW_QUEUED,
	CW_DEQUEUED,
	CW_BE_BUSY,
};

struct connwait {
	unsigned			magic;
#define CONNWAIT_MAGIC			0x75c7a52b
	enum connwait_e			cw_state;
	VTAILQ_ENTRY(connwait)		cw_list;
	pthread_cond_t			cw_cond;
};

static const char * const vbe_proto_ident = "HTTP Backend";

static struct lock backends_mtx;

/*--------------------------------------------------------------------*/

void
VBE_Connect_Error(struct VSC_vbe *vsc, int err)
{

	switch(err) {
	case 0:
		/*
		 * This is kind of brittle, but zero is the only
		 * value of errno we can trust to have no meaning.
		 */
		vsc->helddown++;
		break;
	case EACCES:
	case EPERM:
		vsc->fail_eacces++;
		break;
	case EADDRNOTAVAIL:
		vsc->fail_eaddrnotavail++;
		break;
	case ECONNREFUSED:
		vsc->fail_econnrefused++;
		break;
	case ENETUNREACH:
		vsc->fail_enetunreach++;
		break;
	case ETIMEDOUT:
		vsc->fail_etimedout++;
		break;
	default:
		vsc->fail_other++;
	}
}

/*--------------------------------------------------------------------*/

#define FIND_TMO(tmx, dst, bo, be)					\
	do {								\
		CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);			\
		dst = bo->tmx;						\
		if (isnan(dst) && be->tmx >= 0.0)			\
			dst = be->tmx;					\
		if (isnan(dst))						\
			dst = cache_param->tmx;				\
	} while (0)

#define FIND_BE_SPEC(tmx, dst, be, def)					\
	do {								\
		CHECK_OBJ_NOTNULL(bp, BACKEND_MAGIC);			\
		dst = be->tmx;						\
		if (dst == def)						\
			dst = cache_param->tmx;				\
	} while (0)

#define FIND_BE_PARAM(tmx, dst, be)					\
	FIND_BE_SPEC(tmx, dst, be, 0)

#define FIND_BE_TMO(tmx, dst, be)					\
	FIND_BE_SPEC(tmx, dst, be, -1.0)

#define BE_BUSY(be)	\
	(be->max_connections > 0 && be->n_conn >= be->max_connections)

/*--------------------------------------------------------------------*/

static void
vbe_connwait_signal_locked(const struct backend *bp)
{
	struct connwait *cw;

	Lck_AssertHeld(bp->director->mtx);

	if (bp->n_conn < bp->max_connections) {
		cw = VTAILQ_FIRST(&bp->cw_head);
		if (cw != NULL) {
			CHECK_OBJ(cw, CONNWAIT_MAGIC);
			assert(cw->cw_state == CW_QUEUED);
			PTOK(pthread_cond_signal(&cw->cw_cond));
		}
	}
}

static void
vbe_connwait_dequeue_locked(struct backend *bp, struct connwait *cw)
{
	Lck_AssertHeld(bp->director->mtx);
	VTAILQ_REMOVE(&bp->cw_head, cw, cw_list);
	vbe_connwait_signal_locked(bp);
	cw->cw_state = CW_DEQUEUED;
}

static void
vbe_connwait_fini(struct connwait *cw)
{
	CHECK_OBJ_NOTNULL(cw, CONNWAIT_MAGIC);
	assert(cw->cw_state != CW_QUEUED);
	PTOK(pthread_cond_destroy(&cw->cw_cond));
	FINI_OBJ(cw);
}

/*--------------------------------------------------------------------
 * Get a connection to the backend
 *
 * note: wrk is a separate argument because it differs for pipe vs. fetch
 */

static struct pfd *
vbe_dir_getfd(VRT_CTX, struct worker *wrk, VCL_BACKEND dir, struct backend *bp,
    unsigned force_fresh)
{
	struct busyobj *bo;
	struct pfd *pfd;
	int *fdp, err;
	vtim_dur tmod;
	char abuf1[VTCP_ADDRBUFSIZE], abuf2[VTCP_ADDRBUFSIZE];
	char pbuf1[VTCP_PORTBUFSIZE], pbuf2[VTCP_PORTBUFSIZE];
	unsigned wait_limit;
	vtim_dur wait_tmod;
	vtim_dur wait_end;
	struct connwait cw[1];

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	bo = ctx->bo;
	CHECK_OBJ_NOTNULL(bp, BACKEND_MAGIC);
	AN(bp->vsc);

	if (!VRT_Healthy(ctx, dir, NULL)) {
		VSLb(bo->vsl, SLT_FetchError,
		     "backend %s: unhealthy", VRT_BACKEND_string(dir));
		bp->vsc->unhealthy++;
		VSC_C_main->backend_unhealthy++;
		return (NULL);
	}
	INIT_OBJ(cw, CONNWAIT_MAGIC);
	PTOK(pthread_cond_init(&cw->cw_cond, NULL));
	Lck_Lock(bp->director->mtx);
	FIND_BE_PARAM(backend_wait_limit, wait_limit, bp);
	FIND_BE_TMO(backend_wait_timeout, wait_tmod, bp);
	cw->cw_state = CW_DO_CONNECT;
	if (!VTAILQ_EMPTY(&bp->cw_head) || BE_BUSY(bp))
		cw->cw_state = CW_BE_BUSY;

	if (cw->cw_state == CW_BE_BUSY && wait_limit > 0 &&
	    wait_tmod > 0.0 && bp->cw_count < wait_limit) {
		VTAILQ_INSERT_TAIL(&bp->cw_head, cw, cw_list);
		bp->cw_count++;
		VSC_C_main->backend_wait++;
		cw->cw_state = CW_QUEUED;
		wait_end = VTIM_real() + wait_tmod;
		do {
			err = Lck_CondWaitUntil(&cw->cw_cond, bp->director->mtx,
			    wait_end);
		} while (err == EINTR);
		bp->cw_count--;
		if (err != 0 && BE_BUSY(bp)) {
			VTAILQ_REMOVE(&bp->cw_head, cw, cw_list);
			VSC_C_main->backend_wait_fail++;
			cw->cw_state = CW_BE_BUSY;
		}
	}
	Lck_Unlock(bp->director->mtx);

	if (cw->cw_state == CW_BE_BUSY) {
		VSLb(bo->vsl, SLT_FetchError,
		     "backend %s: busy", VRT_BACKEND_string(dir));
		bp->vsc->busy++;
		VSC_C_main->backend_busy++;
		vbe_connwait_fini(cw);
		return (NULL);
	}

	AZ(bo->htc);
	bo->htc = WS_Alloc(bo->ws, sizeof *bo->htc);
	/* XXX: we may want to detect the ws overflow sooner */
	if (bo->htc == NULL) {
		VSLb(bo->vsl, SLT_FetchError, "out of workspace");
		/* XXX: counter ? */
		if (cw->cw_state == CW_QUEUED) {
			Lck_Lock(bp->director->mtx);
			vbe_connwait_dequeue_locked(bp, cw);
			Lck_Unlock(bp->director->mtx);
		}
		vbe_connwait_fini(cw);
		return (NULL);
	}
	bo->htc->doclose = SC_NULL;
	CHECK_OBJ_NOTNULL(bo->htc->doclose, STREAM_CLOSE_MAGIC);

	FIND_TMO(connect_timeout, tmod, bo, bp);
	pfd = VCP_Get(bp->conn_pool, tmod, wrk, force_fresh, &err);
	if (pfd == NULL) {
		Lck_Lock(bp->director->mtx);
		VBE_Connect_Error(bp->vsc, err);
		Lck_Unlock(bp->director->mtx);
		VSLb(bo->vsl, SLT_FetchError,
		     "backend %s: fail errno %d (%s)",
		     VRT_BACKEND_string(dir), err, VAS_errtxt(err));
		VSC_C_main->backend_fail++;
		bo->htc = NULL;
		if (cw->cw_state == CW_QUEUED) {
			Lck_Lock(bp->director->mtx);
			vbe_connwait_dequeue_locked(bp, cw);
			Lck_Unlock(bp->director->mtx);
		}
		vbe_connwait_fini(cw);
		return (NULL);
	}

	VSLb_ts_busyobj(bo, "Connected", W_TIM_real(wrk));
	fdp = PFD_Fd(pfd);
	AN(fdp);
	assert(*fdp >= 0);

	Lck_Lock(bp->director->mtx);
	bp->n_conn++;
	bp->vsc->conn++;
	bp->vsc->req++;
	if (cw->cw_state == CW_QUEUED)
		vbe_connwait_dequeue_locked(bp, cw);

	Lck_Unlock(bp->director->mtx);

	CHECK_OBJ_NOTNULL(bo->htc->doclose, STREAM_CLOSE_MAGIC);

	err = 0;
	if (bp->proxy_header != 0)
		err += VPX_Send_Proxy(*fdp, bp->proxy_header, bo->sp);
	if (err < 0) {
		VSLb(bo->vsl, SLT_FetchError,
		     "backend %s: proxy write errno %d (%s)",
		     VRT_BACKEND_string(dir),
		     errno, VAS_errtxt(errno));
		// account as if connect failed - good idea?
		VSC_C_main->backend_fail++;
		bo->htc = NULL;
		VCP_Close(&pfd);
		AZ(pfd);
		Lck_Lock(bp->director->mtx);
		bp->n_conn--;
		bp->vsc->conn--;
		bp->vsc->req--;
		vbe_connwait_signal_locked(bp);
		Lck_Unlock(bp->director->mtx);
		vbe_connwait_fini(cw);
		return (NULL);
	}
	bo->acct.bereq_hdrbytes += err;

	PFD_LocalName(pfd, abuf1, sizeof abuf1, pbuf1, sizeof pbuf1);
	PFD_RemoteName(pfd, abuf2, sizeof abuf2, pbuf2, sizeof pbuf2);
	VSLb(bo->vsl, SLT_BackendOpen, "%d %s %s %s %s %s %s",
	    *fdp, VRT_BACKEND_string(dir), abuf2, pbuf2, abuf1, pbuf1,
	    PFD_State(pfd) == PFD_STATE_STOLEN ? "reuse" : "connect");

	INIT_OBJ(bo->htc, HTTP_CONN_MAGIC);
	bo->htc->priv = pfd;
	bo->htc->rfd = fdp;
	bo->htc->doclose = SC_NULL;
	FIND_TMO(first_byte_timeout,
	    bo->htc->first_byte_timeout, bo, bp);
	FIND_TMO(between_bytes_timeout,
	    bo->htc->between_bytes_timeout, bo, bp);
	vbe_connwait_fini(cw);
	return (pfd);
}

static void v_matchproto_(vdi_finish_f)
vbe_dir_finish(VRT_CTX, VCL_BACKEND d)
{
	struct backend *bp;
	struct busyobj *bo;
	struct pfd *pfd;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	bo = ctx->bo;
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CAST_OBJ_NOTNULL(bp, d->priv, BACKEND_MAGIC);

	CHECK_OBJ_NOTNULL(bo->htc, HTTP_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(bo->htc->doclose, STREAM_CLOSE_MAGIC);

	pfd = bo->htc->priv;
	bo->htc->priv = NULL;
	if (bo->htc->doclose != SC_NULL || bp->proxy_header != 0) {
		VSLb(bo->vsl, SLT_BackendClose, "%d %s close %s", *PFD_Fd(pfd),
		    VRT_BACKEND_string(d), bo->htc->doclose->name);
		VCP_Close(&pfd);
		AZ(pfd);
		Lck_Lock(bp->director->mtx);
	} else {
		assert (PFD_State(pfd) == PFD_STATE_USED);
		VSLb(bo->vsl, SLT_BackendClose, "%d %s recycle", *PFD_Fd(pfd),
		    VRT_BACKEND_string(d));
		Lck_Lock(bp->director->mtx);
		VSC_C_main->backend_recycle++;
		VCP_Recycle(bo->wrk, &pfd);
	}
	assert(bp->n_conn > 0);
	bp->n_conn--;
	AN(bp->vsc);
	bp->vsc->conn--;
#define ACCT(foo)	bp->vsc->foo += bo->acct.foo;
#include "tbl/acct_fields_bereq.h"
	vbe_connwait_signal_locked(bp);
	Lck_Unlock(bp->director->mtx);
	bo->htc = NULL;
}

static int v_matchproto_(vdi_gethdrs_f)
vbe_dir_gethdrs(VRT_CTX, VCL_BACKEND d)
{
	int i, extrachance = 1;
	struct backend *bp;
	struct pfd *pfd;
	struct busyobj *bo;
	struct worker *wrk;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	bo = ctx->bo;
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->bereq, HTTP_MAGIC);
	if (bo->htc != NULL)
		CHECK_OBJ_NOTNULL(bo->htc->doclose, STREAM_CLOSE_MAGIC);
	wrk = ctx->bo->wrk;
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(bp, d->priv, BACKEND_MAGIC);

	/*
	 * Now that we know our backend, we can set a default Host:
	 * header if one is necessary.  This cannot be done in the VCL
	 * because the backend may be chosen by a director.
	 */
	if (!http_GetHdr(bo->bereq, H_Host, NULL) && bp->hosthdr != NULL)
		http_PrintfHeader(bo->bereq, "Host: %s", bp->hosthdr);

	do {
		if (bo->htc != NULL)
			CHECK_OBJ_NOTNULL(bo->htc->doclose, STREAM_CLOSE_MAGIC);
		pfd = vbe_dir_getfd(ctx, wrk, d, bp, extrachance == 0 ? 1 : 0);
		if (pfd == NULL)
			return (-1);
		AN(bo->htc);
		CHECK_OBJ_NOTNULL(bo->htc->doclose, STREAM_CLOSE_MAGIC);
		if (PFD_State(pfd) != PFD_STATE_STOLEN)
			extrachance = 0;

		i = V1F_SendReq(wrk, bo, &bo->acct.bereq_hdrbytes,
		    &bo->acct.bereq_bodybytes);

		if (i == 0 && PFD_State(pfd) != PFD_STATE_USED) {
			if (VCP_Wait(wrk, pfd, VTIM_real() +
			    bo->htc->first_byte_timeout) != 0) {
				bo->htc->doclose = SC_RX_TIMEOUT;
				VSLb(bo->vsl, SLT_FetchError,
				     "first byte timeout (reused connection)");
				extrachance = 0;
			}
		}

		if (bo->htc->doclose == SC_NULL) {
			assert(PFD_State(pfd) == PFD_STATE_USED);
			if (i == 0)
				i = V1F_FetchRespHdr(bo);
			if (i == 0) {
				AN(bo->htc->priv);
				http_VSL_log(bo->beresp);
				return (0);
			}
		}
		CHECK_OBJ_NOTNULL(bo->htc->doclose, STREAM_CLOSE_MAGIC);

		/*
		 * If we recycled a backend connection, there is a finite chance
		 * that the backend closed it before we got the bereq to it.
		 * In that case do a single automatic retry if req.body allows.
		 */
		vbe_dir_finish(ctx, d);
		AZ(bo->htc);
		if (i < 0 || extrachance == 0)
			break;
		if (bo->no_retry != NULL)
			break;
		VSC_C_main->backend_retry++;
	} while (extrachance--);
	return (-1);
}

static VCL_IP v_matchproto_(vdi_getip_f)
vbe_dir_getip(VRT_CTX, VCL_BACKEND d)
{
	struct pfd *pfd;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo->htc, HTTP_CONN_MAGIC);
	pfd = ctx->bo->htc->priv;

	return (VCP_GetIp(pfd));
}

/*--------------------------------------------------------------------*/

static stream_close_t v_matchproto_(vdi_http1pipe_f)
vbe_dir_http1pipe(VRT_CTX, VCL_BACKEND d)
{
	int i;
	stream_close_t retval;
	struct backend *bp;
	struct v1p_acct v1a;
	struct pfd *pfd;
	vtim_real deadline;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	CAST_OBJ_NOTNULL(bp, d->priv, BACKEND_MAGIC);

	memset(&v1a, 0, sizeof v1a);

	/* This is hackish... */
	v1a.req = ctx->req->acct.req_hdrbytes;
	ctx->req->acct.req_hdrbytes = 0;

	ctx->req->res_mode = RES_PIPE;

	retval = SC_TX_ERROR;
	pfd = vbe_dir_getfd(ctx, ctx->req->wrk, d, bp, 0);

	if (pfd != NULL) {
		CHECK_OBJ_NOTNULL(ctx->bo->htc, HTTP_CONN_MAGIC);
		i = V1F_SendReq(ctx->req->wrk, ctx->bo,
		    &v1a.bereq, &v1a.out);
		VSLb_ts_req(ctx->req, "Pipe", W_TIM_real(ctx->req->wrk));
		if (i == 0) {
			deadline = ctx->bo->task_deadline;
			if (isnan(deadline))
				deadline = cache_param->pipe_task_deadline;
			if (deadline > 0.)
				deadline += ctx->req->sp->t_idle;
			retval = V1P_Process(ctx->req, *PFD_Fd(pfd), &v1a,
			    deadline);
		}
		VSLb_ts_req(ctx->req, "PipeSess", W_TIM_real(ctx->req->wrk));
		ctx->bo->htc->doclose = retval;
		vbe_dir_finish(ctx, d);
	}
	V1P_Charge(ctx->req, &v1a, bp->vsc);
	CHECK_OBJ_NOTNULL(retval, STREAM_CLOSE_MAGIC);
	return (retval);
}

/*--------------------------------------------------------------------*/

static void
vbe_dir_event(const struct director *d, enum vcl_event_e ev)
{
	struct backend *bp;

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(bp, d->priv, BACKEND_MAGIC);

	if (ev == VCL_EVENT_WARM) {
		VRT_VSC_Reveal(bp->vsc_seg);
		if (bp->probe != NULL)
			VBP_Control(bp, 1);
	} else if (ev == VCL_EVENT_COLD) {
		if (bp->probe != NULL)
			VBP_Control(bp, 0);
		VRT_VSC_Hide(bp->vsc_seg);
	} else if (ev == VCL_EVENT_DISCARD) {
		VRT_DelDirector(&bp->director);
	}
}

/*---------------------------------------------------------------------*/

static void
vbe_free(struct backend *be)
{

	CHECK_OBJ_NOTNULL(be, BACKEND_MAGIC);

	if (be->probe != NULL)
		VBP_Remove(be);

	VSC_vbe_Destroy(&be->vsc_seg);
	Lck_Lock(&backends_mtx);
	VSC_C_main->n_backend--;
	Lck_Unlock(&backends_mtx);
	VCP_Rel(&be->conn_pool);

#define DA(x)	do { if (be->x != NULL) free(be->x); } while (0)
#define DN(x)	/**/
	VRT_BACKEND_HANDLE();
#undef DA
#undef DN
	free(be->endpoint);

	assert(VTAILQ_EMPTY(&be->cw_head));
	FREE_OBJ(be);
}

static void v_matchproto_(vdi_destroy_f)
vbe_destroy(const struct director *d)
{
	struct backend *be;

	CAST_OBJ_NOTNULL(be, d->priv, BACKEND_MAGIC);
	vbe_free(be);
}

/*--------------------------------------------------------------------*/

static void
vbe_panic(const struct director *d, struct vsb *vsb)
{
	struct backend *bp;

	PAN_CheckMagic(vsb, d, DIRECTOR_MAGIC);
	bp = d->priv;
	PAN_CheckMagic(vsb, bp, BACKEND_MAGIC);

	VCP_Panic(vsb, bp->conn_pool);
	VSB_printf(vsb, "hosthdr = %s,\n", bp->hosthdr);
	VSB_printf(vsb, "n_conn = %u,\n", bp->n_conn);
}

/*--------------------------------------------------------------------
 */

static void v_matchproto_(vdi_list_f)
vbe_list(VRT_CTX, const struct director *d, struct vsb *vsb, int pflag,
    int jflag)
{
	struct backend *bp;

	(void)ctx;

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(bp, d->priv, BACKEND_MAGIC);

	if (bp->probe != NULL)
		VBP_Status(vsb, bp, pflag, jflag);
	else if (jflag && pflag)
		VSB_cat(vsb, "{},\n");
	else if (jflag)
		VSB_cat(vsb, "[0, 0, \"healthy\"]");
	else if (pflag)
		return;
	else
		VSB_cat(vsb, "0/0\thealthy");
}

/*--------------------------------------------------------------------
 */

static VCL_BOOL v_matchproto_(vdi_healthy_f)
vbe_healthy(VRT_CTX, VCL_BACKEND d, VCL_TIME *t)
{
	struct backend *bp;

	(void)ctx;
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(bp, d->priv, BACKEND_MAGIC);

	if (t != NULL)
		*t = bp->changed;

	return (!bp->sick);
}


/*--------------------------------------------------------------------
 */

static const struct vdi_methods vbe_methods[1] = {{
	.magic =		VDI_METHODS_MAGIC,
	.type =			"backend",
	.http1pipe =		vbe_dir_http1pipe,
	.gethdrs =		vbe_dir_gethdrs,
	.getip =		vbe_dir_getip,
	.finish =		vbe_dir_finish,
	.event =		vbe_dir_event,
	.destroy =		vbe_destroy,
	.panic =		vbe_panic,
	.list =			vbe_list,
	.healthy =		vbe_healthy
}};

static const struct vdi_methods vbe_methods_noprobe[1] = {{
	.magic =		VDI_METHODS_MAGIC,
	.type =			"backend",
	.http1pipe =		vbe_dir_http1pipe,
	.gethdrs =		vbe_dir_gethdrs,
	.getip =		vbe_dir_getip,
	.finish =		vbe_dir_finish,
	.event =		vbe_dir_event,
	.destroy =		vbe_destroy,
	.panic =		vbe_panic,
	.list =			vbe_list
}};

/*--------------------------------------------------------------------
 * Create a new static or dynamic director::backend instance.
 */

size_t
VRT_backend_vsm_need(VRT_CTX)
{
	(void)ctx;
	return (VRT_VSC_Overhead(VSC_vbe_size));
}

/*
 * The new_backend via parameter is a VCL_BACKEND, but we need a (struct
 * backend)
 *
 * For now, we resolve it when creating the backend, which implies no redundancy
 * / load balancing across the via director if it is more than a simple backend.
 */

static const struct backend *
via_resolve(VRT_CTX, const struct vrt_endpoint *vep, VCL_BACKEND via)
{
	const struct backend *viabe = NULL;

	CHECK_OBJ_NOTNULL(vep, VRT_ENDPOINT_MAGIC);
	CHECK_OBJ_NOTNULL(via, DIRECTOR_MAGIC);

	if (vep->uds_path) {
		VRT_fail(ctx, "Via is only supported for IP addresses");
		return (NULL);
	}

	via = VRT_DirectorResolve(ctx, via);

	if (via == NULL) {
		VRT_fail(ctx, "Via resolution failed");
		return (NULL);
	}

	CHECK_OBJ(via, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(via->vdir, VCLDIR_MAGIC);

	if (via->vdir->methods == vbe_methods ||
	    via->vdir->methods == vbe_methods_noprobe)
		CAST_OBJ_NOTNULL(viabe, via->priv, BACKEND_MAGIC);

	if (viabe == NULL)
		VRT_fail(ctx, "Via does not resolve to a backend");

	return (viabe);
}

/*
 * construct a new endpoint identical to vep with sa in a proxy header
 */
static struct vrt_endpoint *
via_endpoint(const struct vrt_endpoint *vep, const struct suckaddr *sa,
    const char *auth)
{
	struct vsb *preamble;
	struct vrt_blob blob[1];
	struct vrt_endpoint *nvep, *ret;
	const struct suckaddr *client_bogo;

	CHECK_OBJ_NOTNULL(vep, VRT_ENDPOINT_MAGIC);
	AN(sa);

	nvep = VRT_Endpoint_Clone(vep);
	CHECK_OBJ_NOTNULL(nvep, VRT_ENDPOINT_MAGIC);

	if (VSA_Get_Proto(sa) == AF_INET6)
		client_bogo = bogo_ip6;
	else
		client_bogo = bogo_ip;

	preamble = VSB_new_auto();
	AN(preamble);
	VPX_Format_Proxy(preamble, 2, client_bogo, sa, auth);
	blob->blob = VSB_data(preamble);
	blob->len = VSB_len(preamble);
	nvep->preamble = blob;
	ret = VRT_Endpoint_Clone(nvep);
	CHECK_OBJ_NOTNULL(ret, VRT_ENDPOINT_MAGIC);
	VSB_destroy(&preamble);
	FREE_OBJ(nvep);

	return (ret);
}

VCL_BACKEND
VRT_new_backend_clustered(VRT_CTX, struct vsmw_cluster *vc,
    const struct vrt_backend *vrt, VCL_BACKEND via)
{
	struct backend *be;
	struct vcl *vcl;
	const struct vrt_backend_probe *vbp;
	const struct vrt_endpoint *vep;
	const struct vdi_methods *m;
	const struct suckaddr *sa = NULL;
	char abuf[VTCP_ADDRBUFSIZE];
	const struct backend *viabe = NULL;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vrt, VRT_BACKEND_MAGIC);
	vep = vrt->endpoint;
	CHECK_OBJ_NOTNULL(vep, VRT_ENDPOINT_MAGIC);
	if (vep->uds_path == NULL) {
		if (vep->ipv4 == NULL && vep->ipv6 == NULL) {
			VRT_fail(ctx, "%s: Illegal IP", __func__);
			return (NULL);
		}
	} else {
		assert(vep->ipv4== NULL && vep->ipv6== NULL);
	}

	if (via != NULL) {
		viabe = via_resolve(ctx, vep, via);
		if (viabe == NULL)
			return (NULL);
	}

	vcl = ctx->vcl;
	AN(vcl);
	AN(vrt->vcl_name);

	/* Create new backend */
	ALLOC_OBJ(be, BACKEND_MAGIC);
	if (be == NULL)
		return (NULL);
	VTAILQ_INIT(&be->cw_head);

#define DA(x)	do { if (vrt->x != NULL) REPLACE((be->x), (vrt->x)); } while (0)
#define DN(x)	do { be->x = vrt->x; } while (0)
	VRT_BACKEND_HANDLE();
#undef DA
#undef DN

#define CPTMO(a, b, x) do {				\
		if ((a)->x < 0.0 || isnan((a)->x))	\
			(a)->x = (b)->x;		\
	} while(0)

	if (viabe != NULL) {
		CPTMO(be, viabe, connect_timeout);
		CPTMO(be, viabe, first_byte_timeout);
		CPTMO(be, viabe, between_bytes_timeout);
	}
#undef CPTMO

	if (viabe || be->hosthdr == NULL) {
		if (vrt->endpoint->uds_path != NULL)
			sa = bogo_ip;
		else if (cache_param->prefer_ipv6 && vep->ipv6 != NULL)
			sa = vep->ipv6;
		else if (vep->ipv4!= NULL)
			sa = vep->ipv4;
		else
			sa = vep->ipv6;
		if (be->hosthdr == NULL) {
			VTCP_name(sa, abuf, sizeof abuf, NULL, 0);
			REPLACE(be->hosthdr, abuf);
		}
	}

	be->vsc = VSC_vbe_New(vc, &be->vsc_seg,
	    "%s.%s", VCL_Name(ctx->vcl), vrt->vcl_name);
	AN(be->vsc);
	if (! vcl->temp->is_warm)
		VRT_VSC_Hide(be->vsc_seg);

	if (viabe)
		vep = be->endpoint = via_endpoint(viabe->endpoint, sa,
		    be->authority);
	else
		vep = be->endpoint = VRT_Endpoint_Clone(vep);

	AN(vep);
	be->conn_pool = VCP_Ref(vep, vbe_proto_ident);
	AN(be->conn_pool);

	vbp = vrt->probe;
	if (vbp == NULL)
		vbp = VCL_DefaultProbe(vcl);

	if (vbp != NULL) {
		VBP_Insert(be, vbp, be->conn_pool);
		m = vbe_methods;
	} else {
		be->sick = 0;
		m = vbe_methods_noprobe;
	}

	Lck_Lock(&backends_mtx);
	VSC_C_main->n_backend++;
	Lck_Unlock(&backends_mtx);

	be->director = VRT_AddDirector(ctx, m, be, "%s", vrt->vcl_name);

	if (be->director == NULL) {
		vbe_free(be);
		return (NULL);
	}
	/* for cold VCL, update initial director state */
	if (be->probe != NULL)
		VBP_Update_Backend(be->probe);
	return (be->director);
}

VCL_BACKEND
VRT_new_backend(VRT_CTX, const struct vrt_backend *vrt, VCL_BACKEND via)
{

	CHECK_OBJ_NOTNULL(vrt, VRT_BACKEND_MAGIC);
	CHECK_OBJ_NOTNULL(vrt->endpoint, VRT_ENDPOINT_MAGIC);
	return (VRT_new_backend_clustered(ctx, NULL, vrt, via));
}

/*--------------------------------------------------------------------
 * Delete a dynamic director::backend instance.  Undeleted dynamic and
 * static instances are GC'ed when the VCL is discarded (in cache_vcl.c)
 */

void
VRT_delete_backend(VRT_CTX, VCL_BACKEND *dp)
{

	(void)ctx;
	CHECK_OBJ_NOTNULL(*dp, DIRECTOR_MAGIC);
	VRT_DisableDirector(*dp);
	VRT_Assign_Backend(dp, NULL);
}

/*---------------------------------------------------------------------*/

void
VBE_InitCfg(void)
{

	Lck_New(&backends_mtx, lck_vbe);
}
