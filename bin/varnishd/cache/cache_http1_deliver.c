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

#include "cache.h"

#include "vct.h"

/*--------------------------------------------------------------------*/

static void
v1d_dorange(const struct req *req, const char *r, ssize_t *plow, ssize_t *phigh)
{
	ssize_t low, high, has_low;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	assert(req->obj->response == 200);
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

	if (low >= req->obj->len)
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
			low = req->obj->len - high;
			high = req->obj->len - 1;
		}
	} else
		high = req->obj->len - 1;
	if (*r != '\0')
		return;

	if (high >= req->obj->len)
		high = req->obj->len - 1;

	if (low > high)
		return;

	http_PrintfHeader(req->resp, "Content-Range: bytes %jd-%jd/%jd",
	    (intmax_t)low, (intmax_t)high, (intmax_t)req->obj->len);
	http_Unset(req->resp, H_Content_Length);
	assert(req->res_mode & RES_LEN);
	http_PrintfHeader(req->resp, "Content-Length: %jd",
	    (intmax_t)(1 + high - low));
	http_SetResp(req->resp, "HTTP/1.1", 206, "Partial Content");

	*plow = low;
	*phigh = high;
}

/*--------------------------------------------------------------------*/

static void
v1d_BuildHttp(struct req *req)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->obj, OBJECT_MAGIC);

	req->res_mode = 0;

	if (!req->disable_esi && req->obj->esidata != NULL) {
		/* In ESI mode, we can't know the aggregate length */
		req->res_mode &= ~RES_LEN;
		req->res_mode |= RES_ESI;
	} else if (req->obj->objcore->busyobj == NULL) {
		/* XXX: Not happy with this convoluted test */
		req->res_mode |= RES_LEN;
		if (!(req->obj->objcore->flags & OC_F_PASS) ||
		    req->obj->len != 0) {
			http_Unset(req->resp, H_Content_Length);
			http_PrintfHeader(req->resp,
			    "Content-Length: %zd", req->obj->len);
		}
		if (cache_param->http_range_support)
			http_SetHeader(req->resp, "Accept-Ranges: bytes");
	}

	if (req->esi_level > 0) {
		/* Included ESI object, always CHUNKED or EOF */
		req->res_mode &= ~RES_LEN;
		req->res_mode |= RES_ESI_CHILD;
	}

	if (cache_param->http_gzip_support && req->obj->gziped &&
	    !RFC2616_Req_Gzip(req->http)) {
		/*
		 * We don't know what it uncompresses to
		 * XXX: we could cache that
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

	if (req->obj->objcore != NULL
	    && !(req->obj->objcore->flags & OC_F_PASS)
	    && req->obj->response == 200
	    && req->http->conds && RFC2616_Do_Cond(req)) {
		req->wantbody = 0;
		http_SetResp(req->resp, "HTTP/1.1", 304, "Not Modified");
		http_Unset(req->resp, H_Content_Length);
	} else if (req->res_mode & RES_CHUNKED)
		http_SetHeader(req->resp, "Transfer-Encoding: chunked");

	http_PrintfHeader(req->resp, "Connection: %s",
	    req->doclose ? "close" : "keep-alive");
}

/*--------------------------------------------------------------------
 * We have a gzip'ed object and need to ungzip it for a client which
 * does not understand gzip.
 * XXX: handle invalid gzip data better (how ?)
 */

static void
v1d_WriteGunzipObj(struct req *req)
{
	unsigned u = 0;
	struct vgz *vg;
	struct objiter *oi;
	void *ptr;
	ssize_t len;
	int i;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	vg = VGZ_NewUngzip(req->vsl, "U D -");
	AZ(VGZ_WrwInit(vg));

	oi = ObjIterBegin(req->obj);
	XXXAN(oi);

	while (ObjIter(oi, &ptr, &len)) {
		CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
		u += len;

		i = VGZ_WrwGunzip(req, vg, ptr, len);
		/* XXX: error check */
		(void)i;
	}
	VGZ_WrwFlush(req, vg);
	(void)VGZ_Destroy(&vg);
	assert(u == req->obj->len);
	ObjIterEnd(&oi);
}

/*--------------------------------------------------------------------*/

static void
v1d_WriteDirObj(struct req *req, ssize_t low, ssize_t high)
{
	ssize_t u = 0;
	ssize_t idx, skip, len;
	struct objiter *oi;
	void *ptr;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	oi = ObjIterBegin(req->obj);
	XXXAN(oi);

	idx = 0;
	while (ObjIter(oi, &ptr, &len)) {
		u += len;
		skip = 0;
		if (idx + len <= low) {
			/* This segment is too early */
			idx += len;
			continue;
		}
		if (idx < low) {
			/* Chop front of segment off */
			skip += (low - idx);
			len -= (low - idx);
			idx += (low - idx);
		}
		if (idx + len > high)
			/* Chop tail of segment off */
			len = 1 + high - idx;

		idx += len;

		req->acct_req.bodybytes += len;
		(void)WRW_Write(req->wrk, (char*)ptr + skip, len);
	}
	assert(u == req->obj->len);
	ObjIterEnd(&oi);
}

/*--------------------------------------------------------------------
 * Deliver an object.
 * Attempt optimizations like 304 and 206 here.
 */

static void
v1d_WriteObj(struct req *req)
{
	char *r;
	ssize_t low, high;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	/*
	 * If nothing special planned, we can attempt Range support
	 */
	low = 0;
	high = req->obj->len - 1;
	if (
	    req->wantbody &&
	    (req->res_mode & RES_LEN) &&
	    !(req->res_mode & (RES_ESI|RES_ESI_CHILD|RES_GUNZIP)) &&
	    cache_param->http_range_support &&
	    req->obj->response == 200 &&
	    http_GetHdr(req->http, H_Range, &r))
		v1d_dorange(req, r, &low, &high);

	WRW_Reserve(req->wrk, &req->sp->fd, req->vsl, req->t_resp);

	/*
	 * Send HTTP protocol header, unless interior ESI object
	 */
	if (!(req->res_mode & RES_ESI_CHILD))
		req->acct_req.hdrbytes +=
		    HTTP1_Write(req->wrk, req->resp, 1);

	if (!req->wantbody)
		req->res_mode &= ~RES_CHUNKED;

	if (req->res_mode & RES_CHUNKED)
		WRW_Chunked(req->wrk);

	if (!req->wantbody) {
		/* This was a HEAD or conditional request */
	} else if (req->obj->len == 0) {
		/* Nothing to do here */
	} else if (req->res_mode & RES_ESI) {
		ESI_Deliver(req);
	} else if (req->res_mode & RES_ESI_CHILD && req->gzip_resp) {
		ESI_DeliverChild(req);
	} else if (req->res_mode & RES_ESI_CHILD &&
	    !req->gzip_resp && req->obj->gziped) {
		v1d_WriteGunzipObj(req);
	} else if (req->res_mode & RES_GUNZIP) {
		v1d_WriteGunzipObj(req);
	} else {
		v1d_WriteDirObj(req, low, high);
	}

	if (req->res_mode & RES_CHUNKED &&
	    !(req->res_mode & RES_ESI_CHILD))
		WRW_EndChunk(req->wrk);

	if (WRW_FlushRelease(req->wrk) && req->sp->fd >= 0)
		SES_Close(req->sp, SC_REM_CLOSE);
}

void
V1D_Deliver(struct req *req)
{
	while (req->obj->objcore->busyobj)
		(void)usleep(10000);
	
	v1d_BuildHttp(req);
	v1d_WriteObj(req);
}
