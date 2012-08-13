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

#include "cache.h"

#include "vct.h"
#include "vtim.h"

/*--------------------------------------------------------------------*/

static void
res_dorange(const struct req *req, const char *r, ssize_t *plow, ssize_t *phigh)
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

void
RES_BuildHttp(struct req *req)
{
	char time_str[30];

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	http_ClrHeader(req->resp);
	http_FilterResp(req->obj->http, req->resp, 0);

	if (!(req->res_mode & RES_LEN)) {
		http_Unset(req->resp, H_Content_Length);
	} else if (cache_param->http_range_support) {
		/* We only accept ranges if we know the length */
		http_SetHeader(req->resp, "Accept-Ranges: bytes");
	}

	if (req->res_mode & RES_GUNZIP)
		http_Unset(req->resp, H_Content_Encoding);

	if (req->obj->response == 200
	    && req->http->conds && RFC2616_Do_Cond(req)) {
		req->wantbody = 0;
		http_SetResp(req->resp, "HTTP/1.1", 304, "Not Modified");
		http_Unset(req->resp, H_Content_Length);
	} else if (req->res_mode & RES_CHUNKED)
		http_SetHeader(req->resp, "Transfer-Encoding: chunked");

	http_Unset(req->resp, H_Date);
	VTIM_format(VTIM_real(), time_str);
	http_PrintfHeader(req->resp, "Date: %s", time_str);

	if (req->vxid != req->obj->vxid)
		http_PrintfHeader(req->resp,
		    "X-Varnish: %u %u", req->vxid, req->obj->vxid);
	else
		http_PrintfHeader(req->resp, "X-Varnish: %u", req->vxid);
	http_PrintfHeader(req->resp, "Age: %.0f",
	    req->obj->exp.age + req->t_resp -
	    req->obj->exp.entered);
	http_SetHeader(req->resp, "Via: 1.1 varnish");
	http_PrintfHeader(req->resp, "Connection: %s",
	    req->doclose ? "close" : "keep-alive");
}

/*--------------------------------------------------------------------
 * We have a gzip'ed object and need to ungzip it for a client which
 * does not understand gzip.
 * XXX: handle invalid gzip data better (how ?)
 */

static void
res_WriteGunzipObj(struct req *req)
{
	struct storage *st;
	unsigned u = 0;
	struct vgz *vg;
	int i;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	vg = VGZ_NewUngzip(req->vsl, "U D -");
	AZ(VGZ_WrwInit(vg));

	VTAILQ_FOREACH(st, &req->obj->store, list) {
		CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
		CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
		u += st->len;

		i = VGZ_WrwGunzip(req, vg, st->ptr, st->len);
		/* XXX: error check */
		(void)i;
	}
	VGZ_WrwFlush(req, vg);
	(void)VGZ_Destroy(&vg);
	assert(u == req->obj->len);
}

/*--------------------------------------------------------------------*/

static void
res_WriteDirObj(struct req *req, ssize_t low, ssize_t high)
{
	ssize_t u = 0;
	size_t ptr, off, len;
	struct storage *st;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	ptr = 0;
	VTAILQ_FOREACH(st, &req->obj->store, list) {
		CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
		CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
		u += st->len;
		len = st->len;
		off = 0;
		if (ptr + len <= low) {
			/* This segment is too early */
			ptr += len;
			continue;
		}
		if (ptr < low) {
			/* Chop front of segment off */
			off += (low - ptr);
			len -= (low - ptr);
			ptr += (low - ptr);
		}
		if (ptr + len > high)
			/* Chop tail of segment off */
			len = 1 + high - ptr;

		ptr += len;

		req->acct_req.bodybytes += len;
		(void)WRW_Write(req->wrk, st->ptr + off, len);
	}
	assert(u == req->obj->len);
}

/*--------------------------------------------------------------------
 * Deliver an object.
 * Attempt optimizations like 304 and 206 here.
 */

void
RES_WriteObj(struct req *req)
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
		res_dorange(req, r, &low, &high);

	WRW_Reserve(req->wrk, &req->sp->fd, req->vsl, req->t_resp);

	/*
	 * Send HTTP protocol header, unless interior ESI object
	 */
	if (!(req->res_mode & RES_ESI_CHILD))
		req->acct_req.hdrbytes +=
		    http_Write(req->wrk, req->resp, 1);

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
		res_WriteGunzipObj(req);
	} else if (req->res_mode & RES_GUNZIP) {
		res_WriteGunzipObj(req);
	} else {
		res_WriteDirObj(req, low, high);
	}

	if (req->res_mode & RES_CHUNKED &&
	    !(req->res_mode & RES_ESI_CHILD))
		WRW_EndChunk(req->wrk);

	if (WRW_FlushRelease(req->wrk) && req->sp->fd >= 0)
		SES_Close(req->sp, SC_REM_CLOSE);
}
