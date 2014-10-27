/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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
#include "cache/cache_filter.h"

#include "vct.h"

/*--------------------------------------------------------------------*/

struct vrg_priv {
	unsigned		magic;
#define VRG_PRIV_MAGIC		0xb886e711
	ssize_t			range_low;
	ssize_t			range_high;
	ssize_t			range_off;
};

static int __match_proto__(vdp_bytes)
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
	if (act == VDP_FINI) {
		*priv = NULL;
		return (0);
	}
	CAST_OBJ_NOTNULL(vrg_priv, *priv, VRG_PRIV_MAGIC);
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
	return (retval);
}

/*--------------------------------------------------------------------*/

void
VRG_dorange(struct req *req, struct busyobj *bo, const char *r)
{
	ssize_t len, low, high, has_low;
	struct vrg_priv *vrg_priv;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);
	assert(http_IsStatus(req->resp, 200));

	/* We must snapshot the length if we're streaming from the backend */
	if (bo != NULL)
		len = VBO_waitlen(req->wrk, bo, -1);
	else
		len = ObjGetLen(req->wrk, req->objcore);

	if (strncmp(r, "bytes=", 6))
		return;
	r += 6;

	/* The low end of range */
	has_low = low = 0;
	if (!vct_isdigit(*r) && *r != '-')
		return;
	while (vct_isdigit(*r)) {
		has_low = 1;
		low *= 10;
		low += *r - '0';
		r++;
	}

	if (low >= len)
		return;

	if (*r != '-')
		return;
	r++;

	/* The high end of range */
	if (vct_isdigit(*r)) {
		high = 0;
		while (vct_isdigit(*r)) {
			high *= 10;
			high += *r - '0';
			r++;
		}
		if (!has_low) {
			low = len - high;
			if (low < 0)
				low = 0;
			high = len - 1;
		}
	} else
		high = len - 1;
	if (*r != '\0')
		return;

	if (high >= len)
		high = len - 1;

	if (low > high)
		return;

	http_PrintfHeader(req->resp, "Content-Range: bytes %jd-%jd/%jd",
	    (intmax_t)low, (intmax_t)high, (intmax_t)len);
	http_Unset(req->resp, H_Content_Length);
	if (req->res_mode & RES_LEN)
		http_PrintfHeader(req->resp, "Content-Length: %jd",
		    (intmax_t)(1 + high - low));
	http_PutResponse(req->resp, "HTTP/1.1", 206, NULL);

	vrg_priv = WS_Alloc(req->ws, sizeof *vrg_priv);
	XXXAN(vrg_priv);
	INIT_OBJ(vrg_priv, VRG_PRIV_MAGIC);
	vrg_priv->range_off = 0;
	vrg_priv->range_low = low;
	vrg_priv->range_high = high + 1;
	VDP_push(req, vrg_range_bytes, vrg_priv, 0);
}
