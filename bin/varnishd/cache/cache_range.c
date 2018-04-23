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

#include "cache_varnishd.h"
#include "cache_filter.h"

#include "vct.h"

/*--------------------------------------------------------------------*/

struct vrg_priv {
	unsigned		magic;
#define VRG_PRIV_MAGIC		0xb886e711
	ssize_t			range_low;
	ssize_t			range_high;
	ssize_t			range_off;
};

static int v_matchproto_(vdp_bytes)
vrg_range_bytes(struct req *req, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{
	int retval = 0;
	ssize_t l;
	const char *p = ptr;
	struct vrg_priv *vrg_priv;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	if (act == VDP_INIT)
		return (0);
	CAST_OBJ_NOTNULL(vrg_priv, *priv, VRG_PRIV_MAGIC);
	if (act == VDP_FINI) {
		if (vrg_priv->range_off < vrg_priv->range_high)
			Req_Fail(req, SC_RANGE_SHORT);
		*priv = NULL;	/* struct on ws, no need to free */
		return (0);
	}

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
	if (l > 0)
		retval = VDP_bytes(req, act, p, l);
	else if (act > VDP_NULL)
		retval = VDP_bytes(req, act, p, 0);
	vrg_priv->range_off += len;
	return (retval ||
	    vrg_priv->range_off >= vrg_priv->range_high ? 1 : 0);
}

static const struct vdp vrg_vdp = {
	.name =		"RNG",
	.func =		vrg_range_bytes,
};

/*--------------------------------------------------------------------*/

static const char *
vrg_dorange(struct req *req, const char *r)
{
	ssize_t low, high, has_low, has_high, t;
	struct vrg_priv *vrg_priv;

	if (strncasecmp(r, "bytes=", 6))
		return ("Not Bytes");
	r += 6;

	/* The low end of range */
	has_low = low = 0;
	while (vct_isdigit(*r)) {
		has_low = 1;
		t = low;
		low *= 10;
		low += *r++ - '0';
		if (low < t)
			return ("Low number too big");
	}

	if (*r++ != '-')
		return ("Missing hyphen");

	/* The high end of range */
	has_high = high = 0;
	while (vct_isdigit(*r)) {
		has_high = 1;
		t = high;
		high *= 10;
		high += *r++ - '0';
		if (high < t)
			return ("High number too big");
	}

	if (*r != '\0')
		return ("Trailing stuff");

	if (has_high + has_low == 0)
		return ("Neither high nor low");

	if (!has_low) {
		if (req->resp_len < 0)
			return (NULL);		// Allow 200 response
		if (high == 0)
			return ("No low, high is zero");
		low = req->resp_len - high;
		if (low < 0)
			low = 0;
		high = req->resp_len - 1;
	} else if (req->resp_len >= 0 && (high >= req->resp_len || !has_high))
		high = req->resp_len - 1;
	else if (!has_high || req->resp_len < 0)
		return (NULL);			// Allow 200 response
	/*
	 * else (bo != NULL) {
	 *    We assume that the client knows what it's doing and trust
	 *    that both low and high make sense.
	 * }
	 */

	if (high < low)
		return ("high smaller than low");

	if (req->resp_len >= 0 && low >= req->resp_len)
		return ("low range beyond object");

	if (req->resp_len >= 0)
		http_PrintfHeader(req->resp, "Content-Range: bytes %jd-%jd/%jd",
		    (intmax_t)low, (intmax_t)high, (intmax_t)req->resp_len);
	else
		http_PrintfHeader(req->resp, "Content-Range: bytes %jd-%jd/*",
		    (intmax_t)low, (intmax_t)high);
	req->resp_len = (intmax_t)(1 + high - low);

	vrg_priv = WS_Alloc(req->ws, sizeof *vrg_priv);
	if (vrg_priv == NULL)
		return ("WS too small");

	XXXAN(vrg_priv);
	INIT_OBJ(vrg_priv, VRG_PRIV_MAGIC);
	vrg_priv->range_off = 0;
	vrg_priv->range_low = low;
	vrg_priv->range_high = high + 1;
	if (VDP_push(req, &vrg_vdp, vrg_priv, 1))
		return ("WS too small");
	http_PutResponse(req->resp, "HTTP/1.1", 206, NULL);
	return (NULL);
}

void
VRG_dorange(struct req *req, const char *r)
{
	const char *err;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);
	assert(http_IsStatus(req->resp, 200));

	/* We must snapshot the length if we're streaming from the backend */

	err = vrg_dorange(req, r);
	if (err != NULL) {
		VSLb(req->vsl, SLT_Debug, "RANGE_FAIL %s", err);
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
	}
}
