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
		l = vmin(vrg_priv->range_high - vrg_priv->range_off, len);
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

static const char *
vrg_dorange(struct req *req, void **priv)
{
	ssize_t low, high;
	struct vrg_priv *vrg_priv;
	const char *err;

	err = http_GetRange(req->http, &low, &high);
	if (err != NULL)
		return (err);

	assert(low >= -1);
	assert(high >= -1);

	if (low < 0) {
		if (req->resp_len < 0 || high < 0)
			return (NULL);		// Allow 200 response
		assert(high > 0);
		low = req->resp_len - high;
		if (low < 0)
			low = 0;
		high = req->resp_len - 1;
	} else if (req->resp_len >= 0 && (high >= req->resp_len || high < 0))
		high = req->resp_len - 1;
	else if (high < 0)
		return (NULL);			// Allow 200 response
	/*
	 * else (bo != NULL) {
	 *    We assume that the client knows what it's doing and trust
	 *    that both low and high make sense.
	 * }
	 */

	if (req->resp_len >= 0 && low >= req->resp_len)
		return ("low range beyond object");

	if (req->resp_len >= 0) {
		http_PrintfHeader(req->resp, "Content-Range: bytes %jd-%jd/%jd",
		    (intmax_t)low, (intmax_t)high, (intmax_t)req->resp_len);
		req->resp_len = (intmax_t)(1 + high - low);
	} else
		http_PrintfHeader(req->resp, "Content-Range: bytes %jd-%jd/*",
		    (intmax_t)low, (intmax_t)high);

	vrg_priv = WS_Alloc(req->ws, sizeof *vrg_priv);
	if (vrg_priv == NULL)
		return ("WS too small");

	INIT_OBJ(vrg_priv, VRG_PRIV_MAGIC);
	vrg_priv->req = req;
	vrg_priv->range_off = 0;
	vrg_priv->range_low = low;
	vrg_priv->range_high = high + 1;
	*priv = vrg_priv;
	http_PutResponse(req->resp, "HTTP/1.1", 206, NULL);
	return (NULL);
}

/*
 * return 1 if range should be observed, based on if-range value
 * if-range can either be a date or an ETag [RFC7233 3.2 p8]
 */
static int
vrg_ifrange(struct req *req)
{
	const char *p, *e;
	vtim_real ims, lm, d;

	if (!http_GetHdr(req->http, H_If_Range, &p))	// rfc7233,l,455,456
		return (1);

	/* strong validation needed */
	if (p[0] == 'W' && p[1] == '/')			// rfc7233,l,500,501
		return (0);

	/* ETag */
	if (p[0] == '"') {				// rfc7233,l,512,514
		if (!http_GetHdr(req->resp, H_ETag, &e))
			return (0);
		if ((e[0] == 'W' && e[1] == '/'))	// rfc7232,l,547,548
			return (0);
		/* XXX: should we also have http_etag_cmp() ? */
		return (strcmp(p, e) == 0);		// rfc7232,l,548,548
	}

	/* assume date, strong check [RFC7232 2.2.2 p7] */
	ims = VTIM_parse(p);
	if (!ims)					// rfc7233,l,502,512
		return (0);

	/* the response needs a Date */
	// rfc7232 fc7232,l,439,440
	if (!http_GetHdr(req->resp, H_Date, &p))
		return (0);
	d = VTIM_parse(p);
	if (!d)
		return (0);


	/* grab the Last Modified value */
	if (!http_GetHdr(req->resp, H_Last_Modified, &p))
		return (0);

	lm = VTIM_parse(p);
	if (!lm)
		return (0);

	/* Last Modified must be 60 seconds older than Date */
	if (lm > d + 60)				// rfc7232,l,442,443
		return (0);

	if (lm != ims)					// rfc7233,l,455,456
		return (0);
	return (1);
}

static int v_matchproto_(vdp_init_f)
vrg_range_init(VRT_CTX, struct vdp_ctx *vdc, void **priv, struct objcore *oc)
{
	const char *err;
	struct req *req;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	(void)oc;
	req = vdc->req;
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	if (!vrg_ifrange(req))		// rfc7233,l,455,456
		return (1);
	err = vrg_dorange(req, priv);
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

/*--------------------------------------------------------------------*/

int
VRG_CheckBo(struct busyobj *bo)
{
	ssize_t rlo, rhi, crlo, crhi, crlen, clen;
	const char *err;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	if (!cache_param->http_range_support)
		return (0);

	err = http_GetRange(bo->bereq0, &rlo, &rhi);
	clen = http_GetContentLength(bo->beresp);
	crlen = http_GetContentRange(bo->beresp, &crlo, &crhi);

	if (err != NULL) {
		VSLb(bo->vsl, SLT_Error, "Invalid range header (%s)", err);
		return (-1);
	}

	if (crlen < -1) {
		VSLb(bo->vsl, SLT_Error, "Invalid content-range header");
		return (-1);
	}

	if (clen < -1) {
		VSLb(bo->vsl, SLT_Error, "Invalid content-length header");
		return (-1);
	}

	if (crlo < 0 && crhi < 0 && crlen < 0) {
		AZ(http_GetHdr(bo->beresp, H_Content_Range, NULL));
		return (0);
	}

	if (rlo < 0 && rhi < 0) {
		VSLb(bo->vsl, SLT_Error, "Unexpected content-range header");
		return (-1);
	}

	if (crlo < 0) {		// Content-Range: bytes */123
		assert(crhi < 0);
		assert(crlen > 0);
		if (http_GetStatus(bo->beresp) == 416)
			return (0);
		crlo = 0;
		crhi = crlen - 1;
	}

#define RANGE_CHECK(val, op, crval, what)			\
	do {							\
		if (val >= 0 && !(val op crval)) {		\
			VSLb(bo->vsl, SLT_Error,		\
			    "Expected " what " %zd, got %zd",	\
			    crval, val);			\
			return (-1);				\
		}						\
	} while (0)

	crlen = (crhi - crlo) + 1;
	RANGE_CHECK(clen, ==, crlen, "content length");

	/* NB: if the client didn't specify a low range the high range
	 * was adjusted based on the resource length, and a high range
	 * is allowed to be out of bounds so at this point there is
	 * nothing left to check.
	 */
	if (rlo < 0)
		return (0);

	RANGE_CHECK(rlo, ==, crlo, "low range");
	RANGE_CHECK(rhi, >=, crhi, "minimum high range");
#undef RANGE_CHECK

	return (0);
}
