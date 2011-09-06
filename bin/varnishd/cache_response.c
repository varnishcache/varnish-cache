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

#include <sys/types.h>
#include <sys/time.h>

#include <stdio.h>
#include <stdlib.h>

#include "cache.h"
#include "stevedore.h"
#include "vct.h"

/*--------------------------------------------------------------------*/

static void
res_dorange(const struct sess *sp, const char *r, ssize_t *plow, ssize_t *phigh)
{
	ssize_t low, high, has_low;

	assert(sp->obj->response == 200);
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

	if (low >= sp->obj->len)
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
			low = sp->obj->len - high;
			high = sp->obj->len - 1;
		}
	} else
		high = sp->obj->len - 1;
	if (*r != '\0')
		return;

	if (high >= sp->obj->len)
		high = sp->obj->len - 1;

	if (low > high)
		return;

	http_PrintfHeader(sp->wrk, sp->fd, sp->wrk->resp,
	    "Content-Range: bytes %jd-%jd/%jd",
	    (intmax_t)low, (intmax_t)high, (intmax_t)sp->obj->len);
	http_Unset(sp->wrk->resp, H_Content_Length);
	assert(sp->wrk->res_mode & RES_LEN);
	http_PrintfHeader(sp->wrk, sp->fd, sp->wrk->resp,
	    "Content-Length: %jd", (intmax_t)(1 + high - low));
	http_SetResp(sp->wrk->resp, "HTTP/1.1", 206, "Partial Content");

	*plow = low;
	*phigh = high;
}

/*--------------------------------------------------------------------*/

void
RES_BuildHttp(const struct sess *sp)
{
	char time_str[30];

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);


	http_ClrHeader(sp->wrk->resp);
	sp->wrk->resp->logtag = HTTP_Tx;
	http_CopyResp(sp->wrk->resp, sp->obj->http);
	http_FilterFields(sp->wrk, sp->fd, sp->wrk->resp, sp->obj->http,
	    HTTPH_A_DELIVER);

	if (!(sp->wrk->res_mode & RES_LEN)) {
		http_Unset(sp->wrk->resp, H_Content_Length);
	} else if (params->http_range_support) {
		/* We only accept ranges if we know the length */
		http_SetHeader(sp->wrk, sp->fd, sp->wrk->resp,
		    "Accept-Ranges: bytes");
	}

	if (sp->wrk->res_mode & RES_CHUNKED)
		http_PrintfHeader(sp->wrk, sp->fd, sp->wrk->resp,
		    "Transfer-Encoding: chunked");

	TIM_format(TIM_real(), time_str);
	http_PrintfHeader(sp->wrk, sp->fd, sp->wrk->resp, "Date: %s", time_str);

	if (sp->xid != sp->obj->xid)
		http_PrintfHeader(sp->wrk, sp->fd, sp->wrk->resp,
		    "X-Varnish: %u %u", sp->xid, sp->obj->xid);
	else
		http_PrintfHeader(sp->wrk, sp->fd, sp->wrk->resp,
		    "X-Varnish: %u", sp->xid);
	http_PrintfHeader(sp->wrk, sp->fd, sp->wrk->resp, "Age: %.0f",
	    sp->obj->exp.age + sp->t_resp - sp->obj->exp.entered);
	http_SetHeader(sp->wrk, sp->fd, sp->wrk->resp, "Via: 1.1 varnish");
	http_PrintfHeader(sp->wrk, sp->fd, sp->wrk->resp, "Connection: %s",
	    sp->doclose ? "close" : "keep-alive");
}

/*--------------------------------------------------------------------
 * We have a gzip'ed object and need to ungzip it for a client which
 * does not understand gzip.
 * XXX: handle invalid gzip data better (how ?)
 */

static void
res_WriteGunzipObj(struct sess *sp)
{
	struct storage *st;
	unsigned u = 0;
	struct vgz *vg;
	char obuf[params->gzip_stack_buffer];
	ssize_t obufl = 0;
	int i;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	vg = VGZ_NewUngzip(sp, "U D -");

	VGZ_Obuf(vg, obuf, sizeof obuf);
	VTAILQ_FOREACH(st, &sp->obj->store, list) {
		CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
		CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
		u += st->len;

		VSC_C_main->n_objwrite++;

		i = VGZ_WrwGunzip(sp, vg,
		    st->ptr, st->len,
		    obuf, sizeof obuf, &obufl);
		/* XXX: error check */
		(void)i;
	}
	if (obufl) {
		(void)WRW_Write(sp->wrk, obuf, obufl);
		(void)WRW_Flush(sp->wrk);
	}
	VGZ_Destroy(&vg);
	assert(u == sp->obj->len);
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
	VTAILQ_FOREACH(st, &sp->obj->store, list) {
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
#ifdef SENDFILE_WORKS
		/*
		 * XXX: the overhead of setting up sendfile is not
		 * XXX: epsilon and maybe not even delta, so avoid
		 * XXX: engaging sendfile for small objects.
		 * XXX: Should use getpagesize() ?
		 */
		if (st->fd >= 0 &&
		    st->len >= params->sendfile_threshold) {
			VSC_C_main->n_objsendfile++;
			WRW_Sendfile(sp->wrk, st->fd, st->where + off, len);
			continue;
		}
#endif /* SENDFILE_WORKS */
		VSC_C_main->n_objwrite++;
		(void)WRW_Write(sp->wrk, st->ptr + off, len);
	}
	assert(u == sp->obj->len);
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

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	WRW_Reserve(sp->wrk, &sp->fd);

	if (sp->obj->response == 200 &&
	    sp->http->conds &&
	    RFC2616_Do_Cond(sp)) {
		sp->wantbody = 0;
		http_SetResp(sp->wrk->resp, "HTTP/1.1", 304, "Not Modified");
		http_Unset(sp->wrk->resp, H_Content_Length);
		http_Unset(sp->wrk->resp, H_Transfer_Encoding);
	}

	/*
	 * If nothing special planned, we can attempt Range support
	 */
	low = 0;
	high = sp->obj->len - 1;
	if (
	    sp->wantbody &&
	    (sp->wrk->res_mode & RES_LEN) &&
	    !(sp->wrk->res_mode & (RES_ESI|RES_ESI_CHILD|RES_GUNZIP)) &&
	    params->http_range_support &&
	    sp->obj->response == 200 &&
	    http_GetHdr(sp->http, H_Range, &r))
		res_dorange(sp, r, &low, &high);

	/*
	 * Always remove C-E if client don't grok it
	 */
	if (sp->wrk->res_mode & RES_GUNZIP)
		http_Unset(sp->wrk->resp, H_Content_Encoding);

	/*
	 * Send HTTP protocol header, unless interior ESI object
	 */
	if (!(sp->wrk->res_mode & RES_ESI_CHILD))
		sp->wrk->acct_tmp.hdrbytes +=
		    http_Write(sp->wrk, sp->wrk->resp, 1);

	if (!sp->wantbody)
		sp->wrk->res_mode &= ~RES_CHUNKED;

	if (sp->wrk->res_mode & RES_CHUNKED)
		WRW_Chunked(sp->wrk);

	if (!sp->wantbody) {
		/* This was a HEAD or conditional request */
	} else if (sp->obj->len == 0) {
		/* Nothing to do here */
	} else if (sp->wrk->res_mode & RES_ESI) {
		ESI_Deliver(sp);
	} else if (sp->wrk->res_mode & RES_ESI_CHILD && sp->wrk->gzip_resp) {
		ESI_DeliverChild(sp);
	} else if (sp->wrk->res_mode & RES_GUNZIP) {
		res_WriteGunzipObj(sp);
	} else {
		res_WriteDirObj(sp, low, high);
	}

	if (sp->wrk->res_mode & RES_CHUNKED &&
	    !(sp->wrk->res_mode & RES_ESI_CHILD))
		WRW_EndChunk(sp->wrk);

	if (WRW_FlushRelease(sp->wrk))
		vca_close_session(sp, "remote closed");
}

/*--------------------------------------------------------------------*/

void
RES_StreamStart(struct sess *sp)
{
	struct stream_ctx *sctx;

	sctx = sp->wrk->sctx;
	CHECK_OBJ_NOTNULL(sctx, STREAM_CTX_MAGIC);

	AZ(sp->wrk->res_mode & RES_ESI_CHILD);
	AN(sp->wantbody);

	WRW_Reserve(sp->wrk, &sp->fd);
	/*
	 * Always remove C-E if client don't grok it
	 */
	if (sp->wrk->res_mode & RES_GUNZIP)
		http_Unset(sp->wrk->resp, H_Content_Encoding);

	if (!(sp->wrk->res_mode & RES_CHUNKED) &&
	    sp->wrk->h_content_length != NULL)
		http_PrintfHeader(sp->wrk, sp->fd, sp->wrk->resp,
		    "Content-Length: %s", sp->wrk->h_content_length);

	sp->wrk->acct_tmp.hdrbytes +=
	    http_Write(sp->wrk, sp->wrk->resp, 1);

	if (sp->wrk->res_mode & RES_CHUNKED)
		WRW_Chunked(sp->wrk);
}

void
RES_StreamPoll(const struct sess *sp)
{
	struct stream_ctx *sctx;
	struct storage *st;
	ssize_t l, l2;
	void *ptr;

	sctx = sp->wrk->sctx;
	CHECK_OBJ_NOTNULL(sctx, STREAM_CTX_MAGIC);
	if (sp->obj->len == sctx->stream_next)
		return;
	assert(sp->obj->len > sctx->stream_next);
	l = sctx->stream_front;
	VTAILQ_FOREACH(st, &sp->obj->store, list) {
		if (st->len + l <= sctx->stream_next) {
			l += st->len;
			continue;
		}
		l2 = st->len + l - sctx->stream_next;
		ptr = st->ptr + (sctx->stream_next - l);
		if (sp->wrk->res_mode & RES_GUNZIP) {
			(void)VGZ_WrwGunzip(sp, sctx->vgz, ptr, l2,
			    sctx->obuf, sctx->obuf_len, &sctx->obuf_ptr);
		} else {
			(void)WRW_Write(sp->wrk, ptr, l2);
		}
		l += st->len;
		sctx->stream_next += l2;
	}
	if (!(sp->wrk->res_mode & RES_GUNZIP))
		(void)WRW_Flush(sp->wrk);

	if (sp->obj->objcore == NULL ||
	    (sp->obj->objcore->flags & OC_F_PASS)) {
		/*
		 * This is a pass object, release storage as soon as we
		 * have delivered it.
		 */
		while (1) {
			st = VTAILQ_FIRST(&sp->obj->store);
			if (st == NULL ||
			    sctx->stream_front + st->len > sctx->stream_next)
				break;
			VTAILQ_REMOVE(&sp->obj->store, st, list);
			sctx->stream_front += st->len;
			STV_free(st);
		}
	}
}

void
RES_StreamEnd(struct sess *sp)
{
	struct stream_ctx *sctx;

	sctx = sp->wrk->sctx;
	CHECK_OBJ_NOTNULL(sctx, STREAM_CTX_MAGIC);

	if (sp->wrk->res_mode & RES_GUNZIP && sctx->obuf_ptr > 0)
		(void)WRW_Write(sp->wrk, sctx->obuf, sctx->obuf_ptr);
	if (sp->wrk->res_mode & RES_CHUNKED &&
	    !(sp->wrk->res_mode & RES_ESI_CHILD))
		WRW_EndChunk(sp->wrk);
	if (WRW_FlushRelease(sp->wrk))
		vca_close_session(sp, "remote closed");
}
