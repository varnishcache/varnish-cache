/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache.h"

#include "hash/hash_slinger.h"

#include "cache_backend.h"
#include "vcli_priv.h"
#include "vtcp.h"
#include "vtim.h"

/*--------------------------------------------------------------------*/

static enum vfp_status __match_proto__(vfp_pull_f)
v1f_pull_straight(struct vfp_ctx *vc, struct vfp_entry *vfe, void *p,
    ssize_t *lp)
{
	ssize_t l, lr;
	struct http_conn *htc;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	CAST_OBJ_NOTNULL(htc, vfe->priv1, HTTP_CONN_MAGIC);
	AN(p);
	AN(lp);

	l = *lp;
	*lp = 0;

	if (vfe->priv2 == 0) // XXX: Optimize Content-Len: 0 out earlier
		return (VFP_END);
	if (vfe->priv2 < l)
		l = vfe->priv2;
	lr = HTTP1_Read(htc, p, l);
	vc->bodybytes += lr;
	if (lr <= 0)
		return (VFP_Error(vc, "straight insufficient bytes"));
	*lp = lr;
	vfe->priv2 -= lr;
	if (vfe->priv2 == 0)
		return (VFP_END);
	return (VFP_OK);
}

static const struct vfp v1f_straight = {
	.name = "V1F_STRAIGHT",
	.pull = v1f_pull_straight,
};

/*--------------------------------------------------------------------
 * Read a chunked HTTP object.
 *
 * XXX: Reading one byte at a time is pretty pessimal.
 */

static enum vfp_status __match_proto__(vfp_pull_f)
v1f_pull_chunked(struct vfp_ctx *vc, struct vfp_entry *vfe, void *p,
    ssize_t *lp)
{
	const char *err;
	struct http_conn *htc;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	CAST_OBJ_NOTNULL(htc, vfe->priv1, HTTP_CONN_MAGIC);
	AN(p);
	AN(lp);

	switch (HTTP1_Chunked(htc, &vfe->priv2, &err, &vc->bodybytes, p, lp)) {
	case H1CR_ERROR:
		return (VFP_Error(vc, "%s", err));
	case H1CR_MORE:
		return (VFP_OK);
	case H1CR_END:
		return (VFP_END);
	default:
		WRONG("invalid HTTP1_Chunked return");
	}
}

static const struct vfp v1f_chunked = {
	.name = "V1F_CHUNKED",
	.pull = v1f_pull_chunked,
};

/*--------------------------------------------------------------------*/

static enum vfp_status __match_proto__(vfp_pull_f)
v1f_pull_eof(struct vfp_ctx *vc, struct vfp_entry *vfe, void *p,
    ssize_t *lp)
{
	ssize_t l, lr;
	struct http_conn *htc;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	CAST_OBJ_NOTNULL(htc, vfe->priv1, HTTP_CONN_MAGIC);
	AN(p);

	AN(lp);

	l = *lp;
	*lp = 0;
	lr = HTTP1_Read(htc, p, l);
	if (lr < 0)
		return (VFP_Error(vc, "eof socket fail"));
	if (lr == 0)
		return (VFP_END);
	*lp = lr;
	vc->bodybytes += lr;
	return (VFP_OK);
}

static const struct vfp v1f_eof = {
	.name = "V1F_EOF",
	.pull = v1f_pull_eof,
};

/*--------------------------------------------------------------------
 */

void
V1F_Setup_Fetch(struct vfp_ctx *vfc, struct http_conn *htc)
{
	struct vfp_entry *vfe;

	CHECK_OBJ_NOTNULL(vfc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);

	switch(htc->body_status) {
	case BS_EOF:
		assert(htc->content_length == -1);
		vfe = VFP_Push(vfc, &v1f_eof, 0);
		vfe->priv2 = 0;
		break;
	case BS_LENGTH:
		assert(htc->content_length > 0);
		vfe = VFP_Push(vfc, &v1f_straight, 0);
		vfe->priv2 = htc->content_length;
		break;
	case BS_CHUNKED:
		assert(htc->content_length == -1);
		vfe = VFP_Push(vfc, &v1f_chunked, 0);
		vfe->priv2 = -1;
		break;
	default:
		WRONG("Wrong body_status");
		break;
	}
	vfe->priv1 = htc;
}

/*--------------------------------------------------------------------
 * Pass the request body to the backend with chunks
 */

static int __match_proto__(req_body_iter_f)
vbf_iter_req_body_chunked(struct req *req, void *priv, void *ptr, size_t l)
{
	struct worker *wrk;
	char buf[20];

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CAST_OBJ_NOTNULL(wrk, priv, WORKER_MAGIC);

	if (l > 0) {
		bprintf(buf, "%jx\r\n", (uintmax_t)l);
		VSLb(req->vsl, SLT_Debug, "WWWW: %s", buf);
		(void)WRW_Write(wrk, buf, strlen(buf));
		(void)WRW_Write(wrk, ptr, l);
		(void)WRW_Write(wrk, "\r\n", 2);
		if (WRW_Flush(wrk))
			return (-1);
	}
	return (0);
}

/*--------------------------------------------------------------------
 * Pass the request body to the backend
 */

static int __match_proto__(req_body_iter_f)
vbf_iter_req_body(struct req *req, void *priv, void *ptr, size_t l)
{
	struct worker *wrk;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CAST_OBJ_NOTNULL(wrk, priv, WORKER_MAGIC);

	if (l > 0) {
		(void)WRW_Write(wrk, ptr, l);
		if (WRW_Flush(wrk))
			return (-1);
	}
	return (0);
}

/*--------------------------------------------------------------------
 * Send request, and receive the HTTP protocol response, but not the
 * response body.
 *
 * Return value:
 *	-1 failure, not retryable
 *	 0 success
 *	 1 failure which can be retried.
 */

int
V1F_fetch_hdr(struct worker *wrk, struct busyobj *bo, struct req *req)
{
	struct vbc *vc;
	struct http *hp;
	enum http1_status_e hs;
	int retry = -1;
	int i, j, first;
	struct http_conn *htc;
	ssize_t hdrbytes;
	int do_chunked = 0;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_ORNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	htc = bo->htc;

	if (bo->director == NULL) {
		VSLb(bo->vsl, SLT_FetchError, "No backend");
		return (-1);
	}
	AN(bo->director);

	hp = bo->bereq;

	bo->vbc = VDI_GetFd(bo);
	if (bo->vbc == NULL) {
		VSLb(bo->vsl, SLT_FetchError, "no backend connection");
		return (-1);
	}
	vc = bo->vbc;
	if (vc->recycled)
		retry = 1;

	/*
	 * Now that we know our backend, we can set a default Host:
	 * header if one is necessary.  This cannot be done in the VCL
	 * because the backend may be chosen by a director.
	 */
	if (!http_GetHdr(bo->bereq, H_Host, NULL))
		VDI_AddHostHeader(bo->bereq, vc);

	if (req != NULL && req->req_body_status == REQ_BODY_CHUNKED) {
		http_PrintfHeader(hp, "Transfer-Encoding: chunked");
		do_chunked = 1;
	}

	(void)VTCP_blocking(vc->fd);	/* XXX: we should timeout instead */
	WRW_Reserve(wrk, &vc->fd, bo->vsl, bo->t_prev);
	hdrbytes = HTTP1_Write(wrk, hp, HTTP1_Req);

	/* Deal with any message-body the request might (still) have */
	i = 0;

	if (req != NULL) {
		if (do_chunked) {
			i = HTTP1_IterateReqBody(req,
			    vbf_iter_req_body_chunked, wrk);
			(void)WRW_Write(wrk, "0\r\n\r\n", 5);
		} else {
			i = HTTP1_IterateReqBody(req, vbf_iter_req_body, wrk);
		}
		if (req->req_body_status == REQ_BODY_DONE) {
			retry = -1;
		} else if (req->req_body_status == REQ_BODY_FAIL) {
			VSLb(bo->vsl, SLT_FetchError,
			    "req.body read error: %d (%s)",
			    errno, strerror(errno));
			req->doclose = SC_RX_BODY;
			retry = -1;
		}
	}

	j = WRW_FlushRelease(wrk, &bo->acct.bereq_hdrbytes);
	if (bo->acct.bereq_hdrbytes > hdrbytes) {
		bo->acct.bereq_bodybytes = bo->acct.bereq_hdrbytes - hdrbytes;
		bo->acct.bereq_hdrbytes = hdrbytes;
	}
	if (j != 0 || i != 0) {
		VSLb(bo->vsl, SLT_FetchError, "backend write error: %d (%s)",
		    errno, strerror(errno));
		VSLb_ts_busyobj(bo, "Bereq", W_TIM_real(wrk));
		VDI_CloseFd(&bo->vbc, &bo->acct);
		/* XXX: other cleanup ? */
		return (retry);
	}
	VSLb_ts_busyobj(bo, "Bereq", W_TIM_real(wrk));

	VSC_C_main->backend_req++;

	/* Receive response */

	HTTP1_Init(htc, bo->ws, vc->fd, vc->vsl,
	    cache_param->http_resp_size,
	    cache_param->http_resp_hdr_len);
	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(bo->htc, HTTP_CONN_MAGIC);

	VTCP_set_read_timeout(vc->fd, vc->first_byte_timeout);

	first = 1;
	do {
		hs = HTTP1_Rx(htc);
		if (hs == HTTP1_OVERFLOW) {
			bo->acct.beresp_hdrbytes += Tlen(htc->rxbuf);
			VSLb(bo->vsl, SLT_FetchError,
			    "http %sread error: overflow",
			    first ? "first " : "");
			VDI_CloseFd(&bo->vbc, &bo->acct);
			/* XXX: other cleanup ? */
			return (-1);
		}
		if (hs == HTTP1_ERROR_EOF) {
			bo->acct.beresp_hdrbytes += Tlen(htc->rxbuf);
			VSLb(bo->vsl, SLT_FetchError, "http %sread error: EOF",
			    first ? "first " : "");
			VDI_CloseFd(&bo->vbc, &bo->acct);
			/* XXX: other cleanup ? */
			return (retry);
		}
		if (first) {
			retry = -1;
			first = 0;
			VTCP_set_read_timeout(vc->fd,
			    vc->between_bytes_timeout);
		}
	} while (hs != HTTP1_COMPLETE);
	bo->acct.beresp_hdrbytes += Tlen(htc->rxbuf);

	hp = bo->beresp;

	if (HTTP1_DissectResponse(hp, htc)) {
		VSLb(bo->vsl, SLT_FetchError, "http format error");
		VDI_CloseFd(&bo->vbc, &bo->acct);
		/* XXX: other cleanup ? */
		return (-1);
	}

	bo->doclose = hp->doclose;
	return (0);
}
