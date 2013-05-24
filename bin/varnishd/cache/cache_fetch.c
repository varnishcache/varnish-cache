/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
#include "vcl.h"
#include "vct.h"
#include "vtcp.h"
#include "vtim.h"

static unsigned fetchfrag;

/*--------------------------------------------------------------------
 * We want to issue the first error we encounter on fetching and
 * supress the rest.  This function does that.
 *
 * Other code is allowed to look at busyobj->fetch_failed to bail out
 *
 * For convenience, always return -1
 */

int
VBF_Error2(struct busyobj *bo, const char *error, const char *more)
{

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	if (bo->state == BOS_FETCHING) {
		if (more == NULL)
			VSLb(bo->vsl, SLT_FetchError, "%s", error);
		else
			VSLb(bo->vsl, SLT_FetchError, "%s: %s", error, more);
	}
	bo->state = BOS_FAILED;
	return (-1);
}

int
VBF_Error(struct busyobj *bo, const char *error)
{
	return(VBF_Error2(bo, error, NULL));
}

/*--------------------------------------------------------------------
 * VFP_NOP
 *
 * This fetch-processor does nothing but store the object.
 * It also documents the API
 */

/*--------------------------------------------------------------------
 * VFP_BEGIN
 *
 * Called to set up stuff.
 *
 * 'estimate' is the estimate of the number of bytes we expect to receive,
 * as seen on the socket, or zero if unknown.
 */
static void __match_proto__(vfp_begin_f)
vfp_nop_begin(void *priv, size_t estimate)
{
	struct busyobj *bo;

	CAST_OBJ_NOTNULL(bo, priv, BUSYOBJ_MAGIC);

	if (estimate > 0)
		(void)VBF_GetStorage(bo, estimate);
}

/*--------------------------------------------------------------------
 * VFP_BYTES
 *
 * Process (up to) 'bytes' from the socket.
 *
 * Return -1 on error, issue VBF_Error()
 *	will not be called again, once error happens.
 * Return 0 on EOF on socket even if bytes not reached.
 * Return 1 when 'bytes' have been processed.
 */

static int __match_proto__(vfp_bytes_f)
vfp_nop_bytes(void *priv, struct http_conn *htc, ssize_t bytes)
{
	ssize_t l, wl;
	struct storage *st;
	struct busyobj *bo;

	CAST_OBJ_NOTNULL(bo, priv, BUSYOBJ_MAGIC);

	while (bytes > 0) {
		st = VBF_GetStorage(bo, 0);
		if (st == NULL)
			return(-1);
		l = st->space - st->len;
		if (l > bytes)
			l = bytes;
		wl = HTTP1_Read(htc, st->ptr + st->len, l);
		if (wl <= 0)
			return (wl);
		st->len += wl;
		VBO_extend(bo, wl);
		bytes -= wl;
	}
	return (1);
}

/*--------------------------------------------------------------------
 * VFP_END
 *
 * Finish & cleanup
 *
 * Return -1 for error
 * Return 0 for OK
 */

static int __match_proto__(vfp_end_f)
vfp_nop_end(void *priv)
{
	struct storage *st;
	struct busyobj *bo;

	CAST_OBJ_NOTNULL(bo, priv, BUSYOBJ_MAGIC);
	st = VTAILQ_LAST(&bo->fetch_obj->store, storagehead);
	if (st == NULL)
		return (0);

	if (st->len == 0) {
		VTAILQ_REMOVE(&bo->fetch_obj->store, st, list);
		STV_free(st);
		return (0);
	}
	if (st->len < st->space)
		STV_trim(st, st->len, 1);
	return (0);
}

static struct vfp vfp_nop = {
	.begin	=	vfp_nop_begin,
	.bytes	=	vfp_nop_bytes,
	.end	=	vfp_nop_end,
};

/*--------------------------------------------------------------------
 * Fetch Storage to put object into.
 *
 */

struct storage *
VBF_GetStorage(struct busyobj *bo, ssize_t sz)
{
	ssize_t l;
	struct storage *st;
	struct object *obj;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	obj = bo->fetch_obj;
	CHECK_OBJ_NOTNULL(obj, OBJECT_MAGIC);
	st = VTAILQ_LAST(&obj->store, storagehead);
	if (st != NULL && st->len < st->space)
		return (st);

	l = fetchfrag;
	if (l == 0)
		l = sz;
	if (l == 0)
		l = cache_param->fetch_chunksize;
	st = STV_alloc(bo, l);
	if (st == NULL) {
		(void)VBF_Error(bo, "Could not get storage");
		return (NULL);
	}
	AZ(st->len);
	VTAILQ_INSERT_TAIL(&obj->store, st, list);
	return (st);
}

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
		return (VBF_Error(bo, "straight length field bogus"));
	} else if (cl == 0)
		return (0);

	i = bo->vfp->bytes(bo, htc, cl);
	if (i <= 0)
		return (VBF_Error(bo, "straight insufficient bytes"));
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
				return (VBF_Error(bo, "chunked read err"));
		} while (vct_islws(buf[0]));

		if (!vct_ishex(buf[0]))
			return (VBF_Error(bo, "chunked header non-hex"));

		/* Collect hex digits, skipping leading zeros */
		for (u = 1; u < sizeof buf; u++) {
			do {
				if (HTTP1_Read(htc, buf + u, 1) <= 0)
					return (VBF_Error(bo,
					    "chunked read err"));
			} while (u == 1 && buf[0] == '0' && buf[u] == '0');
			if (!vct_ishex(buf[u]))
				break;
		}

		if (u >= sizeof buf)
			return (VBF_Error(bo,"chunked header too long"));

		/* Skip trailing white space */
		while(vct_islws(buf[u]) && buf[u] != '\n')
			if (HTTP1_Read(htc, buf + u, 1) <= 0)
				return (VBF_Error(bo, "chunked read err"));

		if (buf[u] != '\n')
			return (VBF_Error(bo,"chunked header no NL"));

		buf[u] = '\0';
		cl = vbf_fetch_number(buf, 16);
		if (cl < 0)
			return (VBF_Error(bo,"chunked header number syntax"));

		if (cl > 0 && bo->vfp->bytes(bo, htc, cl) <= 0)
			return (VBF_Error(bo, "chunked read err"));

		i = HTTP1_Read(htc, buf, 1);
		if (i <= 0)
			return (VBF_Error(bo, "chunked read err"));
		if (buf[0] == '\r' && HTTP1_Read( htc, buf, 1) <= 0)
			return (VBF_Error(bo, "chunked read err"));
		if (buf[0] != '\n')
			return (VBF_Error(bo,"chunked tail no NL"));
	} while (cl > 0);
	return (0);
}

/*--------------------------------------------------------------------*/

static void
vbf_fetch_eof(struct busyobj *bo, struct http_conn *htc)
{

	assert(htc->body_status == BS_EOF);
	if (bo->vfp->bytes(bo, htc, SSIZE_MAX) < 0)
		(void)VBF_Error(bo,"eof socket fail");
}

/*--------------------------------------------------------------------
 * Pass the request body to the backend
 */

static int __match_proto__(req_body_iter_f)
vbf_iter_req_body(struct req *req, void *priv, void *ptr, size_t l)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	(void)priv;

	if (l > 0) {
		(void)WRW_Write(req->wrk, ptr, l);
		if (WRW_Flush(req->wrk))
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

static int
vbf_fetch_hdr(struct worker *wrk, struct busyobj *bo, struct req *req)
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
		i = HTTP1_IterateReqBody(req, vbf_iter_req_body, NULL);
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

static void
vbf_fetch_body(struct worker *wrk, void *priv)
{
	int cls;
	struct storage *st;
	int mklen;
	ssize_t cl;
	struct http_conn *htc;
	struct object *obj;
	struct busyobj *bo;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(bo, priv, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->vbc, VBC_MAGIC);
	obj = bo->fetch_obj;
	CHECK_OBJ_NOTNULL(obj, OBJECT_MAGIC);
	CHECK_OBJ_NOTNULL(obj->http, HTTP_MAGIC);

	assert(bo->state == BOS_INVALID);

	/*
	 * XXX: The busyobj needs a dstat, but it is not obvious which one
	 * XXX: it should be (own/borrowed).  For now borrow the wrk's.
	 */
	AZ(bo->stats);
	bo->stats = &wrk->stats;

	htc = &bo->htc;

	if (bo->vfp == NULL)
		bo->vfp = &vfp_nop;

	AN(bo->vfp);
	AZ(bo->vgz_rx);
	AZ(VTAILQ_FIRST(&obj->store));

	bo->state = BOS_FETCHING;

	/* XXX: pick up estimate from objdr ? */
	cl = 0;
	cls = 0;
	switch (htc->body_status) {
	case BS_NONE:
		mklen = 0;
		break;
	case BS_ZERO:
		mklen = 1;
		break;
	case BS_LENGTH:
		cl = vbf_fetch_number(bo->h_content_length, 10);

		bo->vfp->begin(bo, cl > 0 ? cl : 0);
		if (bo->state == BOS_FETCHING)
			cls = vbf_fetch_straight(bo, htc, cl);
		mklen = 1;
		if (bo->vfp->end(bo))
			assert(bo->state == BOS_FAILED);
		break;
	case BS_CHUNKED:
		bo->vfp->begin(bo, cl > 0 ? cl : 0);
		if (bo->state == BOS_FETCHING)
			cls = vbf_fetch_chunked(bo, htc);
		mklen = 1;
		if (bo->vfp->end(bo))
			assert(bo->state == BOS_FAILED);
		break;
	case BS_EOF:
		bo->vfp->begin(bo, cl > 0 ? cl : 0);
		if (bo->state == BOS_FETCHING)
			vbf_fetch_eof(bo, htc);
		mklen = 1;
		cls = 1;
		if (bo->vfp->end(bo))
			assert(bo->state == BOS_FAILED);
		break;
	case BS_ERROR:
		cls = VBF_Error(bo, "error incompatible Transfer-Encoding");
		mklen = 0;
		break;
	default:
		cls = 0;
		mklen = 0;
		INCOMPL();
	}
	AZ(bo->vgz_rx);

	/*
	 * We always call vfp_nop_end() to ditch or trim the last storage
	 * segment, to avoid having to replicate that code in all vfp's.
	 */
	AZ(vfp_nop_end(bo));

	bo->vfp = NULL;

	VSLb(bo->vsl, SLT_Fetch_Body, "%u(%s) cls %d mklen %d",
	    htc->body_status, body_status_2str(htc->body_status),
	    cls, mklen);

	http_Teardown(bo->bereq);
	http_Teardown(bo->beresp);

	if (bo->state == BOS_FAILED) {
		wrk->stats.fetch_failed++;
		VDI_CloseFd(&bo->vbc);
		obj->len = 0;
		EXP_Clr(&obj->exp);
		EXP_Rearm(obj);
	} else {
		assert(bo->state == BOS_FETCHING);

		if (cls == 0 && bo->should_close)
			cls = 1;

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

		if (mklen > 0) {
			http_Unset(obj->http, H_Content_Length);
			http_PrintfHeader(obj->http,
			    "Content-Length: %zd", obj->len);
		}

		if (cls)
			VDI_CloseFd(&bo->vbc);
		else
			VDI_RecycleFd(&bo->vbc);


		/* XXX: Atomic assignment, needs volatile/membar ? */
		bo->state = BOS_FINISHED;
	}
	if (obj->objcore->objhead != NULL)
		HSH_Complete(obj->objcore);
	bo->stats = NULL;
	VBO_DerefBusyObj(wrk, &bo);
}

/*--------------------------------------------------------------------
 * Copy req->bereq and run it by VCL::vcl_backend_fetch{}
 */

static void
vbf_make_bereq(struct worker *wrk, const struct req *req, struct busyobj *bo)
{
	int i;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->fetch_objcore, OBJCORE_MAGIC);

	AN(bo->director);
	AZ(bo->vbc);
	AZ(bo->should_close);
	AZ(bo->storage_hint);

	HTTP_Setup(bo->bereq, bo->ws, bo->vsl, HTTP_Bereq);
	http_FilterReq(bo->bereq, req->http,
	    bo->do_pass ? HTTPH_R_PASS : HTTPH_R_FETCH);
	if (!bo->do_pass) {
		// XXX: Forcing GET should happen in vcl_miss{} ?
		http_ForceGet(bo->bereq);
		if (cache_param->http_gzip_support) {
			/*
			 * We always ask the backend for gzip, even if the
			 * client doesn't grok it.  We will uncompress for
			 * the minority of clients which don't.
			 */
			http_Unset(bo->bereq, H_Accept_Encoding);
			http_SetHeader(bo->bereq, "Accept-Encoding: gzip");
		}
	}

	// Don't let VCL reset do_pass
	i = bo->do_pass;
	VCL_backend_fetch_method(bo->vcl, wrk, NULL, bo, bo->bereq->ws);
	bo->do_pass |= i;

	http_PrintfHeader(bo->bereq,
	    "X-Varnish: %u", bo->vsl->wid & VSL_IDENTMASK);
}

/*--------------------------------------------------------------------
 */

static void
vbf_proc_resp(struct worker *wrk, struct busyobj *bo)
{
	int i;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	/*
	 * These two headers can be spread over multiple actual headers
	 * and we rely on their content outside of VCL, so collect them
	 * into one line here.
	 */
	http_CollectHdr(bo->beresp, H_Cache_Control);
	http_CollectHdr(bo->beresp, H_Vary);

	/*
	 * Figure out how the fetch is supposed to happen, before the
	 * headers are adultered by VCL
	 * NB: Also sets other wrk variables
	 */
	bo->htc.body_status = RFC2616_Body(bo, &wrk->stats);

	bo->err_code = http_GetStatus(bo->beresp);

	/*
	 * What does RFC2616 think about TTL ?
	 */
	EXP_Clr(&bo->exp);
	bo->exp.entered = W_TIM_real(wrk);
	RFC2616_Ttl(bo);

	/* pass from vclrecv{} has negative TTL */
	if (bo->fetch_objcore->objhead == NULL)
		bo->exp.ttl = -1.;

	AZ(bo->do_esi);

	// Don't let VCL reset do_pass
	i = bo->do_pass;
	VCL_backend_response_method(bo->vcl, wrk, NULL, bo,
	    bo->beresp->ws);
	bo->do_pass |= i;

	if (bo->do_pass)
		bo->fetch_objcore->flags |= OC_F_PASS;

	/*
	 * The VCL variables beresp.do_g[un]zip tells us how we want the
	 * object processed before it is stored.
	 *
	 * The backend Content-Encoding header tells us what we are going
	 * to receive, which we classify in the following three classes:
	 *
	 *	"Content-Encoding: gzip"	--> object is gzip'ed.
	 *	no Content-Encoding		--> object is not gzip'ed.
	 *	anything else			--> do nothing wrt gzip
	 *
	 */

	/* We do nothing unless the param is set */
	if (!cache_param->http_gzip_support)
		bo->do_gzip = bo->do_gunzip = 0;

	bo->is_gzip = http_HdrIs(bo->beresp, H_Content_Encoding, "gzip");

	bo->is_gunzip = !http_GetHdr(bo->beresp, H_Content_Encoding, NULL);

	/* It can't be both */
	assert(bo->is_gzip == 0 || bo->is_gunzip == 0);

	/* We won't gunzip unless it is gzip'ed */
	if (bo->do_gunzip && !bo->is_gzip)
		bo->do_gunzip = 0;

	/* If we do gunzip, remove the C-E header */
	if (bo->do_gunzip)
		http_Unset(bo->beresp, H_Content_Encoding);

	/* We wont gzip unless it is ungziped */
	if (bo->do_gzip && !bo->is_gunzip)
		bo->do_gzip = 0;

	/* If we do gzip, add the C-E header */
	if (bo->do_gzip)
		http_SetHeader(bo->beresp, "Content-Encoding: gzip");

	/* But we can't do both at the same time */
	assert(bo->do_gzip == 0 || bo->do_gunzip == 0);

	/* ESI takes precedence and handles gzip/gunzip itself */
	if (bo->do_esi)
		bo->vfp = &vfp_esi;
	else if (bo->do_gunzip)
		bo->vfp = &vfp_gunzip;
	else if (bo->do_gzip)
		bo->vfp = &vfp_gzip;
	else if (bo->is_gzip)
		bo->vfp = &vfp_testgzip;

}

int
VBF_Fetch(struct worker *wrk, struct req *req)
{
	struct http *hp, *hp2;
	char *b;
	uint16_t nhttp;
	unsigned l;
	struct vsb *vary = NULL;
	int varyl = 0;
	struct busyobj *bo;
	struct object *obj;
	int i;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);

	bo = req->busyobj;
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	assert(bo->refcount == 2);
	CHECK_OBJ_NOTNULL(bo->vcl, VCL_CONF_MAGIC);

	bo->fetch_objcore = req->objcore;
	req->objcore = NULL;

	vbf_make_bereq(wrk, req, bo);
	xxxassert (wrk->handling == VCL_RET_FETCH);

	HTTP_Setup(bo->beresp, bo->ws, bo->vsl, HTTP_Beresp);

	if (!bo->do_pass)
		req = NULL;

	i = vbf_fetch_hdr(wrk, bo, req);
	/*
	 * If we recycle a backend connection, there is a finite chance
	 * that the backend closed it before we get a request to it.
	 * Do a single retry in that case.
	 */
	if (i == 1) {
		VSC_C_main->backend_retry++;
		i = vbf_fetch_hdr(wrk, bo, req);
	}

	if (bo->do_pass)
		req = NULL;
	AZ(req);

	if (i) {
		wrk->handling = VCL_RET_ERROR;
		bo->err_code = 503;
	} else {
		vbf_proc_resp(wrk, bo);
		if (wrk->handling != VCL_RET_DELIVER)
			VDI_CloseFd(&bo->vbc);
	}

	if (wrk->handling != VCL_RET_DELIVER) {
		/* Clean up partial fetch */
		AZ(bo->vbc);

		if (bo->fetch_objcore->objhead != NULL ||
		    wrk->handling == VCL_RET_RESTART ||
		    wrk->handling == VCL_RET_ERROR) {
			CHECK_OBJ_NOTNULL(bo->fetch_objcore, OBJCORE_MAGIC);
			AZ(HSH_Deref(&wrk->stats, bo->fetch_objcore, NULL));
			bo->fetch_objcore = NULL;
		}
		assert(bo->refcount == 2);
		bo->storage_hint = NULL;
		bo->director = NULL;
		VBO_DerefBusyObj(wrk, &bo);

		switch (wrk->handling) {
		case VCL_RET_ERROR:
			return (-1);
		case VCL_RET_RESTART:
			INCOMPL();
		default:
			WRONG("Illegal action in vcl_fetch{}");
		}
	}

	if (bo->fetch_objcore->objhead == NULL)
		AN(bo->do_pass);

	/* No reason to try streaming a non-existing body */
	if (bo->htc.body_status == BS_NONE)
		bo->do_stream = 0;

	l = http_EstimateWS(bo->beresp,
	    bo->do_pass ? HTTPH_R_PASS : HTTPH_A_INS, &nhttp);

	/* Create Vary instructions */
	if (bo->fetch_objcore->objhead != NULL) {
		varyl = VRY_Create(bo, &vary);
		if (varyl > 0) {
			AN(vary);
			assert(varyl == VSB_len(vary));
			l += varyl;
		} else if (varyl < 0) {
			/* Vary parse error */
			AZ(vary);
			AZ(HSH_Deref(&wrk->stats, bo->fetch_objcore, NULL));
			bo->fetch_objcore = NULL;
			VDI_CloseFd(&bo->vbc);
			return (-1);
		} else
			/* No vary */
			AZ(vary);
	}

	if (bo->exp.ttl < cache_param->shortlived || bo->do_pass == 1)
		bo->storage_hint = TRANSIENT_STORAGE;

	/*
	 * Space for producing a Content-Length: header including padding
	 * A billion gigabytes is enough for anybody.
	 */
	l += strlen("Content-Length: XxxXxxXxxXxxXxxXxx") + sizeof(void *);

	AZ(bo->stats);
	bo->stats = &wrk->stats;
	obj = STV_NewObject(bo, bo->storage_hint, l, nhttp);
	if (obj == NULL) {
		/*
		 * Try to salvage the transaction by allocating a
		 * shortlived object on Transient storage.
		 */
		if (bo->exp.ttl > cache_param->shortlived)
			bo->exp.ttl = cache_param->shortlived;
		bo->exp.grace = 0.0;
		bo->exp.keep = 0.0;
		obj = STV_NewObject(bo, TRANSIENT_STORAGE, l, nhttp);
	}
	bo->stats = NULL;
	if (obj == NULL) {
		AZ(HSH_Deref(&wrk->stats, bo->fetch_objcore, NULL));
		bo->fetch_objcore = NULL;
		VDI_CloseFd(&bo->vbc);
		return (-1);
	}
	CHECK_OBJ_NOTNULL(obj, OBJECT_MAGIC);

	bo->storage_hint = NULL;

	AZ(bo->fetch_obj);
	bo->fetch_obj = obj;

	if (bo->do_gzip || (bo->is_gzip && !bo->do_gunzip))
		obj->gziped = 1;

	if (vary != NULL) {
		obj->vary = (void *)WS_Copy(obj->http->ws,
		    VSB_data(vary), varyl);
		AN(obj->vary);
		VRY_Validate(obj->vary);
		VSB_delete(vary);
	}

	obj->vxid = bo->vsl->wid;
	obj->response = bo->err_code;
	WS_Assert(obj->ws_o);

	/* Filter into object */
	hp = bo->beresp;
	hp2 = obj->http;

	hp2->logtag = HTTP_Obj;
	http_FilterResp(hp, hp2, bo->do_pass ? HTTPH_R_PASS : HTTPH_A_INS);
	http_CopyHome(hp2);

	if (http_GetHdr(hp, H_Last_Modified, &b))
		obj->last_modified = VTIM_parse(b);
	else
		obj->last_modified = floor(bo->exp.entered);

	assert(WRW_IsReleased(wrk));

	/*
	 * Ready to fetch the body
	 */

	assert(bo->refcount == 2);	/* one for each thread */

	if (obj->objcore->objhead != NULL) {
		EXP_Insert(obj);
		AN(obj->objcore->ban);
		AZ(obj->ws_o->overflow);
		HSH_Unbusy(&wrk->stats, obj->objcore);
	}

	vbf_fetch_body(wrk, bo);

	assert(bo->refcount == 1);

	if (obj->objcore->objhead != NULL)
		HSH_Ref(obj->objcore);

	if (bo->state == BOS_FAILED) {
		/* handle early failures */
		(void)HSH_Deref(&wrk->stats, NULL, &obj);
		return (-1);
	}

	assert(WRW_IsReleased(wrk));
	return (0);
}

/*--------------------------------------------------------------------
 * Debugging aids
 */

static void
debug_fragfetch(struct cli *cli, const char * const *av, void *priv)
{
	(void)priv;
	(void)cli;
	fetchfrag = strtoul(av[2], NULL, 0);
}

static struct cli_proto debug_cmds[] = {
	{ "debug.fragfetch", "debug.fragfetch",
		"\tEnable fetch fragmentation\n", 1, 1, "d", debug_fragfetch },
	{ NULL }
};

/*--------------------------------------------------------------------
 *
 */

void
VBF_Init(void)
{

	CLI_AddFuncs(debug_cmds);
}
