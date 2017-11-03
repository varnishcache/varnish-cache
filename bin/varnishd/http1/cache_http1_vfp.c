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
 *
 * HTTP1 Fetch Filters
 *
 * These filters are used for both req.body and beresp.body to handle
 * the HTTP/1 aspects (C-L/Chunked/EOF)
 *
 */

#include "config.h"

#include <errno.h>
#include <inttypes.h>

#include "cache/cache_varnishd.h"
#include "cache/cache_filter.h"
#include "cache_http1.h"

#include "vct.h"

/*--------------------------------------------------------------------
 * Read up to len bytes, returning pipelined data first,
 * read ahead for very small reads if we got a buffer
 */

static ssize_t
v1f_read(const struct vfp_ctx *vc, struct http_conn *htc, void *d, ssize_t len)
{
	ssize_t l;
	unsigned char *p;
	ssize_t i = 0;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	assert(len > 0);
	l = 0;
	p = d;

#ifdef DBG_V1F
	VSLb(vc->wrk->vsl, SLT_Debug, "v1f_read rxra %p->%p = %u "
	     "pipeline %p->%p = %u",
	     htc->rxra_b, htc->rxra_e, pdiff(htc->rxra_b, htc->rxra_e),
	     htc->pipeline_b, htc->pipeline_e,
	     pdiff(htc->pipeline_b, htc->pipeline_e));
#endif
	if (htc->pipeline_b) {
		l = htc->pipeline_e - htc->pipeline_b;
		assert(l > 0);
		if (l > len)
			l = len;
		memcpy(p, htc->pipeline_b, l);
		p += l;
		len -= l;
		htc->pipeline_b += l;
		if (htc->pipeline_b == htc->pipeline_e)
			htc->pipeline_b = htc->pipeline_e = NULL;
	}
	if (len > 0) {
		while (htc->pipeline_b == NULL) {
			i = htc->rxra_e - htc->rxra_b;
			if (i <= len)
				break;

			HTC_nonblocking(htc);
			i = read(*htc->rfd, htc->rxra_b, i);
			// on nonblocking error, fall through to blocking
			if (i <= 0)
				break;

			htc->pipeline_b = htc->rxra_b;
			htc->pipeline_e = htc->rxra_b + i;
			i = v1f_read(vc, htc, p, len);
			if (i < 0)
				return i;
			return (l + i);
		}
		HTC_blocking(htc);
		i = read(*htc->rfd, p, len);
		if (i < 0) {
			// XXX: VTCP_Assert(i); // but also: EAGAIN
			VSLb(vc->wrk->vsl, SLT_FetchError,
			    "%s", strerror(errno));
			return (i);
		}
	}
	return (i + l);
}

/*--------------------------------------------------------------------
 * check if header is in Trailer
 * XXX could be more efficient by avoiding repeated GetHdr in GetHdrToken
 */
static int
v1f_trailer_part_allowed(const struct http *hp, const char *hdr)
{
	const char *p = strchr(hdr, ':');
	const int l = (int)pdiff(hdr, p);
	char cp[l + 1];

	(void)strncpy(cp, hdr, l);
	cp[l] = '\0';

	return (http_GetHdrToken(hp, H_Trailer, cp, NULL, NULL));
}

/*--------------------------------------------------------------------
 * log and filter trailer parts based on Trailer header
 *
 * Ref: https://tools.ietf.org/html/rfc7230#section-4.4
 *
 * re-interpreting SHOULD as MUST: accept no trailer-part unless allowed in
 * Trailer.
 * To allow VCL control, we also accept "Tailer: *" for "allow any".
 *
 * code analoguos to http_Unset()
 */
static void
v1f_trailer_part_process(struct http *hp, int filter)
{
	uint16_t u, v;

	for (v = u = hp->thd; u < hp->nhd; u++) {
		Tcheck(hp->hd[u]);

		if (filter && ! v1f_trailer_part_allowed(hp, hp->hd[u].b)) {
			http_VSLH_del(hp, u);
			continue;
		}
		http_VSLH(hp, u);
		if (v != u) {
			memcpy(&hp->hd[v], &hp->hd[u], sizeof *hp->hd);
			memcpy(&hp->hdf[v], &hp->hdf[u], sizeof *hp->hdf);
		}
		v++;
	}
	hp->nhd = v;
}

/*--------------------------------------------------------------------
 * Process chunked encoding trailer. Inherits buffer from caller, which
 * is pre-filled up to lim
 */

#define trail_err(vc, ws, str) do {					\
		if (ws)						\
			WS_Release((ws), 0);				\
		return (VFP_Error((vc), (str)));			\
	} while(0)

static inline enum vfp_status
v1f_chunked_trailer(struct vfp_ctx *vc, struct http_conn *htc,
    char *buf, size_t bufsz, char *lim)
{
	const char *q;
	unsigned u, rdsz;

	char *hdrs_b = NULL;
	struct ws *ws = NULL;
	struct http *hp = NULL;

	enum {
		DONT_SAVE = 0,
		SAVE_FILTER,
		SAVE_WILD
	}
	save = DONT_SAVE;

	assert (bufsz >= 4);

	hp = vc->req;
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	if (vc->resp && http_GetHdrToken(hp, H_TE, "trailers", NULL, NULL)) {
		hp = vc->resp;
		CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

		if (http_GetHdr(hp, H_Trailer, &q))
			save = (*q == '*') ? SAVE_WILD : SAVE_FILTER;
	} else
		hp = NULL;

	if (save) {
		u = pdiff(buf, lim);

		ws = hp->ws;
		bufsz = WS_Reserve(ws, 0);
		if (bufsz < u + 4)
			trail_err(vc, ws, "insufficient ws for save");

		memcpy(ws->f, buf, u);
		buf = ws->f;
		lim = ws->f + u;
		bufsz -= u;
		hdrs_b = buf;
	}

	/*
	 * Trailers are terminated by CRLFCRLF, we try to read up to 4
	 * characters, unless we have already seen part of the termination
	 * sequence
	 */

	u = 0;
	while (1) {
#ifdef DBG_V1F
		VSLb(vc->wrk->vsl, SLT_Debug, "trailer u=%d %.*s",
		     u, (int)(lim - buf), buf);
#endif
		q = buf;
		while (q < lim) {
			switch (*q) {
			case '\r': {
				if (u & 1)
					trail_err(vc, ws,
						  "chunked trailer CRCR");
				u++;
				break;
			}
			case '\n': {
				if ((u & 1) == 0)
					trail_err(vc, ws,
						  "chunked trailer LF no CR");
				u++;
				break;
			}
			default:
				if (u & 1)
					trail_err(vc, ws,
						  "chunked trailer CR no LF");
				u = 0;
			}
			q++;
		}
		if (u >= 4)
			break;
		rdsz = 4 - u;
		if (save) {
			if (bufsz < rdsz)
				trail_err(vc, ws, "insufficient ws for save");
			buf = lim;
			bufsz -= rdsz;
		}
		if (v1f_read(vc, htc, buf, rdsz) != rdsz)
			trail_err(vc, ws, "chunked trailer read err");
		lim = buf + rdsz;
	}
	assert(u == 4);

	if (save) {
		if (pdiff(hdrs_b, lim) <= 4) {
			WS_Release(ws, 0);
			return (VFP_END);
		}

		lim -= 2;

		hp->thd = hp->nhd;

		// note: Could also change hp->conds - irrelevant here
		if (HTTP1_DissectHdrs(hp, &hdrs_b, lim,
				      cache_param->http_resp_hdr_len))
			trail_err(vc, ws, "chunked trailer dissect failed");

		assert(hdrs_b <= lim);

		v1f_trailer_part_process(hp, save == SAVE_FILTER);

		if (hp->thd == hp->nhd) {
			hp->thd = 0;
			WS_Release(ws, 0);
		} else
			WS_ReleaseP(ws, TRUST_ME(hp->hd[hp->nhd - 1].e + 1));
	}
	return (VFP_END);
}

#undef trail_err

/*--------------------------------------------------------------------
 * Read a chunked HTTP object.
 */

static enum vfp_status __match_proto__(vfp_pull_f)
v1f_pull_chunked(struct vfp_ctx *vc, struct vfp_entry *vfe, void *ptr,
    ssize_t *lp)
{
	struct http_conn *htc;
	const size_t bufsz = 20;	/* XXX: arbitrary */
	char buf[bufsz];
	char *q;
	unsigned u;
	uintmax_t cll;
	ssize_t cl, l, lr;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	CAST_OBJ_NOTNULL(htc, vfe->priv1, HTTP_CONN_MAGIC);
	AN(ptr);
	AN(lp);

	l = *lp;
	*lp = 0;
	if (vfe->priv2 == -1) {
		/* Skip leading whitespace */
		do {
			lr = v1f_read(vc, htc, buf, 1);
			if (lr <= 0)
				return (VFP_Error(vc, "chunked read err"));
		} while (vct_islws(buf[0]));

		if (!vct_ishex(buf[0]))
			 return (VFP_Error(vc, "chunked header non-hex"));

		/* Collect hex digits, skipping leading zeros */
		for (u = 1; u < sizeof buf; u++) {
			do {
				lr = v1f_read(vc, htc, buf + u, 1);
				if (lr <= 0)
					return (VFP_Error(vc,
					    "chunked read err"));
			} while (u == 1 && buf[0] == '0' && buf[u] == '0');
			if (!vct_ishex(buf[u]))
				break;
		}

		if (u >= sizeof buf)
			return (VFP_Error(vc, "chunked header too long"));

		/* ignore extensions until newline (no strict CRLF check) */
		if (vct_islws(buf[u])) {
			while (buf[u] != '\n') {
				lr = v1f_read(vc, htc, buf + u, 1);
				if (lr == 1)
					continue;
				return (VFP_Error(vc, "chunked read err"));
			}
		}

		if (buf[u] != '\n')
			return (VFP_Error(vc, "chunked header no NL"));

		buf[u] = '\0';

		cll = strtoumax(buf, &q, 16);
		if (q == NULL || *q != '\0')
			return (VFP_Error(vc, "chunked header number syntax"));
		cl = (ssize_t)cll;
		if (cl < 0 || (uintmax_t)cl != cll)
			return (VFP_Error(vc, "bogusly large chunk size"));

		vfe->priv2 = cl;
	}
	if (vfe->priv2 > 0) {
		if (vfe->priv2 < l)
			l = vfe->priv2;
		lr = v1f_read(vc, htc, ptr, l);
		if (lr <= 0)
			return (VFP_Error(vc, "straight insufficient bytes"));
		*lp = lr;
		vfe->priv2 -= lr;
		if (vfe->priv2 == 0)
			vfe->priv2 = -1;
		return (VFP_OK);
	}
	AZ(vfe->priv2);

	if (v1f_read(vc, htc, buf, 2) != 2)
		return (VFP_Error(vc, "chunked read err"));
	if (buf[0] == '\r') {
		if (buf[1] == '\n')
			return (VFP_END);
		return (VFP_Error(vc, "chunked tail CR no LF"));
	}
	return (v1f_chunked_trailer(vc, htc, buf, bufsz, buf + 2));
}

static const struct vfp v1f_chunked = {
	.name = "V1F_CHUNKED",
	.pull = v1f_pull_chunked,
};


/*--------------------------------------------------------------------*/

static enum vfp_status __match_proto__(vfp_pull_f)
v1f_pull_straight(struct vfp_ctx *vc, struct vfp_entry *vfe, void *p,
    ssize_t *lp)
{
	ssize_t l, lr;
	struct http_conn *htc;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	CAST_OBJ_NOTNULL(htc, vfe->priv1, HTTP_CONN_MAGIC);
	AN(p);
	AN(lp);

	l = *lp;
	*lp = 0;

	if (vfe->priv2 == 0) // XXX: Optimize Content-Len: 0 out earlier
		return (VFP_END);
	if (vfe->priv2 < l)
		l = vfe->priv2;
	lr = v1f_read(vc, htc, p, l);
	if (lr <= 0)
		return (VFP_Error(vc, "straight insufficient bytes"));
	*lp = lr;
	vfe->priv2 -= lr;
	if (vfe->priv2 == 0)
		return (VFP_END);
	return (VFP_OK);
}

static const struct vfp v1f_straight = {
	.name = "V1F_STRAIGHT",
	.pull = v1f_pull_straight,
};

/*--------------------------------------------------------------------*/

static enum vfp_status __match_proto__(vfp_pull_f)
v1f_pull_eof(struct vfp_ctx *vc, struct vfp_entry *vfe, void *p, ssize_t *lp)
{
	ssize_t l, lr;
	struct http_conn *htc;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	CAST_OBJ_NOTNULL(htc, vfe->priv1, HTTP_CONN_MAGIC);
	AN(p);

	AN(lp);

	l = *lp;
	*lp = 0;
	lr = v1f_read(vc, htc, p, l);
	if (lr < 0)
		return (VFP_Error(vc, "eof socket fail"));
	if (lr == 0)
		return (VFP_END);
	*lp = lr;
	return (VFP_OK);
}

static const struct vfp v1f_eof = {
	.name = "V1F_EOF",
	.pull = v1f_pull_eof,
};

/*--------------------------------------------------------------------
 */

int
V1F_Setup_Fetch(struct vfp_ctx *vfc, struct http_conn *htc)
{
	struct vfp_entry *vfe;

	CHECK_OBJ_NOTNULL(vfc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);

	switch (htc->body_status) {
	case BS_EOF:
		assert(htc->content_length == -1);
		vfe = VFP_Push(vfc, &v1f_eof);
		if (vfe == NULL)
			return (ENOSPC);
		vfe->priv2 = 0;
		break;
	case BS_LENGTH:
		assert(htc->content_length > 0);
		vfe = VFP_Push(vfc, &v1f_straight);
		if (vfe == NULL)
			return (ENOSPC);
		vfe->priv2 = htc->content_length;
		break;
	case BS_CHUNKED:
		assert(htc->content_length == -1);
		vfe = VFP_Push(vfc, &v1f_chunked);
		if (vfe == NULL)
			return (ENOSPC);
		vfe->priv2 = -1;
		break;
	default:
		WRONG("Wrong body_status");
		break;
	}
	vfe->priv1 = htc;
	return 0;
}
