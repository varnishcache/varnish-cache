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
#include "cache/cache_transport.h"

#include "vtcp.h"

/*--------------------------------------------------------------------*/

static void
v1d_error(struct req *req, struct v1l **v1lp, const char *msg)
{
	static const char r_500[] =
	    "HTTP/1.1 500 Internal Server Error\r\n"
	    "Server: Varnish\r\n"
	    "Connection: close\r\n\r\n";
	uint64_t bytes;

	AN(v1lp);
	if (*v1lp != NULL)
		(void) V1L_Close(v1lp, &bytes);

	VSLbs(req->vsl, SLT_Error, TOSTRAND(msg));
	VSLb(req->vsl, SLT_RespProtocol, "HTTP/1.1");
	VSLb(req->vsl, SLT_RespStatus, "500");
	VSLb(req->vsl, SLT_RespReason, "Internal Server Error");

	req->wrk->stats->client_resp_500++;
	VTCP_Assert(write(req->sp->fd, r_500, sizeof r_500 - 1));
	req->doclose = SC_TX_EOF;

	req->acct.resp_bodybytes += VDP_Close(req->vdc, req->objcore, req->boc);
}

/*--------------------------------------------------------------------
 */

enum vtr_deliver_e v_matchproto_(vtr_deliver_f)
V1D_Deliver(struct req *req, int sendbody)
{
	struct vrt_ctx ctx[1];
	int err = 0, chunked = 0;
	stream_close_t sc;
	uint64_t bytes;
	struct v1l *v1l;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_ORNULL(req->boc, BOC_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);

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

	CHECK_OBJ_NOTNULL(req->wrk, WORKER_MAGIC);

	v1l = V1L_Open(req->wrk->aws, &req->sp->fd, req->vsl,
	    req->t_prev + SESS_TMO(req->sp, send_timeout),
	    cache_param->http1_iovs);

	if (v1l == NULL) {
		v1d_error(req, &v1l, "Failure to init v1d (workspace_thread overflow)");
		return (VTR_D_DONE);
	}

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
		if (VDP_Push(ctx, req->vdc, req->ws, VDP_v1l, v1l)) {
			v1d_error(req, &v1l, "Failure to push v1d processor");
			return (VTR_D_DONE);
		}
	}

	if (WS_Overflowed(req->ws)) {
		v1d_error(req, &v1l, "workspace_client overflow");
		return (VTR_D_DONE);
	}

	if (WS_Overflowed(req->sp->ws)) {
		v1d_error(req, &v1l, "workspace_session overflow");
		return (VTR_D_DONE);
	}

	if (WS_Overflowed(req->wrk->aws)) {
		v1d_error(req, &v1l, "workspace_thread overflow");
		return (VTR_D_DONE);
	}

	req->acct.resp_hdrbytes += HTTP1_Write(v1l, req->resp, HTTP1_Resp);

	if (sendbody) {
		if (DO_DEBUG(DBG_FLUSH_HEAD))
			(void)V1L_Flush(v1l);
		if (chunked)
			V1L_Chunked(v1l);
		err = VDP_DeliverObj(req->vdc, req->objcore, req->boc);
		if (!err && chunked)
			V1L_EndChunk(v1l);
	}

	sc = V1L_Close(&v1l, &bytes);

	req->acct.resp_bodybytes += VDP_Close(req->vdc, req->objcore, req->boc);

	if (sc == SC_NULL && err && req->sp->fd >= 0)
		sc = SC_REM_CLOSE;
	if (sc != SC_NULL)
		Req_Fail(req, sc);
	return (VTR_D_DONE);
}
