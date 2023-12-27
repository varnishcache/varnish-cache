/*-
 * Copyright (c) 2016 Varnish Software AS
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
 */

#include "config.h"

#include "cache/cache_varnishd.h"

#include <ctype.h>
#include <stdio.h>

#include "cache/cache_filter.h"
#include "cache/cache_transport.h"

#include "http2/cache_http2.h"

#include "vct.h"

/**********************************************************************/

struct hpack_static {
	uint8_t			idx;
	const char *		name;
	const char *		val;
};

static const struct hpack_static hp_static[] = {
#define HPS(I,N,V) [I] = { I, N ":", V },
#include "tbl/vhp_static.h"
	{ 0, "\377:", ""}		// Terminator
};

static const struct hpack_static *hp_idx[256];

void
V2D_Init(void)
{
	int i;
#define HPS(I,N,V)						\
	i = hp_static[I].name[0];				\
	if (hp_idx[i] == NULL) hp_idx[i] = &hp_static[I];
#include "tbl/vhp_static.h"
#undef HPS
}

/**********************************************************************/

static int v_matchproto_(vdp_init_f)
h2_init(VRT_CTX, struct vdp_ctx *vdc, void **priv)
{
	struct h2_req *r2;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	AN(priv);
	CAST_OBJ_NOTNULL(r2, *priv, H2_REQ_MAGIC);
	(void)r2;
	return (0);
}

static int v_matchproto_(vdp_fini_f)
h2_fini(struct vdp_ctx *vdc, void **priv)
{
	struct h2_req *r2;

	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vdc->wrk, WORKER_MAGIC);
	TAKE_OBJ_NOTNULL(r2, priv, H2_REQ_MAGIC);

	if (r2->error)
		return (0);

	if (vdc->retval < 0) {
		r2->error = H2SE_INTERNAL_ERROR; /* XXX: proper error? */
		H2_Send_Get(vdc->wrk, r2->h2sess, r2);
		H2_Send_RST(vdc->wrk, r2->h2sess, r2, r2->stream, r2->error);
		H2_Send_Rel(r2->h2sess, r2);
		return (0);
	}

	H2_Send_Get(vdc->wrk, r2->h2sess, r2);
	H2_Send(vdc->wrk, r2, H2_F_DATA, H2FF_DATA_END_STREAM, 0, "", NULL);
	H2_Send_Rel(r2->h2sess, r2);
	return (0);
}

static int v_matchproto_(vdp_bytes_f)
h2_bytes(struct vdp_ctx *vdc, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{
	struct h2_req *r2;

	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	CAST_OBJ_NOTNULL(r2, *priv, H2_REQ_MAGIC);
	(void)act;

	if ((r2->h2sess->error || r2->error))
		return (-1);
	if (len == 0)
		return (0);
	H2_Send_Get(vdc->wrk, r2->h2sess, r2);
	vdc->bytes_done = 0;
	H2_Send(vdc->wrk, r2, H2_F_DATA, H2FF_NONE, len, ptr, &vdc->bytes_done);
	H2_Send_Rel(r2->h2sess, r2);
	return (0);
}

static const struct vdp h2_vdp = {
	.name =		"H2B",
	.init =		h2_init,
	.bytes =	h2_bytes,
	.fini =		h2_fini,
};

static inline size_t
h2_status(uint8_t *p, uint16_t status) {
	size_t l = 1;

	switch (status) {
	case 200: *p = 0x80 |  8; break;
	case 204: *p = 0x80 |  9; break;
	case 206: *p = 0x80 | 10; break;
	case 304: *p = 0x80 | 11; break;
	case 400: *p = 0x80 | 12; break;
	case 404: *p = 0x80 | 13; break;
	case 500: *p = 0x80 | 14; break;
	default:
		*p++ = 0x18;
		*p++ = 0x03;
		l = 2;

		l += snprintf((char*)p, 4, "%03d", status);
		assert(l == 5);
		break;
	}

	return (l);
}

int v_matchproto_(vtr_minimal_response_f)
h2_minimal_response(struct req *req, uint16_t status)
{
	struct h2_req *r2;
	size_t l;
	uint8_t buf[6];

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CAST_OBJ_NOTNULL(r2, req->transport_priv, H2_REQ_MAGIC);

	assert(status >= 100);
	assert(status < 1000);

	l = h2_status(buf, status);
	assert(l < sizeof(buf));

	VSLb(req->vsl, SLT_RespProtocol, "HTTP/2.0");
	VSLb(req->vsl, SLT_RespStatus, "%03d", status);
	VSLbs(req->vsl, SLT_RespReason,
	      TOSTRAND(http_Status2Reason(status, NULL)));

	if (status >= 400)
		req->err_code = status;

	/* XXX return code checking once H2_Send returns anything but 0 */
	H2_Send_Get(req->wrk, r2->h2sess, r2);
	H2_Send(req->wrk, r2,
	    H2_F_HEADERS,
	    H2FF_HEADERS_END_HEADERS |
		(status < 200 ? 0 : H2FF_HEADERS_END_STREAM),
	    l, buf, NULL);
	H2_Send_Rel(r2->h2sess, r2);
	return (0);
}

static void
h2_enc_len(struct vsb *vsb, unsigned bits, unsigned val, uint8_t b0)
{
	assert(bits < 8);
	unsigned mask = (1U << bits) - 1U;

	if (val >= mask) {
		VSB_putc(vsb, b0 | (uint8_t)mask);
		val -= mask;
		while (val >= 128) {
			VSB_putc(vsb, 0x80 | ((uint8_t)val & 0x7f));
			val >>= 7;
		}
	}
	VSB_putc(vsb, (uint8_t)val);
}

/*
 * Hand-crafted-H2-HEADERS-R-Us:
 *
 * This is a handbuilt HEADERS frame for when we run out of workspace
 * during delivery.
 */

static const uint8_t h2_500_resp[] = {
	// :status 500
	0x8e,

	// content-length 0
	0x1f, 0x0d, 0x01, 0x30,

	// server Varnish
	0x1f, 0x27, 0x07, 'V', 'a', 'r', 'n', 'i', 's', 'h',
};

static void
h2_build_headers(struct vsb *resp, struct req *req)
{
	unsigned u, l;
	int i;
	struct http *hp;
	const char *r;
	const struct hpack_static *hps;
	uint8_t buf[6];
	ssize_t sz, sz1;

	assert(req->resp->status % 1000 >= 100);
	l = h2_status(buf, req->resp->status % 1000);
	VSB_bcat(resp, buf, l);

	hp = req->resp;
	for (u = HTTP_HDR_FIRST; u < hp->nhd && !VSB_error(resp); u++) {
		r = strchr(hp->hd[u].b, ':');
		AN(r);

		if (http_IsFiltered(hp, u, HTTPH_C_SPECIFIC))
			continue; //rfc7540,l,2999,3006

		hps = hp_idx[tolower(*hp->hd[u].b)];
		sz = 1 + r - hp->hd[u].b;
		assert(sz > 0);
		while (hps != NULL && hps->idx > 0) {
			i = strncasecmp(hps->name, hp->hd[u].b, sz);
			if (i < 0) {
				hps++;
				continue;
			}
			if (i > 0)
				hps = NULL;
			break;
		}
		if (hps != NULL) {
			VSLb(req->vsl, SLT_Debug,
			    "HP {%d, \"%s\", \"%s\"} <%s>",
			    hps->idx, hps->name, hps->val, hp->hd[u].b);
			h2_enc_len(resp, 4, hps->idx, 0x10);
		} else {
			VSB_putc(resp, 0x10);
			sz--;
			h2_enc_len(resp, 7, sz, 0);
			for (sz1 = 0; sz1 < sz; sz1++)
				VSB_putc(resp, tolower(hp->hd[u].b[sz1]));

		}

		while (vct_islws(*++r))
			continue;
		sz = hp->hd[u].e - r;
		h2_enc_len(resp, 7, sz, 0);
		VSB_bcat(resp, r, sz);
	}
}

void v_matchproto_(vtr_deliver_f)
h2_deliver(struct req *req, struct boc *boc, int sendbody)
{
	size_t sz;
	const char *r;
	struct sess *sp;
	struct h2_req *r2;
	struct vsb resp[1];
	struct vrt_ctx ctx[1];
	uintptr_t ss;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_ORNULL(boc, BOC_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);
	CAST_OBJ_NOTNULL(r2, req->transport_priv, H2_REQ_MAGIC);
	sp = req->sp;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	VSLb(req->vsl, SLT_RespProtocol, "HTTP/2.0");

	(void)http_DoConnection(req->resp, SC_RESP_CLOSE);

	ss = WS_Snapshot(req->ws);

	WS_VSB_new(resp, req->ws);
	h2_build_headers(resp, req);
	r = WS_VSB_finish(resp, req->ws, &sz);

	if (r == NULL) {
		VSLb(req->vsl, SLT_Error, "workspace_client overflow");
		VSLb(req->vsl, SLT_RespStatus, "500");
		VSLb(req->vsl, SLT_RespReason, "Internal Server Error");
		req->wrk->stats->client_resp_500++;

		r = (const char*)h2_500_resp;
		sz = sizeof h2_500_resp;
		sendbody = 0;
	}

	AZ(req->wrk->v1l);

	r2->t_send = req->t_prev;

	H2_Send_Get(req->wrk, r2->h2sess, r2);
	H2_Send(req->wrk, r2, H2_F_HEADERS,
	    (sendbody ? 0 : H2FF_HEADERS_END_STREAM) | H2FF_HEADERS_END_HEADERS,
	    sz, r, &req->acct.resp_hdrbytes);
	H2_Send_Rel(r2->h2sess, r2);

	WS_Reset(req->ws, ss);

	/* XXX someone into H2 please add appropriate error handling */
	if (sendbody) {
		INIT_OBJ(ctx, VRT_CTX_MAGIC);
		VCL_Req2Ctx(ctx, req);
		if (! VDP_Push(ctx, req->vdc, req->ws, &h2_vdp, r2))
			(void)VDP_DeliverObj(req->vdc, req->objcore);
	}

	AZ(req->wrk->v1l);
	req->acct.resp_bodybytes += VDP_Close(req->vdc, req->objcore, boc);
}
