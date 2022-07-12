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
#include "cache_http1.h"

#include "vtcp.h"

/*--------------------------------------------------------------------*/

static int v_matchproto_(vdp_bytes_f)
v1d_bytes(struct vdp_ctx *vdx, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{
	ssize_t wl = 0;

	CHECK_OBJ_NOTNULL(vdx, VDP_CTX_MAGIC);
	(void)priv;

	AZ(vdx->nxt);		/* always at the bottom of the pile */

	if (len > 0)
		wl = V1L_Write(vdx->wrk, ptr, len);
	if (act > VDP_NULL && V1L_Flush(vdx->wrk) != SC_NULL)
		return (-1);
	if (len != wl)
		return (-1);
	return (0);
}

static const struct vdp v1d_vdp = {
	.name =		"V1B",
	.bytes =	v1d_bytes,
};

static void
v1d_error(struct req *req, const char *msg)
{
	static const char r_500[] =
	    "HTTP/1.1 500 Internal Server Error\r\n"
	    "Server: Varnish\r\n"
	    "Connection: close\r\n\r\n";

	AZ(req->wrk->v1l);

	VSLbs(req->vsl, SLT_Error, TOSTRAND(msg));
	VSLb(req->vsl, SLT_RespProtocol, "HTTP/1.1");
	VSLb(req->vsl, SLT_RespStatus, "500");
	VSLb(req->vsl, SLT_RespReason, "Internal Server Error");

	req->wrk->stats->client_resp_500++;
	VTCP_Assert(write(req->sp->fd, r_500, sizeof r_500 - 1));
	req->doclose = SC_TX_EOF;
}

/*--------------------------------------------------------------------
 */

void v_matchproto_(vtr_deliver_f)
V1D_Deliver(struct req *req, struct boc *boc, int sendbody)
{
	struct vrt_ctx ctx[1];
	int err = 0, chunked = 0;
	stream_close_t sc;
	uint64_t hdrbytes, bytes;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_ORNULL(boc, BOC_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);

	if (req->doclose == SC_NULL && !cache_param->accept_traffic)
		req->doclose = SC_TRAFFIC_REFUSE;

	if (req->doclose == SC_NULL &&
	    http_HdrIs(req->resp, H_Connection, "close")) {
		req->doclose = SC_RESP_CLOSE;
	} else if (req->doclose != SC_NULL) {
		if (!http_HdrIs(req->resp, H_Connection, "close")) {
			http_Unset(req->resp, H_Connection);
			http_SetHeader(req->resp, "Connection: close");
		}
	} else if (!http_GetHdr(req->resp, H_Connection, NULL))
		http_SetHeader(req->resp, "Connection: keep-alive");

	if (sendbody) {
		if (!http_GetHdr(req->resp, H_Content_Length, NULL)) {
			if (req->http->protover == 11) {
				chunked = 1;
				http_SetHeader(req->resp,
				    "Transfer-Encoding: chunked");
			} else {
				req->doclose = SC_TX_EOF;
			}
		}
		INIT_OBJ(ctx, VRT_CTX_MAGIC);
		VCL_Req2Ctx(ctx, req);
		if (VDP_Push(ctx, req->vdc, req->ws, &v1d_vdp, NULL)) {
			v1d_error(req, "Failure to push v1d processor");
			return;
		}
	}

	if (WS_Overflowed(req->ws)) {
		v1d_error(req, "workspace_client overflow");
		return;
	}

	if (WS_Overflowed(req->sp->ws)) {
		v1d_error(req, "workspace_session overflow");
		return;
	}

	V1L_Open(req->wrk, req->wrk->aws, &req->sp->fd, req->vsl,
	    req->t_prev + SESS_TMO(req->sp, send_timeout),
	    cache_param->http1_iovs);

	if (WS_Overflowed(req->wrk->aws)) {
		v1d_error(req, "workspace_thread overflow");
		return;
	}

	hdrbytes = HTTP1_Write(req->wrk, req->resp, HTTP1_Resp);

	if (sendbody) {
		if (DO_DEBUG(DBG_FLUSH_HEAD))
			(void)V1L_Flush(req->wrk);
		if (chunked)
			V1L_Chunked(req->wrk);
		err = VDP_DeliverObj(req->vdc, req->objcore);
		if (!err && chunked)
			V1L_EndChunk(req->wrk);
	}

	sc = V1L_Close(req->wrk, &bytes);
	AZ(req->wrk->v1l);

	req->acct.resp_hdrbytes += hdrbytes;
	req->acct.resp_bodybytes += VDP_Close(req->vdc, req->objcore, boc);

	if (sc == SC_NULL && err && req->sp->fd >= 0)
		sc = SC_REM_CLOSE;
	if (sc != SC_NULL)
		Req_Fail(req, sc);
}
