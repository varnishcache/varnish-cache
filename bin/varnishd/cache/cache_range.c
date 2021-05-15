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

#include "cache_varnishd.h"
#include "cache_filter.h"

#include "vct.h"
#include <vtim.h>

/*--------------------------------------------------------------------*/

struct vrg_priv {
	unsigned		magic;
#define VRG_PRIV_MAGIC		0xb886e711
	struct req		*req;
	ssize_t			range_low;
	ssize_t			range_high;
	ssize_t			range_off;
};

static int v_matchproto_(vdp_fini_f)
vrg_range_fini(struct vdp_ctx *vdc, void **priv)
{
	struct vrg_priv *vrg_priv;

	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	CAST_OBJ_NOTNULL(vrg_priv, *priv, VRG_PRIV_MAGIC);
	if (vrg_priv->range_off < vrg_priv->range_high) {
		Req_Fail(vrg_priv->req, SC_RANGE_SHORT);
		vrg_priv->req->vdc->retval = -1;
	}
	*priv = NULL;	/* struct on ws, no need to free */
	return (0);
}

static int v_matchproto_(vdp_bytes_f)
vrg_range_bytes(struct vdp_ctx *vdx, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{
	int retval = 0;
	ssize_t l = 0;
	const char *p = ptr;
	struct vrg_priv *vrg_priv;

	CHECK_OBJ_NOTNULL(vdx, VDP_CTX_MAGIC);
	AN(priv);
	CAST_OBJ_NOTNULL(vrg_priv, *priv, VRG_PRIV_MAGIC);

	if (ptr != NULL) {
		l = vrg_priv->range_low - vrg_priv->range_off;
		if (l > 0) {
			if (l > len)
				l = len;
			vrg_priv->range_off += l;
			p += l;
			len -= l;
		}
		l = vrg_priv->range_high - vrg_priv->range_off;
		if (l > len)
			l = len;
		vrg_priv->range_off += len;
		if (vrg_priv->range_off >= vrg_priv->range_high)
			act = VDP_END;
	}

	if (l > 0)
		retval = VDP_bytes(vdx, act, p, l);
	else if (l == 0 && act > VDP_NULL)
		retval = VDP_bytes(vdx, act, p, 0);
	return (retval || act == VDP_END ? 1 : 0);
}

/*--------------------------------------------------------------------*/

static int v_matchproto_(vdp_init_f)
vrg_range_init(struct vdp_ctx *vdc, void **priv, struct objcore *oc)
{
	const char *r;
	const char *err;
	struct req *req;

	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	(void)oc;
	req = vdc->req;
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	assert(http_GetHdr(req->http, H_Range, &r));
	if (!vrg_ifrange(req))		// rfc7233,l,455,456
		return (1);
	err = vrg_dorange(req, r, priv);
	if (err == NULL)
		return (*priv == NULL ? 1 : 0);

	VSLb(vdc->vsl, SLT_Debug, "RANGE_FAIL %s", err);
	if (req->resp_len >= 0)
		http_PrintfHeader(req->resp,
		    "Content-Range: bytes */%jd",
		    (intmax_t)req->resp_len);
	http_PutResponse(req->resp, "HTTP/1.1", 416, NULL);
	/*
	 * XXX: We ought to produce a body explaining things.
	 * XXX: That really calls for us to hit vcl_synth{}
	 */
	req->resp_len = 0;
	return (1);
}

const struct vdp VDP_range = {
	.name =		"range",
	.init =		vrg_range_init,
	.bytes =	vrg_range_bytes,
	.fini =		vrg_range_fini,
};
