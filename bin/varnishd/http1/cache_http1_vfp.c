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
#include "vtcp.h"

/*--------------------------------------------------------------------
 * Read up to len bytes, returning pipelined data first.
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
		i = read(*htc->rfd, p, len);
		if (i < 0) {
			VTCP_Assert(i);
			VSLb(vc->wrk->vsl, SLT_FetchError,
			    "%s", strerror(errno));
			return (i);
		}
		if (i == 0)
			htc->doclose = SC_RESP_CLOSE;

	}
	return (i + l);
}


/*--------------------------------------------------------------------
 * read (CR)?LF at the end of a chunk
 */
static enum vfp_status
v1f_chunk_end(struct vfp_ctx *vc, struct http_conn *htc)
{
	char c;

	if (v1f_read(vc, htc, &c, 1) <= 0)
		return (VFP_Error(vc, "chunked read err"));
	if (c == '\r' && v1f_read(vc, htc, &c, 1) <= 0)
		return (VFP_Error(vc, "chunked read err"));
	if (c != '\n')
		return (VFP_Error(vc, "chunked tail no NL"));
	return (VFP_OK);
}


/*--------------------------------------------------------------------
 * Parse a chunk header and, for VFP_OK, return size in a pointer
 *
 * XXX: Reading one byte at a time is pretty pessimal.
 */

static enum vfp_status
v1f_chunked_hdr(struct vfp_ctx *vc, struct http_conn *htc, ssize_t *szp)
{
	char buf[20];		/* XXX: 20 is arbitrary */
	unsigned u;
	uintmax_t cll;
	ssize_t cl, lr;
	char *q;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	AN(szp);
	assert(*szp == -1);

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
				return (VFP_Error(vc, "chunked read err"));
		} while (u == 1 && buf[0] == '0' && buf[u] == '0');
		if (!vct_ishex(buf[u]))
			break;
	}

	if (u >= sizeof buf)
		return (VFP_Error(vc, "chunked header too long"));

	/* Skip trailing white space */
	while (vct_islws(buf[u]) && buf[u] != '\n') {
		lr = v1f_read(vc, htc, buf + u, 1);
		if (lr <= 0)
			return (VFP_Error(vc, "chunked read err"));
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

	*szp = cl;
	return (VFP_OK);
}


/*--------------------------------------------------------------------
 * Read a chunked HTTP object.
 *
 */

static enum vfp_status v_matchproto_(vfp_pull_f)
v1f_chunked_pull(struct vfp_ctx *vc, struct vfp_entry *vfe, void *ptr,
    ssize_t *lp)
{
	static enum vfp_status vfps;
	struct http_conn *htc;
	ssize_t l, lr;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	CAST_OBJ_NOTNULL(htc, vfe->priv1, HTTP_CONN_MAGIC);
	AN(ptr);
	AN(lp);

	l = *lp;
	*lp = 0;
	if (vfe->priv2 == -1) {
		vfps = v1f_chunked_hdr(vc, htc, &vfe->priv2);
		if (vfps != VFP_OK)
			return (vfps);
	}
	if (vfe->priv2 > 0) {
		if (vfe->priv2 < l)
			l = vfe->priv2;
		lr = v1f_read(vc, htc, ptr, l);
		if (lr <= 0)
			return (VFP_Error(vc, "straight insufficient bytes"));
		*lp = lr;
		vfe->priv2 -= lr;
		if (vfe->priv2 != 0)
			return (VFP_OK);

		vfe->priv2 = -1;
		return (v1f_chunk_end(vc, htc));
	}
	AZ(vfe->priv2);
	vfps = v1f_chunk_end(vc, htc);
	return (vfps == VFP_OK ? VFP_END : vfps);
}

static const struct vfp v1f_chunked = {
	.name = "V1F_CHUNKED",
	.pull = v1f_chunked_pull,
};


/*--------------------------------------------------------------------*/

static enum vfp_status v_matchproto_(vfp_pull_f)
v1f_straight_pull(struct vfp_ctx *vc, struct vfp_entry *vfe, void *p,
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
	.pull = v1f_straight_pull,
};

/*--------------------------------------------------------------------*/

static enum vfp_status v_matchproto_(vfp_pull_f)
v1f_eof_pull(struct vfp_ctx *vc, struct vfp_entry *vfe, void *p, ssize_t *lp)
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
	.pull = v1f_eof_pull,
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
