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
#include "vtim.h"

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

static enum vfp_status __match_proto__(vfp_pull_f)
v1f_pull_straight(struct busyobj *bo, void *p, ssize_t *lp, intptr_t *priv)
{
	ssize_t l, lr;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	if (p == vfp_init)
		return (VFP_OK);
	if (p == vfp_fini)
		return (VFP_ERROR);
	AN(p);
	AN(lp);
	AN(priv);

	l = *lp;
	*lp = 0;

	if (!*priv)		// XXX: Optimize Content-Len: 0 out earlier
		return (VFP_END);
	if (*priv < l)
		l = *priv;
	lr = HTTP1_Read(&bo->htc, p, l);
	bo->acct.beresp_bodybytes += lr;
	if (lr <= 0)
		return (VFP_Error(bo, "straight insufficient bytes"));
	*lp = lr;
	*priv -= lr;
	if (*priv == 0)
		return (VFP_END);
	return (VFP_OK);
}

/*--------------------------------------------------------------------
 * Read a chunked HTTP object.
 *
 * XXX: Reading one byte at a time is pretty pessimal.
 */

static enum vfp_status __match_proto__(vfp_pull_f)
v1f_pull_chunked(struct busyobj *bo, void *p, ssize_t *lp, intptr_t *priv)
{
	int i;
	char buf[20];		/* XXX: 20 is arbitrary */
	unsigned u;
	ssize_t cl, l, lr;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	if (p == vfp_init)
		return (VFP_OK);
	if (p == vfp_fini)
		return (VFP_ERROR);
	AN(p);
	AN(lp);
	AN(priv);
	l = *lp;
	*lp = 0;
	if (*priv == -1) {
		/* Skip leading whitespace */
		do {
			lr = HTTP1_Read(&bo->htc, buf, 1);
			if (lr <= 0)
				return (VFP_Error(bo, "chunked read err"));
			bo->acct.beresp_bodybytes += lr;
		} while (vct_islws(buf[0]));

		if (!vct_ishex(buf[0]))
			 return (VFP_Error(bo, "chunked header non-hex"));

		/* Collect hex digits, skipping leading zeros */
		for (u = 1; u < sizeof buf; u++) {
			do {
				lr = HTTP1_Read(&bo->htc, buf + u, 1);
				if (lr <= 0)
					return (VFP_Error(bo,
					    "chunked read err"));
				bo->acct.beresp_bodybytes += lr;
			} while (u == 1 && buf[0] == '0' && buf[u] == '0');
			if (!vct_ishex(buf[u]))
				break;
		}

		if (u >= sizeof buf)
			return (VFP_Error(bo,"chunked header too long"));

		/* Skip trailing white space */
		while(vct_islws(buf[u]) && buf[u] != '\n') {
			lr = HTTP1_Read(&bo->htc, buf + u, 1);
			if (lr <= 0)
				return (VFP_Error(bo, "chunked read err"));
			bo->acct.beresp_bodybytes += lr;
		}

		if (buf[u] != '\n')
			return (VFP_Error(bo,"chunked header no NL"));

		buf[u] = '\0';

		cl = vbf_fetch_number(buf, 16);
		if (cl < 0)
			return (VFP_Error(bo,"chunked header number syntax"));
		*priv = cl;
	}
	if (*priv > 0) {
		if (*priv < l)
			l = *priv;
		lr = HTTP1_Read(&bo->htc, p, l);
		if (lr <= 0)
			return (VFP_Error(bo, "straight insufficient bytes"));
		bo->acct.beresp_bodybytes += lr;
		*lp = lr;
		*priv -= lr;
		if (*priv == 0)
			*priv = -1;
		return (VFP_OK);
	}
	AZ(*priv);
	i = HTTP1_Read(&bo->htc, buf, 1);
	if (i <= 0)
		return (VFP_Error(bo, "chunked read err"));
	bo->acct.beresp_bodybytes += i;
	if (buf[0] == '\r' && HTTP1_Read(&bo->htc, buf, 1) <= 0)
		return (VFP_Error(bo, "chunked read err"));
	if (buf[0] != '\n')
		return (VFP_Error(bo,"chunked tail no NL"));
	return (VFP_END);
}

/*--------------------------------------------------------------------*/

static enum vfp_status __match_proto__(vfp_pull_f)
v1f_pull_eof(struct busyobj *bo, void *p, ssize_t *lp, intptr_t *priv)
{
	ssize_t l, lr;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	if (p == vfp_init)
		return (VFP_OK);
	if (p == vfp_fini)
		return (VFP_ERROR);
	AN(p);
	AN(lp);
	AN(priv);

	l = *lp;
	*lp = 0;
	lr = HTTP1_Read(&bo->htc, p, l);
	if (lr < 0)
		return (VFP_Error(bo,"eof socket fail"));
	if (lr == 0)
		return (VFP_END);
	*lp = lr;
	return (VFP_OK);
}

/*--------------------------------------------------------------------
 */

ssize_t
V1F_Setup_Fetch(struct busyobj *bo)
{
	struct http_conn *htc;
	ssize_t cl;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	htc = &bo->htc;
	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(bo->vbc, VBC_MAGIC);

	switch(htc->body_status) {
	case BS_EOF:
		VFP_Push(bo, v1f_pull_eof, 0);
		return(-1);
	case BS_LENGTH:
		cl = vbf_fetch_number(bo->h_content_length, 10);
		VFP_Push(bo, v1f_pull_straight, cl);
		return (cl);
	case BS_CHUNKED:
		VFP_Push(bo, v1f_pull_chunked, -1);
		return (-1);
	default:
		break;
	}
	return (-1);
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
	int i, j, first;
	struct http_conn *htc;
	ssize_t hdrbytes;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_ORNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	htc = &bo->htc;

	AN(bo->director);

	hp = bo->bereq;

	bo->vbc = VDI_GetFd(bo);
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
	WRW_Reserve(wrk, &vc->fd, bo->vsl, bo->t_prev);
	hdrbytes = HTTP1_Write(wrk, hp, 0);

	/* Deal with any message-body the request might (still) have */
	i = 0;

	if (req != NULL) {
		i = HTTP1_IterateReqBody(req, vbf_iter_req_body, wrk);
		if (req->req_body_status == REQ_BODY_DONE)
			retry = -1;
		if (req->req_body_status == REQ_BODY_FAIL) {
			VSLb(bo->vsl, SLT_FetchError,
			    "req.body read error: %d (%s)",
			    errno, strerror(errno));
			req->doclose = SC_RX_BODY;
			retry = -1;
		}
	}

	j = WRW_FlushRelease(wrk, &bo->acct.bereq_hdrbytes);
	if (bo->acct.bereq_hdrbytes > hdrbytes) {
		bo->acct.bereq_bodybytes = bo->acct.bereq_hdrbytes - hdrbytes;
		bo->acct.bereq_hdrbytes = hdrbytes;
	}
	if (j != 0 || i != 0) {
		VSLb(bo->vsl, SLT_FetchError, "backend write error: %d (%s)",
		    errno, strerror(errno));
		VSLb_ts_busyobj(bo, "Bereq", W_TIM_real(wrk));
		VDI_CloseFd(&bo->vbc, &bo->acct);
		/* XXX: other cleanup ? */
		return (retry);
	}
	VSLb_ts_busyobj(bo, "Bereq", W_TIM_real(wrk));

	VSC_C_main->backend_req++;

	/* Receive response */

	HTTP1_Init(htc, bo->ws, vc->fd, vc->vsl,
	    cache_param->http_resp_size,
	    cache_param->http_resp_hdr_len);
	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(&bo->htc, HTTP_CONN_MAGIC);

	VTCP_set_read_timeout(vc->fd, vc->first_byte_timeout);

	first = 1;
	do {
		hs = HTTP1_Rx(htc);
		if (hs == HTTP1_OVERFLOW) {
			bo->acct.beresp_hdrbytes += Tlen(htc->rxbuf);
			VSLb(bo->vsl, SLT_FetchError,
			    "http %sread error: overflow",
			    first ? "first " : "");
			VDI_CloseFd(&bo->vbc, &bo->acct);
			/* XXX: other cleanup ? */
			return (-1);
		}
		if (hs == HTTP1_ERROR_EOF) {
			bo->acct.beresp_hdrbytes += Tlen(htc->rxbuf);
			VSLb(bo->vsl, SLT_FetchError, "http %sread error: EOF",
			    first ? "first " : "");
			VDI_CloseFd(&bo->vbc, &bo->acct);
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
	bo->acct.beresp_hdrbytes += Tlen(htc->rxbuf);

	hp = bo->beresp;

	if (HTTP1_DissectResponse(hp, htc)) {
		VSLb(bo->vsl, SLT_FetchError, "http format error");
		VDI_CloseFd(&bo->vbc, &bo->acct);
		/* XXX: other cleanup ? */
		return (-1);
	}
	return (0);
}
