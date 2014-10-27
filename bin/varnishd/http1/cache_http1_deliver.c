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

static int __match_proto__(vdp_bytes)
v1d_bytes(struct req *req, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{
	ssize_t wl = 0;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	(void)priv;
	if (act == VDP_INIT || act == VDP_FINI)
		return (0);

	AZ(req->vdp_nxt);		/* always at the bottom of the pile */

	if (len > 0)
		wl = V1L_Write(req->wrk, ptr, len);
	req->acct.resp_bodybytes += len;
	if (act > VDP_NULL && V1L_Flush(req->wrk))
		return (-1);
	if (len != wl)
		return (-1);
	return (0);
}

/*--------------------------------------------------------------------*/

struct v1rp {
	unsigned		magic;
#define V1RP_MAGIC		0xb886e711
	ssize_t			range_low;
	ssize_t			range_high;
	ssize_t			range_off;
};

static int __match_proto__(vdp_bytes)
v1d_range_bytes(struct req *req, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{
	int retval = 0;
	ssize_t l;
	const char *p = ptr;
	struct v1rp *v1rp;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	if (act == VDP_INIT)
		return (0);
	if (act == VDP_FINI) {
		*priv = NULL;
		return (0);
	}
	CAST_OBJ_NOTNULL(v1rp, *priv, V1RP_MAGIC);
	l = v1rp->range_low - v1rp->range_off;
	if (l > 0) {
		if (l > len)
			l = len;
		v1rp->range_off += l;
		p += l;
		len -= l;
	}
	l = v1rp->range_high - v1rp->range_off;
	if (l > len)
		l = len;
	if (l > 0)
		retval = VDP_bytes(req, act, p, l);
	else if (act > VDP_NULL)
		retval = VDP_bytes(req, act, p, 0);
	v1rp->range_off += len;
	return (retval);
}

/*--------------------------------------------------------------------*/


static void
v1d_dorange(struct req *req, struct busyobj *bo, const char *r)
{
	ssize_t len, low, high, has_low;
	struct v1rp *v1rp;

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

	v1rp = WS_Alloc(req->ws, sizeof *v1rp);
	XXXAN(v1rp);
	INIT_OBJ(v1rp, V1RP_MAGIC);
	v1rp->range_off = 0;
	v1rp->range_low = low;
	v1rp->range_high = high + 1;
	VDP_push(req, v1d_range_bytes, v1rp);
}

/*--------------------------------------------------------------------*/

static enum objiter_status
v1d_WriteDirObj(struct req *req)
{
	enum objiter_status ois;
	ssize_t len;
	void *oi;
	void *ptr;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	oi = ObjIterBegin(req->wrk, req->objcore);
	XXXAN(oi);
	AZ(req->synth_body);

	do {
		ois = ObjIter(req->objcore, oi, &ptr, &len);
		switch(ois) {
		case OIS_DONE:
			AZ(len);
			break;
		case OIS_ERROR:
			break;
		case OIS_DATA:
		case OIS_STREAM:
			if (VDP_bytes(req,
			     ois == OIS_DATA ? VDP_NULL : VDP_FLUSH,  ptr, len))
				ois = OIS_ERROR;
			break;
		default:
			WRONG("Wrong OIS value");
		}
	} while (ois == OIS_DATA || ois == OIS_STREAM);
	ObjIterEnd(req->objcore, &oi);
	return (ois);
}

/*--------------------------------------------------------------------
 */
void
V1D_Deliver(struct req *req, struct busyobj *bo)
{
	const char *r;
	enum objiter_status ois;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);

	req->res_mode = 0;

	if (req->esi_level > 0)
		req->res_mode |= RES_ESI_CHILD;

	if (!req->disable_esi && ObjGetattr(req->wrk, req->objcore,
	    OA_ESIDATA, NULL) != NULL) {
		/* In ESI mode, we can't know the aggregate length */
		req->res_mode |= RES_ESI;
		RFC2616_Weaken_Etag(req->resp);
	}

	if (req->res_mode & (RES_ESI_CHILD|RES_ESI)) {
		/* nothing */
	} else if (http_IsStatus(req->resp, 304)) {
		req->res_mode &= ~RES_LEN;
		http_Unset(req->resp, H_Content_Length);
		req->wantbody = 0;
	} else if (bo != NULL) {
		/* Streaming, decide CHUNKED/EOF later */
	} else if ((req->objcore->flags & OC_F_PASS) && !req->wantbody) {
		/*
		 * if we pass a HEAD the C-L header may already be in the
		 * object and it will not match the actual storage length
		 * which is zero.
		 * Hand that C-L header back to client.
		 */
		req->res_mode |= RES_LEN;
	} else {
		req->res_mode |= RES_LEN;
		http_Unset(req->resp, H_Content_Length);
		http_PrintfHeader(req->resp,
		    "Content-Length: %ju", (uintmax_t)ObjGetLen(
		    req->wrk, req->objcore));
	}

	if (cache_param->http_gzip_support &&
	    ObjCheckFlag(req->wrk, req->objcore, OF_GZIPED) &&
	    !RFC2616_Req_Gzip(req->http)) {
		/*
		 * We don't know what it uncompresses to
		 * XXX: we could cache that, but would still deliver
		 * XXX: with multiple writes because of the gunzip buffer
		 */
		req->res_mode &= ~RES_LEN;
		req->res_mode |= RES_GUNZIP;
	}

	if (!(req->res_mode & (RES_LEN|RES_CHUNKED|RES_EOF))) {
		/* We havn't chosen yet, do so */
		if (!req->wantbody) {
			/* Nothing */
		} else if (req->http->protover >= 11) {
			req->res_mode |= RES_CHUNKED;
		} else {
			req->res_mode |= RES_EOF;
			req->doclose = SC_TX_EOF;
		}
	}
	VSLb(req->vsl, SLT_Debug, "RES_MODE %x", req->res_mode);

	if (!(req->res_mode & RES_LEN))
		http_Unset(req->resp, H_Content_Length);

	if (req->res_mode & RES_GUNZIP)
		http_Unset(req->resp, H_Content_Encoding);

	if (req->res_mode & RES_CHUNKED)
		http_SetHeader(req->resp, "Transfer-Encoding: chunked");

	http_SetHeader(req->resp,
	    req->doclose ? "Connection: close" : "Connection: keep-alive");

	if (req->esi_level == 0) {
		req->vdp_nxt = 0;
		VTAILQ_INIT(&req->vdp);
		VDP_push(req, v1d_bytes, NULL);
	}

	if (
	    req->wantbody &&
	    req->esi_level == 0 &&
	    cache_param->http_range_support &&
	    http_IsStatus(req->resp, 200)) {
		http_SetHeader(req->resp, "Accept-Ranges: bytes");
		if (http_GetHdr(req->http, H_Range, &r))
			v1d_dorange(req, bo, r);
	}

	V1L_Reserve(req->wrk, req->wrk->aws, &req->sp->fd, req->vsl,
	    req->t_prev);

	/*
	 * Send HTTP protocol header, unless interior ESI object
	 */
	if (!(req->res_mode & RES_ESI_CHILD))
		req->acct.resp_hdrbytes +=
		    HTTP1_Write(req->wrk, req->resp, HTTP1_Resp);

	if (req->res_mode & RES_CHUNKED)
		V1L_Chunked(req->wrk);

	ois = OIS_DONE;
	if (!req->wantbody) {
		/* This was a HEAD or conditional request */
	} else if (req->esi_level > 0) {
		if (req->gzip_resp &&
		    ObjCheckFlag(req->wrk, req->objcore, OF_GZIPED) &&
		    !(req->res_mode & RES_ESI)) {
			if (bo != NULL)
				VBO_waitstate(bo, BOS_FINISHED);
			ESI_DeliverChild(req);
		} else {
			if (req->gzip_resp &&
			    !ObjCheckFlag(req->wrk, req->objcore, OF_GZIPED))
				VDP_push(req, VED_pretend_gzip, NULL);
			else if (!req->gzip_resp &&
			    ObjCheckFlag(req->wrk, req->objcore, OF_GZIPED))
				VDP_push(req, VDP_gunzip, NULL);

			if (req->res_mode & RES_ESI)
				ESI_Deliver(req);
			else
				ois = v1d_WriteDirObj(req);
		}
	} else {
		if (req->res_mode & RES_GUNZIP)
			VDP_push(req, VDP_gunzip, NULL);
		if (req->res_mode & RES_ESI)
			ESI_Deliver(req);
		else
			ois = v1d_WriteDirObj(req);
	}
	(void)VDP_bytes(req, VDP_FLUSH, NULL, 0);

	if (ois == OIS_DONE &&
	    (req->res_mode & RES_CHUNKED) &&
	    !(req->res_mode & RES_ESI_CHILD))
		V1L_EndChunk(req->wrk);

	if ((V1L_FlushRelease(req->wrk) || ois != OIS_DONE) &&
	    req->sp->fd >= 0)
		SES_Close(req->sp, SC_REM_CLOSE);
	VDP_close(req);
}
