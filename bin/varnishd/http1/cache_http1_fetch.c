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
 */

#include "config.h"

#include "cache/cache_varnishd.h"
#include "cache/cache_filter.h"

#include <stdio.h>
#include <stdlib.h>

#include "vtcp.h"
#include "vtim.h"

#include "cache_http1.h"

/*--------------------------------------------------------------------
 * Pass the request body to the backend
 */

static int v_matchproto_(objiterate_f)
vbf_iter_req_body(void *priv, unsigned flush, const void *ptr, ssize_t l)
{
	struct busyobj *bo;

	CAST_OBJ_NOTNULL(bo, priv, BUSYOBJ_MAGIC);

	if (l > 0) {
		if (DO_DEBUG(DBG_SLOW_BEREQ))
			VTIM_sleep(1.0);
		(void)V1L_Write(bo->wrk, ptr, l);
		if (flush && V1L_Flush(bo->wrk) != SC_NULL)
			return (-1);
	}
	return (0);
}

/*--------------------------------------------------------------------
 * Send request to backend, including any (cached) req.body
 *
 * Return value:
 *	 0 success
 *	 1 failure
 */

int
V1F_SendReq(struct worker *wrk, struct busyobj *bo, uint64_t *ctr_hdrbytes,
    uint64_t *ctr_bodybytes)
{
	struct http *hp;
	stream_close_t sc;
	ssize_t i;
	uint64_t bytes, hdrbytes;
	struct http_conn *htc;
	int do_chunked = 0;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->htc, HTTP_CONN_MAGIC);
	CHECK_OBJ_ORNULL(bo->req, REQ_MAGIC);
	AN(ctr_hdrbytes);
	AN(ctr_bodybytes);

	htc = bo->htc;
	assert(*htc->rfd > 0);
	hp = bo->bereq;

	if (bo->req != NULL && !bo->req->req_body_status->length_known) {
		http_PrintfHeader(hp, "Transfer-Encoding: chunked");
		do_chunked = 1;
	}

	VTCP_blocking(*htc->rfd);	/* XXX: we should timeout instead */
	/* XXX: need a send_timeout for the backend side */
	V1L_Open(wrk, wrk->aws, htc->rfd, bo->vsl, nan(""), 0);
	hdrbytes = HTTP1_Write(wrk, hp, HTTP1_Req);

	/* Deal with any message-body the request might (still) have */
	i = 0;

	if (bo->bereq_body != NULL) {
		AZ(bo->req);
		AZ(do_chunked);
		(void)ObjIterate(bo->wrk, bo->bereq_body,
		    bo, vbf_iter_req_body, 0);
	} else if (bo->req != NULL &&
	    bo->req->req_body_status != BS_NONE) {
		if (DO_DEBUG(DBG_FLUSH_HEAD))
			(void)V1L_Flush(wrk);
		if (do_chunked)
			V1L_Chunked(wrk);
		i = VRB_Iterate(wrk, bo->vsl, bo->req, vbf_iter_req_body, bo);

		if (!bo->req->req_body_cached)
			bo->no_retry = "req.body not cached";

		if (bo->req->req_body_status == BS_ERROR) {
			/*
			 * XXX: (#2332) We should test to see if the backend
			 * XXX: sent us some headers explaining why.
			 * XXX: This is hard because of the mistaken API split
			 * XXX: between cache_backend.c and V1F, and therefore
			 * XXX: Parked in this comment, pending renovation of
			 * XXX: the VDI/backend-protocol API to allow non-H1
			 * XXX: backends.
			 */
			assert(i < 0);
			VSLb(bo->vsl, SLT_FetchError,
			    "req.body read error: %d (%s)",
			    errno, VAS_errtxt(errno));
			bo->req->doclose = SC_RX_BODY;
		}
		if (do_chunked)
			V1L_EndChunk(wrk);
	}

	sc = V1L_Close(wrk, &bytes);
	CHECK_OBJ_NOTNULL(sc, STREAM_CLOSE_MAGIC);

	/* Bytes accounting */
	if (bytes < hdrbytes)
		*ctr_hdrbytes += bytes;
	else {
		*ctr_hdrbytes += hdrbytes;
		*ctr_bodybytes += bytes - hdrbytes;
	}

	if (sc == SC_NULL && i < 0)
		sc = SC_TX_ERROR;

	CHECK_OBJ_NOTNULL(sc, STREAM_CLOSE_MAGIC);
	if (sc != SC_NULL) {
		VSLb(bo->vsl, SLT_FetchError,
		    "backend write error: %d (%s) (%s)",
		    errno, VAS_errtxt(errno), sc->desc);
		VSLb_ts_busyobj(bo, "Bereq", W_TIM_real(wrk));
		htc->doclose = sc;
		return (-1);
	}
	CHECK_OBJ_NOTNULL(sc, STREAM_CLOSE_MAGIC);
	VSLb_ts_busyobj(bo, "Bereq", W_TIM_real(wrk));
	return (0);
}

int
V1F_FetchRespHdr(struct busyobj *bo)
{

	struct http *hp;
	int i;
	double t;
	struct http_conn *htc;
	enum htc_status_e hs;
	const char *name, *desc;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->htc, HTTP_CONN_MAGIC);
	CHECK_OBJ_ORNULL(bo->req, REQ_MAGIC);

	htc = bo->htc;
	assert(*htc->rfd > 0);

	VSC_C_main->backend_req++;

	/* Receive response */

	HTC_RxInit(htc, bo->ws);
	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(bo->htc, HTTP_CONN_MAGIC);

	t = VTIM_real() + htc->first_byte_timeout;
	hs = HTC_RxStuff(htc, HTTP1_Complete, NULL, NULL,
	    t, NAN, htc->between_bytes_timeout, cache_param->http_resp_size);
	if (hs != HTC_S_COMPLETE) {
		bo->acct.beresp_hdrbytes +=
		    htc->rxbuf_e - htc->rxbuf_b;
		switch (hs) {
		case HTC_S_JUNK:
			VSLb(bo->vsl, SLT_FetchError, "Received junk");
			htc->doclose = SC_RX_JUNK;
			break;
		case HTC_S_CLOSE:
			VSLb(bo->vsl, SLT_FetchError, "backend closed");
			htc->doclose = SC_RESP_CLOSE;
			break;
		case HTC_S_TIMEOUT:
			VSLb(bo->vsl, SLT_FetchError, "timeout");
			htc->doclose = SC_RX_TIMEOUT;
			break;
		case HTC_S_OVERFLOW:
			VSLb(bo->vsl, SLT_FetchError, "overflow");
			htc->doclose = SC_RX_OVERFLOW;
			break;
		case HTC_S_IDLE:
			VSLb(bo->vsl, SLT_FetchError, "first byte timeout");
			htc->doclose = SC_RX_TIMEOUT;
			break;
		default:
			HTC_Status(hs, &name, &desc);
			VSLb(bo->vsl, SLT_FetchError, "HTC %s (%s)",
			     name, desc);
			htc->doclose = SC_RX_BAD;
			break;
		}
		return (htc->rxbuf_e == htc->rxbuf_b ? 1 : -1);
	}
	VTCP_set_read_timeout(*htc->rfd, htc->between_bytes_timeout);

	hp = bo->beresp;

	i = HTTP1_DissectResponse(htc, hp, bo->bereq);
	bo->acct.beresp_hdrbytes += htc->rxbuf_e - htc->rxbuf_b;
	if (i) {
		VSLb(bo->vsl, SLT_FetchError, "http format error");
		htc->doclose = SC_RX_JUNK;
		return (-1);
	}

	htc->doclose = http_DoConnection(hp, SC_RESP_CLOSE);

	/*
	 * Figure out how the fetch is supposed to happen, before the
	 * headers are adulterated by VCL
	 */
	if (http_method_eq(http_GetMethod(bo->bereq), HEAD)) {
		/*
		 * A HEAD request can never have a body in the reply,
		 * no matter what the headers might say.
		 * [RFC7231 4.3.2 p25]
		 */
		bo->wrk->stats->fetch_head++;
		bo->htc->body_status = BS_NONE;
	} else if (http_GetStatus(bo->beresp) <= 199) {
		/*
		 * 1xx responses never have a body.
		 * [RFC7230 3.3.2 p31]
		 * ... but we should never see them.
		 */
		bo->wrk->stats->fetch_1xx++;
		bo->htc->body_status = BS_ERROR;
	} else if (http_IsStatus(bo->beresp, 204)) {
		/*
		 * 204 is "No Content", obviously don't expect a body.
		 * [RFC7230 3.3.1 p29 and 3.3.2 p31]
		 */
		bo->wrk->stats->fetch_204++;
		if ((http_GetHdr(bo->beresp, H_Content_Length, NULL) &&
		    bo->htc->content_length != 0) ||
		    http_GetHdr(bo->beresp, H_Transfer_Encoding, NULL))
			bo->htc->body_status = BS_ERROR;
		else
			bo->htc->body_status = BS_NONE;
	} else if (http_IsStatus(bo->beresp, 304)) {
		/*
		 * 304 is "Not Modified" it has no body.
		 * [RFC7230 3.3 p28]
		 */
		bo->wrk->stats->fetch_304++;
		bo->htc->body_status = BS_NONE;
	} else if (bo->htc->body_status == BS_CHUNKED) {
		bo->wrk->stats->fetch_chunked++;
	} else if (bo->htc->body_status == BS_LENGTH) {
		assert(bo->htc->content_length > 0);
		bo->wrk->stats->fetch_length++;
	} else if (bo->htc->body_status == BS_EOF) {
		bo->wrk->stats->fetch_eof++;
	} else if (bo->htc->body_status == BS_ERROR) {
		bo->wrk->stats->fetch_bad++;
	} else if (bo->htc->body_status == BS_NONE) {
		bo->wrk->stats->fetch_none++;
	} else {
		WRONG("wrong bodystatus");
	}

	assert(bo->vfc->resp == bo->beresp);
	if (bo->htc->body_status != BS_NONE &&
	    bo->htc->body_status != BS_ERROR)
		if (V1F_Setup_Fetch(bo->vfc, bo->htc)) {
			VSLb(bo->vsl, SLT_FetchError, "overflow");
			htc->doclose = SC_RX_OVERFLOW;
			return (-1);
		}

	return (0);
}
