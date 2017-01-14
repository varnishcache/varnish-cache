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

#include "cache/cache.h"

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

static int __match_proto__(vdp_bytes)
h2_bytes(struct req *req, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{
	struct h2_req *r2;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CAST_OBJ_NOTNULL(r2, req->transport_priv, H2_REQ_MAGIC);
	(void)priv;
	if (act == VDP_INIT || act == VDP_FINI)
		return (0);
	AZ(req->vdp_nxt);	       /* always at the bottom of the pile */

	H2_Send(req->wrk, r2,
	    act == VDP_FLUSH ? 1 : 0,
	    H2_FRAME_DATA, H2FF_NONE, len, ptr);

	return (0);
}

void __match_proto__(vtr_deliver_f)
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
	(void)sendbody;

	VSLb(req->vsl, SLT_Debug, "H2: Deliver");

	(void)WS_Reserve(req->ws, 0);
	p = (void*)req->ws->f;

	switch (req->resp->status) {
	case 200: *p++ = 0x80 |  8; break;
	case 204: *p++ = 0x80 |  9; break;
	case 206: *p++ = 0x80 | 10; break;
	case 304: *p++ = 0x80 | 11; break;
	case 400: *p++ = 0x80 | 12; break;
	case 404: *p++ = 0x80 | 13; break;
	case 500: *p++ = 0x80 | 14; break;
	default:
		*p++ = 0x18;
		*p++ = 0x03;

		assert(snprintf((char*)p, 4, "%03d", req->resp->status) == 3);
		p += 3;
		break;
	}

	hp = req->resp;
	for (u = HTTP_HDR_FIRST; u < hp->nhd; u++) {
		assert((char*)p < req->ws->e);

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
			if (hps->idx < 15) {
				*p++ = 0x10 | hps->idx;
			} else {
				*p++ = 0x1f;
				*p++ = hps->idx - 0x0f;
			}
		} else {

			*p++ = 0x10;
			sz--;
			if (sz < 127) {
				*p++ = (uint8_t)sz;
			} else {
				*p++ = 0x7f;
				*p++ = (uint8_t)sz - 0x7f;
			}

			for(sz1 = 0; sz1 < sz; sz1++)
				*p++ = (uint8_t)tolower(hp->hd[u].b[sz1]);

		}

		while(vct_islws(*++r))
			continue;
		sz = hp->hd[u].e - r;
		assert(sz <= 254);
		if (sz < 127) {
			*p++ = (uint8_t)sz;
		} else if (sz < 127 * 2) {
			*p++ = 0x7f;
			*p++ = (uint8_t)sz - 0x7f;
		}

		memcpy(p, r, sz);
		p += sz;
		assert((char*)p < req->ws->e);
	}
	sz = (char*)p - req->ws->f;

	/* XXX: Optimize !sendbody case */
	H2_Send(req->wrk, r2, 1, H2_FRAME_HEADERS, H2FF_HEADERS_END_HEADERS,
	    sz, req->ws->f);

	WS_Release(req->ws, 0);

	if (sendbody && req->resp_len != 0)
		VDP_push(req, h2_bytes, NULL, 1, "H2");

	AZ(req->wrk->v1l);

	if (sendbody && req->resp_len != 0)
		err = VDP_DeliverObj(req);
	/*XXX*/(void)err;

	H2_Send(req->wrk, r2, 1, H2_FRAME_DATA, H2FF_DATA_END_STREAM, 0, NULL);

	AZ(req->wrk->v1l);
	VDP_close(req);
}
