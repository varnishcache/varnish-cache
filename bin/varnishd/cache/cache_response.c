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
res_dorange(const struct sess *sp, const char *r, ssize_t *plow, ssize_t *phigh)
{
	ssize_t low, high, has_low;
	struct req *req;

	req = sp->req;
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
RES_BuildHttp(const struct sess *sp)
{
	char time_str[30];
	struct req *req;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	req = sp->req;
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	http_ClrHeader(req->resp);
	req->resp->logtag = HTTP_Tx;
	http_FilterResp(req->obj->http, req->resp, 0);

	if (!(req->res_mode & RES_LEN)) {
		http_Unset(req->resp, H_Content_Length);
	} else if (cache_param->http_range_support) {
		/* We only accept ranges if we know the length */
		http_SetHeader(req->resp, "Accept-Ranges: bytes");
	}

	if (req->res_mode & RES_CHUNKED)
		http_SetHeader(req->resp, "Transfer-Encoding: chunked");

	VTIM_format(VTIM_real(), time_str);
	http_PrintfHeader(req->resp, "Date: %s", time_str);

	if (req->xid != req->obj->xid)
		http_PrintfHeader(req->resp,
		    "X-Varnish: %u %u", req->xid, req->obj->xid);
	else
		http_PrintfHeader(req->resp, "X-Varnish: %u", req->xid);
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
res_WriteGunzipObj(const struct sess *sp)
{
	struct storage *st;
	unsigned u = 0;
	struct vgz *vg;
	int i;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	vg = VGZ_NewUngzip(sp->wrk->vsl, "U D -");
	AZ(VGZ_WrwInit(vg));

	VTAILQ_FOREACH(st, &sp->req->obj->store, list) {
		CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
		CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
		u += st->len;

		VSC_C_main->n_objwrite++;

		i = VGZ_WrwGunzip(sp->wrk, vg, st->ptr, st->len);
		/* XXX: error check */
		(void)i;
	}
	VGZ_WrwFlush(sp->wrk, vg);
	(void)VGZ_Destroy(&vg);
	assert(u == sp->req->obj->len);
}

/*--------------------------------------------------------------------*/

static void
res_WriteDirObj(const struct sess *sp, ssize_t low, ssize_t high)
{
	ssize_t u = 0;
	size_t ptr, off, len;
	struct storage *st;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	ptr = 0;
	VTAILQ_FOREACH(st, &sp->req->obj->store, list) {
		CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
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

		sp->wrk->acct_tmp.bodybytes += len;
		VSC_C_main->n_objwrite++;
		(void)WRW_Write(sp->wrk, st->ptr + off, len);
	}
	assert(u == sp->req->obj->len);
}

/*--------------------------------------------------------------------
 * Deliver an object.
 * Attempt optimizations like 304 and 206 here.
 */

void
RES_WriteObj(struct sess *sp)
{
	char *r;
	ssize_t low, high;
	struct req *req;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	req = sp->req;
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	WRW_Reserve(sp->wrk, &sp->fd);

	if (req->obj->response == 200 &&
	    req->http->conds &&
	    RFC2616_Do_Cond(sp)) {
		req->wantbody = 0;
		http_SetResp(req->resp, "HTTP/1.1", 304, "Not Modified");
		http_Unset(req->resp, H_Content_Length);
		http_Unset(req->resp, H_Transfer_Encoding);
	}

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
		res_dorange(sp, r, &low, &high);

	/*
	 * Always remove C-E if client don't grok it
	 */
	if (req->res_mode & RES_GUNZIP)
		http_Unset(req->resp, H_Content_Encoding);

	/*
	 * Send HTTP protocol header, unless interior ESI object
	 */
	if (!(req->res_mode & RES_ESI_CHILD))
		sp->wrk->acct_tmp.hdrbytes +=
		    http_Write(sp->wrk, req->resp, 1);

	if (!req->wantbody)
		req->res_mode &= ~RES_CHUNKED;

	if (req->res_mode & RES_CHUNKED)
		WRW_Chunked(sp->wrk);

	if (!req->wantbody) {
		/* This was a HEAD or conditional request */
	} else if (req->obj->len == 0) {
		/* Nothing to do here */
	} else if (req->res_mode & RES_ESI) {
		ESI_Deliver(sp);
	} else if (req->res_mode & RES_ESI_CHILD && req->gzip_resp) {
		ESI_DeliverChild(sp);
	} else if (req->res_mode & RES_ESI_CHILD &&
	    !req->gzip_resp && req->obj->gziped) {
		res_WriteGunzipObj(sp);
	} else if (req->res_mode & RES_GUNZIP) {
		res_WriteGunzipObj(sp);
	} else {
		res_WriteDirObj(sp, low, high);
	}

	if (req->res_mode & RES_CHUNKED &&
	    !(req->res_mode & RES_ESI_CHILD))
		WRW_EndChunk(sp->wrk);

	if (WRW_FlushRelease(sp->wrk) && sp->fd >= 0)
		SES_Close(sp, "remote closed");
}

/*--------------------------------------------------------------------*/

void
RES_StreamStart(struct sess *sp)
{
	struct req *req;

	req = sp->req;
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	AZ(req->res_mode & RES_ESI_CHILD);
	AN(req->wantbody);
	AZ(req->stream_vgz);
	AZ(req->stream_next);
	AZ(req->stream_front);

	WRW_Reserve(sp->wrk, &sp->fd);

	if (req->res_mode & RES_GUNZIP) {
		req->stream_vgz = VGZ_NewUngzip(sp->wrk->vsl, "U S -");
		AZ(VGZ_WrwInit(req->stream_vgz));
		http_Unset(req->resp, H_Content_Encoding);
	}

	if (!(req->res_mode & RES_CHUNKED) &&
	    sp->wrk->busyobj->h_content_length != NULL)
		http_PrintfHeader(sp->req->resp,
		    "Content-Length: %s", sp->wrk->busyobj->h_content_length);

	sp->wrk->acct_tmp.hdrbytes +=
	    http_Write(sp->wrk, sp->req->resp, 1);

	if (req->res_mode & RES_CHUNKED)
		WRW_Chunked(sp->wrk);
}

void
RES_StreamPoll(struct worker *wrk)
{
	struct storage *st;
	struct busyobj *bo;
	struct req *req;
	ssize_t l, l2;
	void *ptr;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	bo = wrk->busyobj;
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->fetch_obj, OBJECT_MAGIC);
	req = wrk->sp->req;
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	if (bo->fetch_obj->len == req->stream_next)
		return;
	assert(bo->fetch_obj->len > req->stream_next);
	l = req->stream_front;
	VTAILQ_FOREACH(st, &bo->fetch_obj->store, list) {
		if (st->len + l <= req->stream_next) {
			l += st->len;
			continue;
		}
		l2 = st->len + l - req->stream_next;
		ptr = st->ptr + (req->stream_next - l);
		if (wrk->sp->req->res_mode & RES_GUNZIP)
			(void)VGZ_WrwGunzip(wrk, req->stream_vgz, ptr, l2);
		else
			(void)WRW_Write(wrk, ptr, l2);
		l += st->len;
		req->stream_next += l2;
	}
	if (!(wrk->sp->req->res_mode & RES_GUNZIP))
		(void)WRW_Flush(wrk);

	if (bo->fetch_obj->objcore == NULL ||
	    (bo->fetch_obj->objcore->flags & OC_F_PASS)) {
		/*
		 * This is a pass object, release storage as soon as we
		 * have delivered it.
		 */
		while (1) {
			st = VTAILQ_FIRST(&bo->fetch_obj->store);
			if (st == NULL ||
			    req->stream_front + st->len > req->stream_next)
				break;
			VTAILQ_REMOVE(&bo->fetch_obj->store, st, list);
			req->stream_front += st->len;
			STV_free(st);
		}
	}
}

void
RES_StreamEnd(struct sess *sp)
{
	struct req *req;

	req = sp->req;
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	if (req->res_mode & RES_GUNZIP) {
		AN(req->stream_vgz);
		VGZ_WrwFlush(sp->wrk, req->stream_vgz);
		(void)VGZ_Destroy(&req->stream_vgz);
	}
	if (req->res_mode & RES_CHUNKED && !(req->res_mode & RES_ESI_CHILD))
		WRW_EndChunk(sp->wrk);
	if (WRW_FlushRelease(sp->wrk))
		SES_Close(sp, "remote closed");
	req->stream_vgz = NULL;
	req->stream_next = 0;
	req->stream_front = 0;
}
