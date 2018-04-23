/*-
 * Copyright (c) 2016 Varnish Software AS
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
 */

#include "config.h"

#include "cache/cache_varnishd.h"

#include <netinet/in.h>

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

static int v_matchproto_(vdp_bytes)
h2_bytes(struct req *req, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{
	struct h2_req *r2;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CAST_OBJ_NOTNULL(r2, req->transport_priv, H2_REQ_MAGIC);
	(void)priv;

	if (act == VDP_INIT)
		return (0);
	if (r2->error && act != VDP_FINI)
		return (-1);
	H2_Send_Get(req->wrk, r2->h2sess, r2);
	H2_Send(req->wrk, r2,
	    H2_F_DATA,
	    act == VDP_FINI ? H2FF_DATA_END_STREAM : H2FF_NONE,
	    len, ptr);
	req->acct.resp_bodybytes += len;
	H2_Send_Rel(r2->h2sess, r2);
	return (0);
}

static const struct vdp h2_vdp = {
	.name =		"H2B",
	.func =		h2_bytes,
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
	VSLb(req->vsl, SLT_RespReason, "%s", http_Status2Reason(status, NULL));

	if (status >= 400)
		req->err_code = status;

	/* XXX return code checking once H2_Send returns anything but 0 */
	H2_Send_Get(req->wrk, r2->h2sess, r2);
	H2_Send(req->wrk, r2,
	    H2_F_HEADERS,
	    H2FF_HEADERS_END_HEADERS |
		(status < 200 ? 0 : H2FF_HEADERS_END_STREAM),
	    l, buf);
	H2_Send_Rel(r2->h2sess, r2);
	return (0);
}

static uint8_t *
h2_enc_len(uint8_t *p, unsigned bits, unsigned val)
{
	assert(bits < 8);
	unsigned mask = (1U << bits) - 1U;

	if (val >= mask) {
		*p++ |= (uint8_t)mask;
		val -= mask;
		while (val >= 128) {
			*p++ = 0x80 | ((uint8_t)val & 0x7f);
			val >>= 7;
		}
	}
	*p++ = (uint8_t)val;
	return (p);
}

void v_matchproto_(vtr_deliver_f)
h2_deliver(struct req *req, struct boc *boc, int sendbody)
{
	ssize_t sz, sz1;
	uint8_t *p;
	unsigned u;
	const char *r;
	struct http *hp;
	struct sess *sp;
	struct h2_req *r2;
	int i, err;
	const struct hpack_static *hps;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_ORNULL(boc, BOC_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);
	CAST_OBJ_NOTNULL(r2, req->transport_priv, H2_REQ_MAGIC);
	sp = req->sp;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	(void)WS_Reserve(req->ws, 0);
	p = (void*)req->ws->f;

	p += h2_status(p, req->resp->status);

	hp = req->resp;
	for (u = HTTP_HDR_FIRST; u < hp->nhd; u++) {
		assert(WS_Inside(req->ws, p, NULL));

		r = strchr(hp->hd[u].b, ':');
		AN(r);

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
			*p = 0x10;
			p = h2_enc_len(p, 4, hps->idx);
		} else {

			*p++ = 0x10;
			sz--;
			p = h2_enc_len(p, 7, sz);
			for (sz1 = 0; sz1 < sz; sz1++)
				*p++ = (uint8_t)tolower(hp->hd[u].b[sz1]);

		}

		while (vct_islws(*++r))
			continue;
		sz = hp->hd[u].e - r;
		p = h2_enc_len(p, 7, sz);
		memcpy(p, r, sz);
		p += sz;
		assert(WS_Inside(req->ws, p, NULL));
	}
	sz = (char*)p - req->ws->f;

	AZ(req->wrk->v1l);

	if (sendbody && req->resp_len == 0)
		sendbody = 0;

	r2->t_send = req->t_prev;

	H2_Send_Get(req->wrk, r2->h2sess, r2);
	H2_Send(req->wrk, r2, H2_F_HEADERS,
	    (sendbody ? 0 : H2FF_HEADERS_END_STREAM) | H2FF_HEADERS_END_HEADERS,
	    sz, req->ws->f);
	req->acct.resp_hdrbytes += sz;
	H2_Send_Rel(r2->h2sess, r2);

	WS_Release(req->ws, 0);

	/* XXX someone into H2 please add appropriate error handling */
	while (sendbody) {
		err = VDP_push(req, &h2_vdp, NULL, 1);
		if (err)
			break;
		err = VDP_DeliverObj(req);
		break;
	}

	AZ(req->wrk->v1l);
	VDP_close(req);
}
