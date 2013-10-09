/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2013 Varnish Software AS
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

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache.h"

#include "hash/hash_slinger.h"

#include "cache_backend.h"
#include "vcli_priv.h"
#include "vct.h"
#include "vtcp.h"

/*--------------------------------------------------------------------
 * Convert a string to a size_t safely
 */

static ssize_t
vbf_fetch_number(const char *nbr, int radix)
{
	uintmax_t cll;
	ssize_t cl;
	char *q;

	if (*nbr == '\0')
		return (-1);
	cll = strtoumax(nbr, &q, radix);
	if (q == NULL || *q != '\0')
		return (-1);

	cl = (ssize_t)cll;
	if((uintmax_t)cl != cll) /* Protect against bogusly large values */
		return (-1);
	return (cl);
}

/*--------------------------------------------------------------------*/

static int
vbf_fetch_straight(struct busyobj *bo, struct http_conn *htc, ssize_t cl)
{
	int i;

	assert(htc->body_status == BS_LENGTH);

	if (cl < 0) {
		return (VFP_Error(bo, "straight length field bogus"));
	} else if (cl == 0)
		return (0);

	i = bo->vfp->bytes(bo, htc, cl);
	if (i <= 0)
		return (VFP_Error(bo, "straight insufficient bytes"));
	return (0);
}

/*--------------------------------------------------------------------
 * Read a chunked HTTP object.
 *
 * XXX: Reading one byte at a time is pretty pessimal.
 */

static int
vbf_fetch_chunked(struct busyobj *bo, struct http_conn *htc)
{
	int i;
	char buf[20];		/* XXX: 20 is arbitrary */
	unsigned u;
	ssize_t cl;

	assert(htc->body_status == BS_CHUNKED);
	do {
		/* Skip leading whitespace */
		do {
			if (HTTP1_Read(htc, buf, 1) <= 0)
				return (VFP_Error(bo, "chunked read err"));
		} while (vct_islws(buf[0]));

		if (!vct_ishex(buf[0]))
			return (VFP_Error(bo, "chunked header non-hex"));

		/* Collect hex digits, skipping leading zeros */
		for (u = 1; u < sizeof buf; u++) {
			do {
				if (HTTP1_Read(htc, buf + u, 1) <= 0)
					return (VFP_Error(bo,
					    "chunked read err"));
			} while (u == 1 && buf[0] == '0' && buf[u] == '0');
			if (!vct_ishex(buf[u]))
				break;
		}

		if (u >= sizeof buf)
			return (VFP_Error(bo,"chunked header too long"));

		/* Skip trailing white space */
		while(vct_islws(buf[u]) && buf[u] != '\n')
			if (HTTP1_Read(htc, buf + u, 1) <= 0)
				return (VFP_Error(bo, "chunked read err"));

		if (buf[u] != '\n')
			return (VFP_Error(bo,"chunked header no NL"));

		buf[u] = '\0';
		cl = vbf_fetch_number(buf, 16);
		if (cl < 0)
			return (VFP_Error(bo,"chunked header number syntax"));

		if (cl > 0 && bo->vfp->bytes(bo, htc, cl) <= 0)
			return (VFP_Error(bo, "chunked read err"));

		i = HTTP1_Read(htc, buf, 1);
		if (i <= 0)
			return (VFP_Error(bo, "chunked read err"));
		if (buf[0] == '\r' && HTTP1_Read( htc, buf, 1) <= 0)
			return (VFP_Error(bo, "chunked read err"));
		if (buf[0] != '\n')
			return (VFP_Error(bo,"chunked tail no NL"));
	} while (cl > 0);
	return (0);
}

/*--------------------------------------------------------------------*/

static void
vbf_fetch_eof(struct busyobj *bo, struct http_conn *htc)
{

	assert(htc->body_status == BS_EOF);
	if (bo->vfp->bytes(bo, htc, SSIZE_MAX) < 0)
		(void)VFP_Error(bo,"eof socket fail");
}

/*--------------------------------------------------------------------
 * Pass the request body to the backend
 */

static int __match_proto__(req_body_iter_f)
vbf_iter_req_body(struct req *req, void *priv, void *ptr, size_t l)
{
	struct worker *wrk;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CAST_OBJ_NOTNULL(wrk, priv, WORKER_MAGIC);

	if (l > 0) {
		(void)WRW_Write(wrk, ptr, l);
		if (WRW_Flush(wrk))
			return (-1);
	}
	return (0);
}

/*--------------------------------------------------------------------
 * Send request, and receive the HTTP protocol response, but not the
 * response body.
 *
 * Return value:
 *	-1 failure, not retryable
 *	 0 success
 *	 1 failure which can be retried.
 */

int
V1F_fetch_hdr(struct worker *wrk, struct busyobj *bo, struct req *req)
{
	struct vbc *vc;
	struct http *hp;
	enum htc_status_e hs;
	int retry = -1;
	int i, first;
	struct http_conn *htc;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_ORNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	htc = &bo->htc;

	AN(bo->director);

	hp = bo->bereq;

	bo->vbc = VDI_GetFd(NULL, bo);
	if (bo->vbc == NULL) {
		VSLb(bo->vsl, SLT_FetchError, "no backend connection");
		return (-1);
	}
	vc = bo->vbc;
	if (vc->recycled)
		retry = 1;

	/*
	 * Now that we know our backend, we can set a default Host:
	 * header if one is necessary.  This cannot be done in the VCL
	 * because the backend may be chosen by a director.
	 */
	if (!http_GetHdr(bo->bereq, H_Host, NULL))
		VDI_AddHostHeader(bo->bereq, vc);

	(void)VTCP_blocking(vc->fd);	/* XXX: we should timeout instead */
	WRW_Reserve(wrk, &vc->fd, bo->vsl, bo->t_fetch);
	(void)HTTP1_Write(wrk, hp, 0);	/* XXX: stats ? */

	/* Deal with any message-body the request might (still) have */
	i = 0;

	if (req != NULL) {
		i = HTTP1_IterateReqBody(req, vbf_iter_req_body, wrk);
		if (req->req_body_status == REQ_BODY_DONE)
			retry = -1;
	}

	if (WRW_FlushRelease(wrk) || i != 0) {
		VSLb(bo->vsl, SLT_FetchError, "backend write error: %d (%s)",
		    errno, strerror(errno));
		VDI_CloseFd(&bo->vbc);
		/* XXX: other cleanup ? */
		return (retry);
	}

	/* XXX is this the right place? */
	VSC_C_main->backend_req++;

	/* Receive response */

	HTTP1_Init(htc, bo->ws, vc->fd, vc->vsl,
	    cache_param->http_resp_size,
	    cache_param->http_resp_hdr_len);

	VTCP_set_read_timeout(vc->fd, vc->first_byte_timeout);

	first = 1;
	do {
		hs = HTTP1_Rx(htc);
		if (hs == HTTP1_OVERFLOW) {
			VSLb(bo->vsl, SLT_FetchError,
			    "http %sread error: overflow",
			    first ? "first " : "");
			VDI_CloseFd(&bo->vbc);
			/* XXX: other cleanup ? */
			return (-1);
		}
		if (hs == HTTP1_ERROR_EOF) {
			VSLb(bo->vsl, SLT_FetchError, "http %sread error: EOF",
			    first ? "first " : "");
			VDI_CloseFd(&bo->vbc);
			/* XXX: other cleanup ? */
			return (retry);
		}
		if (first) {
			retry = -1;
			first = 0;
			VTCP_set_read_timeout(vc->fd,
			    vc->between_bytes_timeout);
		}
	} while (hs != HTTP1_COMPLETE);

	hp = bo->beresp;

	if (HTTP1_DissectResponse(hp, htc)) {
		VSLb(bo->vsl, SLT_FetchError, "http format error");
		VDI_CloseFd(&bo->vbc);
		/* XXX: other cleanup ? */
		return (-1);
	}
	return (0);
}

/*--------------------------------------------------------------------
 * This function is either called by the requesting thread OR by a
 * dedicated body-fetch work-thread.
 *
 * We get passed the busyobj in the priv arg, and we inherit a
 * refcount on it, which we must release, when done fetching.
 */

void
V1F_fetch_body(struct worker *wrk, struct busyobj *bo)
{
	int cls;
	struct storage *st;
	ssize_t cl;
	struct http_conn *htc;
	struct object *obj;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	htc = &bo->htc;
	CHECK_OBJ_ORNULL(bo->vbc, VBC_MAGIC);
	obj = bo->fetch_obj;
	CHECK_OBJ_NOTNULL(obj, OBJECT_MAGIC);
	CHECK_OBJ_NOTNULL(obj->http, HTTP_MAGIC);

	assert(bo->state == BOS_FETCHING);

	/*
	 * XXX: The busyobj needs a dstat, but it is not obvious which one
	 * XXX: it should be (own/borrowed).  For now borrow the wrk's.
	 */
	AZ(bo->stats);
	bo->stats = &wrk->stats;

	AN(bo->vfp);
	AZ(bo->vgz_rx);
	assert(VTAILQ_EMPTY(&obj->store));

	/* XXX: pick up estimate from objdr ? */
	cl = 0;
	cls = bo->should_close;
	switch (htc->body_status) {
	case BS_NONE:
		break;
	case BS_ZERO:
		break;
	case BS_LENGTH:
		cl = vbf_fetch_number(bo->h_content_length, 10);

		bo->vfp->begin(bo, cl);
		if (bo->state == BOS_FETCHING && cl > 0)
			cls |= vbf_fetch_straight(bo, htc, cl);
		if (bo->vfp->end(bo))
			assert(bo->state == BOS_FAILED);
		break;
	case BS_CHUNKED:
		bo->vfp->begin(bo, cl > 0 ? cl : 0);
		if (bo->state == BOS_FETCHING)
			cls |= vbf_fetch_chunked(bo, htc);
		if (bo->vfp->end(bo))
			assert(bo->state == BOS_FAILED);
		break;
	case BS_EOF:
		bo->vfp->begin(bo, cl > 0 ? cl : 0);
		if (bo->state == BOS_FETCHING)
			vbf_fetch_eof(bo, htc);
		cls = 1;
		if (bo->vfp->end(bo))
			assert(bo->state == BOS_FAILED);
		break;
	case BS_ERROR:
		cls |= VFP_Error(bo, "error incompatible Transfer-Encoding");
		break;
	default:
		INCOMPL();
	}
	AZ(bo->vgz_rx);

	/*
	 * Trim or delete the last segment, if any
	 */

	st = VTAILQ_LAST(&bo->fetch_obj->store, storagehead);
	/* XXX: Temporary:  Only trim if we are not streaming */
	if (st != NULL && !bo->do_stream) {
		/* XXX: is any of this safe under streaming ? */
		if (st->len == 0) {
			VTAILQ_REMOVE(&bo->fetch_obj->store, st, list);
			STV_free(st);
		} else if (st->len < st->space) {
			STV_trim(st, st->len, 1);
		}
	}

	bo->vfp = NULL;

	VSLb(bo->vsl, SLT_Fetch_Body, "%u(%s) cls %d",
	    htc->body_status, body_status_2str(htc->body_status), cls);

	http_Teardown(bo->bereq);
	http_Teardown(bo->beresp);

	if (bo->vbc != NULL) {
		if (cls)
			VDI_CloseFd(&bo->vbc);
		else
			VDI_RecycleFd(&bo->vbc);
	}
	AZ(bo->vbc);

	if (bo->state == BOS_FAILED) {
		wrk->stats.fetch_failed++;
		obj->len = 0;
	} else {
		assert(bo->state == BOS_FETCHING);

		VSLb(bo->vsl, SLT_Length, "%zd", obj->len);

		{
		/* Sanity check fetch methods accounting */
			ssize_t uu;

			uu = 0;
			VTAILQ_FOREACH(st, &obj->store, list)
				uu += st->len;
			if (bo->do_stream)
				/* Streaming might have started freeing stuff */
				assert(uu <= obj->len);

			else
				assert(uu == obj->len);
		}
	}
	bo->stats = NULL;
}
