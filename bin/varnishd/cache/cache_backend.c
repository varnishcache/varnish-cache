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

#include "vsa.h"
#include "vtcp.h"
#include "vtim.h"

#include "cache_backend.h"
#include "cache_conn_pool.h"
#include "cache_transport.h"
#include "cache_vcl.h"
#include "http1/cache_http1.h"

#include "VSC_vbe.h"

/*--------------------------------------------------------------------*/

static const char * const vbe_proto_ident = "HTTP Backend";

static VTAILQ_HEAD(, backend) backends = VTAILQ_HEAD_INITIALIZER(backends);
static VTAILQ_HEAD(, backend) cool_backends =
    VTAILQ_HEAD_INITIALIZER(cool_backends);
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
		if (dst == 0.0)						\
			dst = be->tmx;					\
		if (dst == 0.0)						\
			dst = cache_param->tmx;				\
	} while (0)

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

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	bo = ctx->bo;
	CHECK_OBJ_NOTNULL(bp, BACKEND_MAGIC);
	AN(bp->vsc);

	if (! VRT_Healthy(ctx, dir, NULL)) {
		VSLb(bo->vsl, SLT_FetchError,
		     "backend %s: unhealthy", VRT_BACKEND_string(dir));
		bp->vsc->unhealthy++;
		VSC_C_main->backend_unhealthy++;
		return (NULL);
	}

	if (bp->max_connections > 0 && bp->n_conn >= bp->max_connections) {
		VSLb(bo->vsl, SLT_FetchError,
		     "backend %s: busy", VRT_BACKEND_string(dir));
		bp->vsc->busy++;
		VSC_C_main->backend_busy++;
		return (NULL);
	}

	AZ(bo->htc);
	bo->htc = WS_Alloc(bo->ws, sizeof *bo->htc);
	if (bo->htc == NULL) {
		VSLb(bo->vsl, SLT_FetchError, "out of workspace");
		/* XXX: counter ? */
		return (NULL);
	}
	bo->htc->doclose = SC_NULL;

	FIND_TMO(connect_timeout, tmod, bo, bp);
	pfd = VCP_Get(bp->conn_pool, tmod, wrk, force_fresh, &err);
	if (pfd == NULL) {
		VBE_Connect_Error(bp->vsc, err);
		VSLb(bo->vsl, SLT_FetchError,
		     "backend %s: fail errno %d (%s)",
		     VRT_BACKEND_string(dir), err, vstrerror(err));
		VSC_C_main->backend_fail++;
		bo->htc = NULL;
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
	Lck_Unlock(bp->director->mtx);

	err = 0;
	if (bp->proxy_header != 0)
		err += VPX_Send_Proxy(*fdp, bp->proxy_header, bo->sp);
	if (err < 0) {
		VSLb(bo->vsl, SLT_FetchError,
		     "backend %s: proxy write errno %d (%s)",
		     VRT_BACKEND_string(dir),
		     errno, vstrerror(errno));
		// account as if connect failed - good idea?
		VSC_C_main->backend_fail++;
		bo->htc = NULL;
		VCP_Close(&pfd);
		AZ(pfd);
		Lck_Lock(bp->director->mtx);
		bp->n_conn--;
		bp->vsc->conn--;
		bp->vsc->req--;
		Lck_Unlock(bp->director->mtx);
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
	FIND_TMO(first_byte_timeout,
	    bo->htc->first_byte_timeout, bo, bp);
	FIND_TMO(between_bytes_timeout,
	    bo->htc->between_bytes_timeout, bo, bp);
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
	pfd = bo->htc->priv;
	bo->htc->priv = NULL;
	if (PFD_State(pfd) != PFD_STATE_USED) {
		AN(bo->htc->doclose);
		if (bo->htc->doclose != SC_TX_PIPE) {
#define SESS_CLOSE(U, l, err, desc)				\
			if (bo->htc->doclose == SC_ ## U)	\
				AN(err);
#include "tbl/sess_close.h"
		}
	}
	if (bo->htc->doclose != SC_NULL || bp->proxy_header != 0) {
		VSLb(bo->vsl, SLT_BackendClose, "%d %s close", *PFD_Fd(pfd),
		    VRT_BACKEND_string(d));
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
		pfd = vbe_dir_getfd(ctx, wrk, d, bp, extrachance == 0 ? 1 : 0);
		if (pfd == NULL)
			return (-1);
		AN(bo->htc);
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
				return (0);
			}
		}

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

static enum sess_close v_matchproto_(vdi_http1pipe_f)
vbe_dir_http1pipe(VRT_CTX, VCL_BACKEND d)
{
	int i;
	enum sess_close retval;
	struct backend *bp;
	struct v1p_acct v1a;
	struct pfd *pfd;

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

	pfd = vbe_dir_getfd(ctx, ctx->req->wrk, d, bp, 0);

	if (pfd == NULL) {
		retval = SC_TX_ERROR;
	} else {
		CHECK_OBJ_NOTNULL(ctx->bo->htc, HTTP_CONN_MAGIC);
		i = V1F_SendReq(ctx->req->wrk, ctx->bo,
		    &v1a.bereq, &v1a.out);
		VSLb_ts_req(ctx->req, "Pipe", W_TIM_real(ctx->req->wrk));
		if (i == 0)
			V1P_Process(ctx->req, *PFD_Fd(pfd), &v1a);
		VSLb_ts_req(ctx->req, "PipeSess", W_TIM_real(ctx->req->wrk));
		ctx->bo->htc->doclose = SC_TX_PIPE;
		vbe_dir_finish(ctx, d);
		retval = SC_TX_PIPE;
	}
	V1P_Charge(ctx->req, &v1a, bp->vsc);
	return (retval);
}

/*--------------------------------------------------------------------*/

static void
vbe_dir_event(const struct director *d, enum vcl_event_e ev)
{
	struct backend *bp;

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(bp, d->priv, BACKEND_MAGIC);

	if (ev == VCL_EVENT_WARM)
		VRT_VSC_Reveal(bp->vsc_seg);

	if (bp->probe != NULL && ev == VCL_EVENT_WARM)
		VBP_Control(bp, 1);

	if (bp->probe != NULL && ev == VCL_EVENT_COLD)
		VBP_Control(bp, 0);

	if (ev == VCL_EVENT_COLD)
		VRT_VSC_Hide(bp->vsc_seg);
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
	if (be->cooled > 0)
		VTAILQ_REMOVE(&cool_backends, be, list);
	else
		VTAILQ_REMOVE(&backends, be, list);
	VSC_C_main->n_backend--;
	VCP_Rel(&be->conn_pool);
	Lck_Unlock(&backends_mtx);

#define DA(x)	do { if (be->x != NULL) free(be->x); } while (0)
#define DN(x)	/**/
	VRT_BACKEND_HANDLE();
#undef DA
#undef DN
	free(be->endpoint);

	Lck_Delete(be->director->mtx);
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

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(bp, d->priv, BACKEND_MAGIC);

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

VCL_BACKEND
VRT_new_backend_clustered(VRT_CTX, struct vsmw_cluster *vc,
    const struct vrt_backend *vrt)
{
	struct backend *be;
	struct vcl *vcl;
	const struct vrt_backend_probe *vbp;
	const struct vrt_endpoint *vep;
	const struct vdi_methods *m;
	const struct suckaddr *sa;
	char abuf[VTCP_ADDRBUFSIZE];

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

	vcl = ctx->vcl;
	AN(vcl);
	AN(vrt->vcl_name);

	/* Create new backend */
	ALLOC_OBJ(be, BACKEND_MAGIC);
	if (be == NULL)
		return (NULL);

	vep = be->endpoint = VRT_Endpoint_Clone(vep);
#define DA(x)	do { if (vrt->x != NULL) REPLACE((be->x), (vrt->x)); } while (0)
#define DN(x)	do { be->x = vrt->x; } while (0)
	VRT_BACKEND_HANDLE();
#undef DA
#undef DN

	if (be->hosthdr == NULL) {
		if (vrt->endpoint->uds_path != NULL)
			sa = bogo_ip;
		else if (cache_param->prefer_ipv6 && vep->ipv6 != NULL)
			sa = vep->ipv6;
		else if (vep->ipv4!= NULL)
			sa = vep->ipv4;
		else
			sa = vep->ipv6;
		VTCP_name(sa, abuf, sizeof abuf, NULL, 0);
		REPLACE(be->hosthdr, abuf);
	}

	be->vsc = VSC_vbe_New(vc, &be->vsc_seg,
	    "%s.%s", VCL_Name(ctx->vcl), vrt->vcl_name);
	AN(be->vsc);
	if (! vcl->temp->is_warm)
		VRT_VSC_Hide(be->vsc_seg);

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
	VTAILQ_INSERT_TAIL(&backends, be, list);
	VSC_C_main->n_backend++;
	Lck_Unlock(&backends_mtx);

	be->director = VRT_AddDirector(ctx, m, be, "%s", vrt->vcl_name);

	if (be->director != NULL) {
		/* for cold VCL, update initial director state */
		if (be->probe != NULL && ! vcl->temp->is_warm)
			VBP_Update_Backend(be->probe);
		return (be->director);
	}

	vbe_free(be);
	return (NULL);
}

VCL_BACKEND
VRT_new_backend(VRT_CTX, const struct vrt_backend *vrt)
{
	CHECK_OBJ_NOTNULL(vrt->endpoint, VRT_ENDPOINT_MAGIC);
	return (VRT_new_backend_clustered(ctx, NULL, vrt));
}

/*--------------------------------------------------------------------
 * Delete a dynamic director::backend instance.  Undeleted dynamic and
 * static instances are GC'ed when the VCL is discarded (in cache_vcl.c)
 */

void
VRT_delete_backend(VRT_CTX, VCL_BACKEND *dp)
{
	VCL_BACKEND d;
	struct backend *be;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	TAKE_OBJ_NOTNULL(d, dp, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(be, d->priv, BACKEND_MAGIC);
	Lck_Lock(be->director->mtx);
	VRT_DisableDirector(be->director);
	Lck_Unlock(be->director->mtx);
	Lck_Lock(&backends_mtx);
	AZ(be->cooled);
	be->cooled = VTIM_real() + 60.;
	VTAILQ_REMOVE(&backends, be, list);
	VTAILQ_INSERT_TAIL(&cool_backends, be, list);
	Lck_Unlock(&backends_mtx);

	// NB. The backend is still usable for the ongoing transactions,
	// this is why we don't bust the director's magic number.
}

void
VBE_SetHappy(const struct backend *be, uint64_t happy)
{

	CHECK_OBJ_NOTNULL(be, BACKEND_MAGIC);
	Lck_Lock(&backends_mtx);
	if (be->vsc != NULL)
		be->vsc->happy = happy;
	Lck_Unlock(&backends_mtx);
}

/*---------------------------------------------------------------------*/

void
VBE_Poll(void)
{
	struct backend *be, *be2;
	vtim_real now = VTIM_real();

	ASSERT_CLI();
	Lck_Lock(&backends_mtx);
	VTAILQ_FOREACH_SAFE(be, &cool_backends, list, be2) {
		CHECK_OBJ_NOTNULL(be, BACKEND_MAGIC);
		if (be->cooled > now)
			break;
		if (be->n_conn > 0)
			continue;
		Lck_Unlock(&backends_mtx);
		VRT_DelDirector(&be->director);
		Lck_Lock(&backends_mtx);
	}
	Lck_Unlock(&backends_mtx);
}

/*---------------------------------------------------------------------*/

void
VBE_InitCfg(void)
{

	Lck_New(&backends_mtx, lck_vbe);
}
