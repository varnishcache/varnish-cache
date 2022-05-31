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
 *
 * HTTP1 Fetch Filters
 *
 * These filters are used for both req.body and beresp.body to handle
 * the HTTP/1 aspects (C-L/Chunked/EOF)
 *
 */

#include "config.h"

#include <inttypes.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/uio.h>

#include "cache/cache_varnishd.h"
#include "cache/cache_filter.h"
#include "cache_http1.h"

#include "vct.h"
#include "vtcp.h"

/*--------------------------------------------------------------------
 * Read into the iovecs, returning pipelined data first.
 *
 * iova[1] and beyond are for readahead.
 *
 * v1f_read() returns with no actual read issued if pipelined data
 * fills iova[0]
 */

// for single io vector
#define IOV(ptr,len) (struct iovec[1]){{.iov_base = (ptr), .iov_len = (len)}}, 1

static ssize_t
v1f_read(const struct vfp_ctx *vc, struct http_conn *htc,
    const struct iovec *iova, int iovcnt)
{
	ssize_t l, i = 0;
	size_t ll;
	int c;
	struct iovec iov[iovcnt];

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	AN(iova);
	assert(iovcnt > 0);
	memcpy(iov, iova, sizeof iov);
	/* only check first iov */
	AN(iov[0].iov_base);
	assert(iov[0].iov_len > 0);

	l = 0;
	c = 0;

	while (htc->pipeline_b && c < iovcnt) {
		ll = htc->pipeline_e - htc->pipeline_b;
		assert(ll > 0);
		ll = vmin(ll, iov[c].iov_len);
		memcpy(iov[c].iov_base, htc->pipeline_b, ll);
		iov[c].iov_base = (unsigned char *)iov[c].iov_base + ll;
		iov[c].iov_len -= ll;
		htc->pipeline_b += ll;
		if (iov[c].iov_len == 0)
			c++;
		if (htc->pipeline_b == htc->pipeline_e)
			htc->pipeline_b = htc->pipeline_e = NULL;
		l += ll;
	}

	// see comment above
	if (c > 0)
		return (l);
	AZ(c);

	i = readv(*htc->rfd, iov, iovcnt);

	if (i < 0) {
		VTCP_Assert(i);
		VSLb(vc->wrk->vsl, SLT_FetchError, "%s", VAS_errtxt(errno));
		return (i);
	}

	if (i == 0)
		htc->doclose = SC_RESP_CLOSE;

	return (i + l);
}

/* txt but not const */
typedef struct {
	char		*b;
	char		*e;
} wtxt;

struct v1f_chunked_priv {
	unsigned		magic;
#define V1F_CHUNKED_PRIV_MAGIC 0xf5232e94
	unsigned		len;
	struct vfp_ctx		*vc;
	struct http_conn	*htc;
	wtxt			avail;
	char			buf[];
};

/* XXX reduce? */
#define ASSERT_V1F_CHUNKED_PRIV(x)					\
do {									\
	CHECK_OBJ_NOTNULL(x, V1F_CHUNKED_PRIV_MAGIC);			\
	assert((x->avail.b != NULL) == (x->avail.e != NULL));		\
	assert(x->avail.b == NULL || x->avail.b >= x->buf);		\
	assert(x->avail.e == NULL || x->avail.e <= x->buf + x->len);	\
} while (0)

/*
 * ensure the readahead buffer has at least n bytes available and read
 * ahead a maximum of ra
 *
 * the caller must ensure that ra is below or at the maximum readahead
 * poosible at that point in order to not read into the next request
 *
 * (we can not return data to the next request's htc pipeline (yet))
 */

static inline enum vfp_status
v1f_chunked_need(struct v1f_chunked_priv *v1fcp, size_t n, size_t ra)
{
	char *p;
	ssize_t i;
	size_t l = 0;

	ASSERT_V1F_CHUNKED_PRIV(v1fcp);
	AN(n);
	assert(n <= v1fcp->len);
	assert(n <= ra);

	if (v1fcp->avail.b != NULL)
		l = Tlen(v1fcp->avail);

	if (l >= n)
		return (VFP_OK);

	if (l == 0) {
		p = v1fcp->buf;
	} else {
		p = memmove(v1fcp->buf, v1fcp->avail.b, l);
		p += l;
	}

	if (ra > v1fcp->len)
		ra = v1fcp->len;

	assert(ra >= l);
	ra -= l;

	while (ra > 0 && l < n) {
		i = v1f_read(v1fcp->vc, v1fcp->htc, IOV(p, ra));
		if (i <= 0)
			return (VFP_Error(v1fcp->vc, "chunked read err"));

//		VSLb(vc->wrk->vsl, SLT_Debug, "need read %zu/%zu", i, ra);

		p += i;
		l += i;
		ra -= i;
	}

	v1fcp->avail.b = v1fcp->buf;
	v1fcp->avail.e = v1fcp->buf + l;

	assert(Tlen(v1fcp->avail) >= n);

	return (VFP_OK);
}

/*
 * read chunked data into the buffer, returning an existing readahead
 * and reading ahead up a maximum of ra
 */

static ssize_t
v1f_chunked_data(struct v1f_chunked_priv *v1fcp, void *ptr, size_t len,
    size_t ra)
{
	struct iovec iov[2];
	size_t r = 0, l;
	char *p;

	ASSERT_V1F_CHUNKED_PRIV(v1fcp);
	AN(ptr);
	AN(len);
	AN(ra <= v1fcp->len);

	p = ptr;

	if (v1fcp->avail.b != NULL) {
		l = Tlen(v1fcp->avail);
		l = vmin(l, len);

		memcpy(p, v1fcp->avail.b, l);
		p += l;
		len -= l;
		v1fcp->avail.b += l;
		r += l;

		if (Tlen(v1fcp->avail) == 0)
		    v1fcp->avail.b = v1fcp->avail.e = NULL;

		if (len == 0)
			return (r);
	}

	/* all readahead consumed */
	assert(v1fcp->avail.b == NULL || v1fcp->avail.b == v1fcp->avail.e);

	iov[0].iov_base = p;
	iov[0].iov_len = len;

	iov[1].iov_base = v1fcp->buf;
	iov[1].iov_len = ra;

	l = v1f_read(v1fcp->vc, v1fcp->htc, iov, 2);
	if (l <= len)
		return (r + l);

	/* we have readahead data */
	v1fcp->avail.b = v1fcp->buf;
	v1fcp->avail.e = v1fcp->buf + (l - len);


//	VSLb(vc->wrk->vsl, SLT_Debug, "data readahead %zu", l - len);

	return (r + len);
}


#define V1FNEED(priv, n, ra)						\
do {									\
	enum vfp_status vfps = v1f_chunked_need(priv, n, ra);		\
	if (vfps != VFP_OK)						\
		return (vfps);						\
} while(0)

/*--------------------------------------------------------------------
 * read (CR)?LF at the end of a chunk
 *
 */
static enum vfp_status
v1f_chunk_end(struct v1f_chunked_priv *v1fcp)
{

	V1FNEED(v1fcp, 1, 1);
	if (*v1fcp->avail.b == '\r') {
		v1fcp->avail.b++;
		V1FNEED(v1fcp, 1, 1);
	}
	if (*v1fcp->avail.b != '\n')
		return (VFP_Error(v1fcp->vc, "chunk end no NL"));
	v1fcp->avail.b++;
	return (VFP_OK);
}

/*--------------------------------------------------------------------
 * Parse a chunk header and, for VFP_OK, return size in a pointer
 *
 */

static enum vfp_status
v1f_chunked_hdr(struct v1f_chunked_priv *v1fcp, ssize_t *szp)
{
	const size_t ra_safe = 3;
	const unsigned maxdigits = 20;
	unsigned u;
	uintmax_t cll;
	ssize_t cl, ra;
	char *q;

	ASSERT_V1F_CHUNKED_PRIV(v1fcp);
	AN(szp);
	assert(*szp == -1);

	/* safe readahead is 0 digit + NL (end chunklen) + NL (end trailers)
	 *
	 * for the first non-zero difit, we can, conservatively, add 2: 1
	 * (minimum length) + 1 (NL).
	 *
	 * thereafter we just add 16 as a conservative lower bound
	 *
	 */
	ra = ra_safe;

	/* Skip leading whitespace. */
	do {
		V1FNEED(v1fcp, 1, ra);
		while (Tlen(v1fcp->avail) && vct_islws(*v1fcp->avail.b))
			v1fcp->avail.b++;
	} while (Tlen(v1fcp->avail) == 0);

	V1FNEED(v1fcp, 1, ra);
	if (!vct_ishex(*v1fcp->avail.b))
		return (VFP_Error(v1fcp->vc, "chunked header non-hex"));

	/* Collect hex digits and trailing space */
	assert(v1fcp->len >= maxdigits);
	for (u = 0; u < maxdigits; u++) {
		V1FNEED(v1fcp, u + 1, u + ra);
		if (!vct_ishex(v1fcp->avail.b[u]))
			break;
		/* extremely simple and conservative read-ahead adjustment */
		if (ra > ra_safe)
			ra += 16;
		else if (v1fcp->avail.b[u] != '0')
			ra += 2;
	}
	/* min readahead is one less now ( NL + NL ) */
	ra--;
	for (; u < maxdigits; u++) {
		V1FNEED(v1fcp, u + 1, u + ra);
		if (!vct_islws(v1fcp->avail.b[u]) || v1fcp->avail.b[u] == '\n')
			break;
		v1fcp->avail.b[u] = '\0';
	}
	if (u >= maxdigits)
		return (VFP_Error(v1fcp->vc, "chunked header too long"));

	if (v1fcp->avail.b[u] != '\n')
		return (VFP_Error(v1fcp->vc, "chunked header no NL"));

	v1fcp->avail.b[u] = '\0';

	cll = strtoumax(v1fcp->avail.b, &q, 16);
	if (q == NULL || *q != '\0')
		return (VFP_Error(v1fcp->vc, "chunked header number syntax"));
	cl = (ssize_t)cll;
	if (cl < 0 || (uintmax_t)cl != cll)
		return (VFP_Error(v1fcp->vc, "bogusly large chunk size"));

	v1fcp->avail.b += u + 1;
	*szp = cl;
	return (VFP_OK);
}


/*--------------------------------------------------------------------
 * Check if data is available
 */

static int
v1f_hasreadahead(struct v1f_chunked_priv *v1fcp)
{

	ASSERT_V1F_CHUNKED_PRIV(v1fcp);
	return (v1fcp->avail.b != NULL && Tlen(v1fcp->avail));
}


/*--------------------------------------------------------------------
 * Read a chunked HTTP object.
 *
 */

static enum vfp_status v_matchproto_(vfp_init_f)
v1f_chunked_init(VRT_CTX, struct vfp_ctx *vc, struct vfp_entry *vfe)
{
	struct http_conn *htc;
	struct v1f_chunked_priv *v1fcp;
	const unsigned sz = 24 + sizeof(struct v1f_chunked_priv);

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vc->wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	CAST_OBJ_NOTNULL(htc, vfe->priv1, HTTP_CONN_MAGIC);

	v1fcp = calloc(1, sz);
	AN(v1fcp);
	v1fcp->magic = V1F_CHUNKED_PRIV_MAGIC;
	v1fcp->len = sz - sizeof *v1fcp;
	v1fcp->htc = htc;
	v1fcp->vc = vc;
	vfe->priv1 = v1fcp;

	return (VFP_OK);
}


static enum vfp_status v_matchproto_(vfp_pull_f)
v1f_chunked_pull(struct vfp_ctx *vc, struct vfp_entry *vfe, void *ptr,
    ssize_t *lp)
{
	static enum vfp_status vfps;
	struct v1f_chunked_priv *v1fcp;
	struct http_conn *htc;
	ssize_t ra, l, lr;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	CAST_OBJ_NOTNULL(v1fcp, vfe->priv1, V1F_CHUNKED_PRIV_MAGIC);
	htc = v1fcp->htc;
	assert(vc == v1fcp->vc);
	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	AN(ptr);
	AN(lp);

	l = *lp;
	*lp = 0;
	if (vfe->priv2 == -1) {
		vfps = v1f_chunked_hdr(v1fcp, &vfe->priv2);
		if (vfps != VFP_OK)
			return (vfps);
	}
	if (vfe->priv2 > 0) {
		/* safe readahead is 4:
		 *
		 * NL (end chunk)
		 * 1 digit
		 * NL (end chunklen)
		 * NL (end trailers)
		 */
		ra = 0;

		if (vfe->priv2 <= l) {
			l = vfe->priv2;
			ra = 4;
		}

		lr = v1f_chunked_data(v1fcp, ptr, l, ra);
		if (lr <= 0)
			return (VFP_Error(vc, "chunked insufficient bytes"));
		*lp = lr;
		vfe->priv2 -= lr;
		if (vfe->priv2 != 0)
			return (VFP_OK);

		vfe->priv2 = -1;

		vfps = v1f_chunk_end(v1fcp);
		if (vfps != VFP_OK)
			return (vfps);

		/* only if some data of the next chunk header is available, read
		 * it to check if we can return VFP_END */
		if (! v1f_hasreadahead(v1fcp))
			return (VFP_OK);
		vfps = v1f_chunked_hdr(v1fcp, &vfe->priv2);
		if (vfps != VFP_OK)
			return (vfps);
		if (vfe->priv2 != 0)
			return (VFP_OK);
	}
	AZ(vfe->priv2);
	vfps = v1f_chunk_end(v1fcp);
	return (vfps == VFP_OK ? VFP_END : vfps);
}

static void v_matchproto_(vfp_fini_f)
v1f_chunked_fini(struct vfp_ctx *vc, struct vfp_entry *vfe)
{
	struct v1f_chunked_priv *v1fcp;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vc->wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	TAKE_OBJ_NOTNULL(v1fcp, &vfe->priv1, V1F_CHUNKED_PRIV_MAGIC);
	free(v1fcp);
}

static const struct vfp v1f_chunked = {
	.name = "V1F_CHUNKED",
	.init = v1f_chunked_init,
	.pull = v1f_chunked_pull,
	.fini = v1f_chunked_fini
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
	l = vmin(l, vfe->priv2);
	lr = v1f_read(vc, htc, IOV(p, l));
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
	lr = v1f_read(vc, htc, IOV(p, l));
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

	if (htc->body_status == BS_EOF) {
		assert(htc->content_length == -1);
		vfe = VFP_Push(vfc, &v1f_eof);
		if (vfe == NULL)
			return (ENOSPC);
		vfe->priv2 = 0;
	} else if (htc->body_status == BS_LENGTH) {
		assert(htc->content_length > 0);
		vfe = VFP_Push(vfc, &v1f_straight);
		if (vfe == NULL)
			return (ENOSPC);
		vfe->priv2 = htc->content_length;
	} else if (htc->body_status == BS_CHUNKED) {
		assert(htc->content_length == -1);
		vfe = VFP_Push(vfc, &v1f_chunked);
		if (vfe == NULL)
			return (ENOSPC);
		vfe->priv2 = -1;
	} else {
		WRONG("Wrong body_status");
	}
	vfe->priv1 = htc;
	return (0);
}
