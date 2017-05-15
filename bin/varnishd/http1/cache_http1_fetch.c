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
 */

#include "config.h"

#include "cache/cache.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "vtcp.h"
#include "vtim.h"

#include "cache_http1.h"

/*--------------------------------------------------------------------
 * Pass the request body to the backend
 */

static int __match_proto__(objiterate_f)
vbf_iter_req_body(void *priv, int flush, const void *ptr, ssize_t l)
{
	struct busyobj *bo;

	CAST_OBJ_NOTNULL(bo, priv, BUSYOBJ_MAGIC);

	if (l > 0) {
		bo->acct.bereq_bodybytes += V1L_Write(bo->wrk, ptr, l);
		if (flush && V1L_Flush(bo->wrk))
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
V1F_SendReq(struct worker *wrk, struct busyobj *bo, uint64_t *ctr,
    int onlycached)
{
	struct http *hp;
	int j;
	ssize_t i;
	struct http_conn *htc;
	int do_chunked = 0;
	char abuf[VTCP_ADDRBUFSIZE];
	char pbuf[VTCP_PORTBUFSIZE];

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->htc, HTTP_CONN_MAGIC);
	CHECK_OBJ_ORNULL(bo->req, REQ_MAGIC);

	htc = bo->htc;
	assert(*htc->rfd > 0);
	hp = bo->bereq;

	if (bo->req != NULL &&
	    bo->req->req_body_status == REQ_BODY_WITHOUT_LEN) {
		http_PrintfHeader(hp, "Transfer-Encoding: chunked");
		do_chunked = 1;
	}

	VTCP_hisname(*htc->rfd, abuf, sizeof abuf, pbuf, sizeof pbuf);
	VSLb(bo->vsl, SLT_BackendStart, "%s %s", abuf, pbuf);

	(void)VTCP_blocking(*htc->rfd);	/* XXX: we should timeout instead */
	V1L_Reserve(wrk, wrk->aws, htc->rfd, bo->vsl, bo->t_prev);
	*ctr += HTTP1_Write(wrk, hp, HTTP1_Req);

	/* Deal with any message-body the request might (still) have */
	i = 0;

	if (bo->req != NULL &&
	    (bo->req->req_body_status == REQ_BODY_CACHED || !onlycached)) {
		if (do_chunked)
			V1L_Chunked(wrk);
		i = VRB_Iterate(bo->req, vbf_iter_req_body, bo);

		if (bo->req->req_body_status == REQ_BODY_FAIL) {
			assert(i < 0);
			VSLb(bo->vsl, SLT_FetchError,
			    "req.body read error: %d (%s)",
			    errno, strerror(errno));
			bo->req->doclose = SC_RX_BODY;
		}
		if (do_chunked)
			V1L_EndChunk(wrk);
	}

	j = V1L_FlushRelease(wrk);
	if (j != 0 || i < 0) {
		VSLb(bo->vsl, SLT_FetchError, "backend write error: %d (%s)",
		    errno, strerror(errno));
		VSLb_ts_busyobj(bo, "Bereq", W_TIM_real(wrk));
		htc->doclose = SC_TX_ERROR;
		return (-1);
	}
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
	    t, t + htc->between_bytes_timeout, cache_param->http_resp_size);
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
		default:
			VSLb(bo->vsl, SLT_FetchError, "HTC status %d", hs);
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

	htc->doclose = http_DoConnection(hp);

	return (0);
}
