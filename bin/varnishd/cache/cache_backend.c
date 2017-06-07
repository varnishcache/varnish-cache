/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 * The director implementation for VCL backends.
 *
 */

#include "config.h"

#include <stdlib.h>

#include "cache.h"

#include "vrt.h"
#include "vtcp.h"
#include "vtim.h"

#include "cache_director.h"
#include "cache_backend.h"
#include "cache_transport.h"
#include "http1/cache_http1.h"

#include "VSC_vbe.h"

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
 */

static struct vbc *
vbe_dir_getfd(struct worker *wrk, struct backend *bp, struct busyobj *bo)
{
	struct vbc *vbc;
	double tmod;
	char abuf1[VTCP_ADDRBUFSIZE], abuf2[VTCP_ADDRBUFSIZE];
	char pbuf1[VTCP_PORTBUFSIZE], pbuf2[VTCP_PORTBUFSIZE];

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bp, BACKEND_MAGIC);
	AN(bp->vsc);

	if (!VBE_Healthy(bp, NULL)) {
		// XXX: per backend stats ?
		VSC_C_main->backend_unhealthy++;
		return (NULL);
	}

	if (bp->max_connections > 0 && bp->n_conn >= bp->max_connections) {
		// XXX: per backend stats ?
		VSC_C_main->backend_busy++;
		return (NULL);
	}

	AZ(bo->htc);
	bo->htc = WS_Alloc(bo->ws, sizeof *bo->htc);
	if (bo->htc == NULL)
		/* XXX: counter ? */
		return (NULL);
	bo->htc->doclose = SC_NULL;

	FIND_TMO(connect_timeout, tmod, bo, bp);
	vbc = VBT_Get(bp->tcp_pool, tmod, bp, wrk);
	if (vbc == NULL) {
		// XXX: Per backend stats ?
		VSC_C_main->backend_fail++;
		bo->htc = NULL;
		return (NULL);
	}

	assert(vbc->fd >= 0);
	AN(vbc->addr);

	Lck_Lock(&bp->mtx);
	bp->n_conn++;
	bp->vsc->conn++;
	bp->vsc->req++;
	Lck_Unlock(&bp->mtx);

	if (bp->proxy_header != 0)
		VPX_Send_Proxy(vbc->fd, bp->proxy_header, bo->sp);

	VTCP_myname(vbc->fd, abuf1, sizeof abuf1, pbuf1, sizeof pbuf1);
	VTCP_hisname(vbc->fd, abuf2, sizeof abuf2, pbuf2, sizeof pbuf2);
	VSLb(bo->vsl, SLT_BackendOpen, "%d %s %s %s %s %s",
	    vbc->fd, bp->display_name, abuf2, pbuf2, abuf1, pbuf1);

	INIT_OBJ(bo->htc, HTTP_CONN_MAGIC);
	bo->htc->priv = vbc;
	bo->htc->rfd = &vbc->fd;
	FIND_TMO(first_byte_timeout,
	    bo->htc->first_byte_timeout, bo, bp);
	FIND_TMO(between_bytes_timeout,
	    bo->htc->between_bytes_timeout, bo, bp);
	return (vbc);
}

static unsigned __match_proto__(vdi_healthy_f)
vbe_dir_healthy(const struct director *d, const struct busyobj *bo,
    double *changed)
{
	struct backend *be;

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CHECK_OBJ_ORNULL(bo, BUSYOBJ_MAGIC);
	CAST_OBJ_NOTNULL(be, d->priv, BACKEND_MAGIC);
	return (VBE_Healthy(be, changed));
}

static void __match_proto__(vdi_finish_f)
vbe_dir_finish(const struct director *d, struct worker *wrk,
    struct busyobj *bo)
{
	struct backend *bp;
	struct vbc *vbc;

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CAST_OBJ_NOTNULL(bp, d->priv, BACKEND_MAGIC);

	CHECK_OBJ_NOTNULL(bo->htc, HTTP_CONN_MAGIC);
	CAST_OBJ_NOTNULL(vbc, bo->htc->priv, VBC_MAGIC);
	bo->htc->priv = NULL;
	if (vbc->state != VBC_STATE_USED)
		VBT_Wait(wrk, vbc);
	if (bo->htc->doclose != SC_NULL || bp->proxy_header != 0) {
		VSLb(bo->vsl, SLT_BackendClose, "%d %s", vbc->fd,
		    bp->display_name);
		VBT_Close(bp->tcp_pool, &vbc);
		Lck_Lock(&bp->mtx);
	} else {
		VSLb(bo->vsl, SLT_BackendReuse, "%d %s", vbc->fd,
		    bp->display_name);
		Lck_Lock(&bp->mtx);
		VSC_C_main->backend_recycle++;
		VBT_Recycle(wrk, bp->tcp_pool, &vbc);
	}
	assert(bp->n_conn > 0);
	bp->n_conn--;
	bp->vsc->conn--;
#define ACCT(foo)	bp->vsc->foo += bo->acct.foo;
#include "tbl/acct_fields_bereq.h"
	Lck_Unlock(&bp->mtx);
	bo->htc = NULL;
}

static int __match_proto__(vdi_gethdrs_f)
vbe_dir_gethdrs(const struct director *d, struct worker *wrk,
    struct busyobj *bo)
{
	int i, extrachance = 1;
	struct backend *bp;
	struct vbc *vbc;

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CAST_OBJ_NOTNULL(bp, d->priv, BACKEND_MAGIC);

	/*
	 * Now that we know our backend, we can set a default Host:
	 * header if one is necessary.  This cannot be done in the VCL
	 * because the backend may be chosen by a director.
	 */
	if (!http_GetHdr(bo->bereq, H_Host, NULL) && bp->hosthdr != NULL)
		http_PrintfHeader(bo->bereq, "Host: %s", bp->hosthdr);

	do {
		vbc = vbe_dir_getfd(wrk, bp, bo);
		if (vbc == NULL) {
			VSLb(bo->vsl, SLT_FetchError, "no backend connection");
			return (-1);
		}
		AN(bo->htc);
		if (vbc->state != VBC_STATE_STOLEN)
			extrachance = 0;

		i = V1F_SendReq(wrk, bo, &bo->acct.bereq_hdrbytes, 0);

		if (vbc->state != VBC_STATE_USED)
			VBT_Wait(wrk, vbc);

		assert(vbc->state == VBC_STATE_USED);

		if (i == 0)
			i = V1F_FetchRespHdr(bo);
		if (i == 0) {
			AN(bo->htc->priv);
			return (0);
		}

		/*
		 * If we recycled a backend connection, there is a finite chance
		 * that the backend closed it before we got the bereq to it.
		 * In that case do a single automatic retry if req.body allows.
		 */
		vbe_dir_finish(d, wrk, bo);
		AZ(bo->htc);
		if (i < 0)
			break;
		if (bo->req != NULL &&
		    bo->req->req_body_status != REQ_BODY_NONE &&
		    bo->req->req_body_status != REQ_BODY_CACHED)
			break;
		VSC_C_main->backend_retry++;
	} while (extrachance);
	return (-1);
}

static int __match_proto__(vdi_getbody_f)
vbe_dir_getbody(const struct director *d, struct worker *wrk,
    struct busyobj *bo)
{

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->vfc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(bo->htc, HTTP_CONN_MAGIC);

	return (V1F_Setup_Fetch(bo->vfc, bo->htc));
}

static const struct suckaddr * __match_proto__(vdi_getip_f)
vbe_dir_getip(const struct director *d, struct worker *wrk,
    struct busyobj *bo)
{
	struct vbc *vbc;

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->htc, HTTP_CONN_MAGIC);
	CAST_OBJ_NOTNULL(vbc, bo->htc->priv, VBC_MAGIC);

	return (vbc->addr);
}

/*--------------------------------------------------------------------*/

static enum sess_close
vbe_dir_http1pipe(const struct director *d, struct req *req, struct busyobj *bo)
{
	int i;
	enum sess_close retval;
	struct backend *bp;
	struct v1p_acct v1a;
	struct vbc *vbc;

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CAST_OBJ_NOTNULL(bp, d->priv, BACKEND_MAGIC);

	memset(&v1a, 0, sizeof v1a);

	/* This is hackish... */
	v1a.req = req->acct.req_hdrbytes;
	req->acct.req_hdrbytes = 0;

	req->res_mode = RES_PIPE;

	vbc = vbe_dir_getfd(req->wrk, bp, bo);

	if (vbc == NULL) {
		VSLb(bo->vsl, SLT_FetchError, "no backend connection");
		retval = SC_TX_ERROR;
	} else {
		i = V1F_SendReq(req->wrk, bo, &v1a.bereq, 1);
		VSLb_ts_req(req, "Pipe", W_TIM_real(req->wrk));
		if (i == 0)
			V1P_Process(req, vbc->fd, &v1a);
		VSLb_ts_req(req, "PipeSess", W_TIM_real(req->wrk));
		bo->htc->doclose = SC_TX_PIPE;
		vbe_dir_finish(d, req->wrk, bo);
		retval = SC_TX_PIPE;
	}
	V1P_Charge(req, &v1a, bp->vsc);
	return (retval);
}

/*--------------------------------------------------------------------*/

static void
vbe_panic(const struct director *d, struct vsb *vsb)
{
	struct backend *bp;

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(bp, d->priv, BACKEND_MAGIC);

	VSB_printf(vsb, "display_name = %s,\n", bp->display_name);
	if (bp->ipv4_addr != NULL)
		VSB_printf(vsb, "ipv4 = %s,\n", bp->ipv4_addr);
	if (bp->ipv6_addr != NULL)
		VSB_printf(vsb, "ipv6 = %s,\n", bp->ipv6_addr);
	VSB_printf(vsb, "port = %s,\n", bp->port);
	VSB_printf(vsb, "hosthdr = %s,\n", bp->hosthdr);
	VSB_printf(vsb, "health = %s,\n",
	    bp->healthy ? "healthy" : "sick");
	VSB_printf(vsb, "admin_health = %s, changed = %f,\n",
	    bp->admin_health, bp->health_changed);
	VSB_printf(vsb, "n_conn = %u,\n", bp->n_conn);
}

/*--------------------------------------------------------------------*/

void
VBE_fill_director(struct backend *be)
{
	struct director *d;

	CHECK_OBJ_NOTNULL(be, BACKEND_MAGIC);

	INIT_OBJ(be->director, DIRECTOR_MAGIC);
	d = be->director;
	d->priv = be;
	d->name = "backend";
	d->vcl_name = be->vcl_name;
	d->http1pipe = vbe_dir_http1pipe;
	d->healthy = vbe_dir_healthy;
	d->gethdrs = vbe_dir_gethdrs;
	d->getbody = vbe_dir_getbody;
	d->getip = vbe_dir_getip;
	d->finish = vbe_dir_finish;
	d->panic = vbe_panic;
}
