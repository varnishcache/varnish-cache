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

#include "cache/cache_varnishd.h"
#include "cache/cache_filter.h"
#include "cache_http1.h"

/*--------------------------------------------------------------------*/

static int v_matchproto_(vdp_bytes)
v1d_bytes(struct req *req, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{
	ssize_t wl = 0;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	(void)priv;
	if (act == VDP_INIT || act == VDP_FINI)
		return (0);

	AZ(req->vdc->nxt);		/* always at the bottom of the pile */

	if (len > 0)
		wl = V1L_Write(req->wrk, ptr, len);
	if (act > VDP_NULL && V1L_Flush(req->wrk))
		return (-1);
	if (len != wl)
		return (-1);
	return (0);
}

static const struct vdp v1d_vdp = {
	.name =		"V1B",
	.func =		v1d_bytes,
};

static void
v1d_error(struct req *req, const char *msg)
{
	static const char r_500[] =
	    "HTTP/1.1 500 Internal Server Error\r\n"
	    "Server: Varnish\r\n"
	    "Connection: close\r\n\r\n";

	VSLb(req->vsl, SLT_Error, "%s", msg);
	VSLb(req->vsl, SLT_RespProtocol, "HTTP/1.1");
	VSLb(req->vsl, SLT_RespStatus, "500");
	VSLb(req->vsl, SLT_RespReason, "Internal Server Error");

	(void)write(req->sp->fd, r_500, sizeof r_500 - 1);
	req->doclose = SC_TX_EOF;
}

/*--------------------------------------------------------------------
 */

void v_matchproto_(vtr_deliver_f)
V1D_Deliver(struct req *req, struct boc *boc, int sendbody)
{
	int err;
	unsigned u;
	uint64_t hdrbytes, bytes;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_ORNULL(boc, BOC_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);

	if (sendbody) {
		if (http_GetHdr(req->resp, H_Content_Length, NULL))
			req->res_mode |= RES_LEN;
		else if (req->http->protover == 11) {
			req->res_mode |= RES_CHUNKED;
			http_SetHeader(req->resp, "Transfer-Encoding: chunked");
		} else {
			req->res_mode |= RES_EOF;
			req->doclose = SC_TX_EOF;
		}
	}

	if (!req->doclose && http_HdrIs(req->resp, H_Connection, "close")) {
		req->doclose = SC_RESP_CLOSE;
	} else if (req->doclose) {
		if (!http_HdrIs(req->resp, H_Connection, "close")) {
			http_Unset(req->resp, H_Connection);
			http_SetHeader(req->resp, "Connection: close");
		}
	} else if (!http_GetHdr(req->resp, H_Connection, NULL))
		http_SetHeader(req->resp, "Connection: keep-alive");

	if (WS_Overflowed(req->ws)) {
		v1d_error(req, "workspace_client overflow");
		return;
	}

	if (WS_Overflowed(req->sp->ws)) {
		v1d_error(req, "workspace_session overflow");
		return;
	}

	if (req->resp_len == 0)
		sendbody = 0;

	if (sendbody && VDP_push(req, &v1d_vdp, NULL, 1)) {
		v1d_error(req, "workspace_thread overflow");
		AZ(req->wrk->v1l);
		return;
	}

	AZ(req->wrk->v1l);
	V1L_Open(req->wrk, req->wrk->aws,
		 &req->sp->fd, req->vsl, req->t_prev, 0);

	if (WS_Overflowed(req->wrk->aws)) {
		v1d_error(req, "workspace_thread overflow");
		AZ(req->wrk->v1l);
		return;
	}

	hdrbytes = HTTP1_Write(req->wrk, req->resp, HTTP1_Resp);

	if (DO_DEBUG(DBG_FLUSH_HEAD))
		(void)V1L_Flush(req->wrk);

	if (!sendbody || req->res_mode & RES_ESI) {
		if (V1L_Close(req->wrk, &bytes) && req->sp->fd >= 0) {
			Req_Fail(req, SC_REM_CLOSE);
			sendbody = 0;
		}

		/* Charge bytes sent as reported from V1L_Close. Only
		 * header-bytes have been attempted sent. */
		req->acct.resp_hdrbytes += bytes;
		hdrbytes = 0;
	}

	if (!sendbody) {
		AZ(req->wrk->v1l);
		VDP_close(req);
		return;
	}

	AN(sendbody);
	if (req->res_mode & RES_ESI) {
		AZ(req->wrk->v1l);

		V1L_Open(req->wrk, req->wrk->aws,
			 &req->sp->fd, req->vsl, req->t_prev,
			 cache_param->esi_iovs);

		if (WS_Overflowed(req->wrk->aws)) {
			v1d_error(req, "workspace_thread overflow");
			AZ(req->wrk->v1l);
			return;
		}
	}

	if (req->res_mode & RES_CHUNKED)
		V1L_Chunked(req->wrk);
	err = VDP_DeliverObj(req);
	if (!err && (req->res_mode & RES_CHUNKED))
		V1L_EndChunk(req->wrk);

	u = V1L_Close(req->wrk, &bytes);

	/* Bytes accounting */
	if (bytes < hdrbytes)
		req->acct.resp_hdrbytes += bytes;
	else {
		req->acct.resp_hdrbytes += hdrbytes;
		req->acct.resp_bodybytes += bytes - hdrbytes;
	}

	if ((u || err) && req->sp->fd >= 0)
		Req_Fail(req, SC_REM_CLOSE);
	AZ(req->wrk->v1l);
	VDP_close(req);
}
