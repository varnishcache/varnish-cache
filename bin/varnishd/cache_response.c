/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Linpro AS
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

#include "svnid.h"
SVNID("$Id$")

#include <sys/types.h>
#include <sys/time.h>

#include <stdio.h>
#include <stdlib.h>

#include "cache.h"
#include "vct.h"

/*--------------------------------------------------------------------*/

static void
res_do_304(struct sess *sp)
{
	char lm[64];
	char *p;

	http_ClrHeader(sp->wrk->resp);
	sp->wrk->resp->logtag = HTTP_Tx;
	http_SetResp(sp->wrk->resp, "HTTP/1.1", 304, "Not Modified");
	TIM_format(sp->t_req, lm);
	http_PrintfHeader(sp->wrk, sp->fd, sp->wrk->resp, "Date: %s", lm);
	http_SetHeader(sp->wrk, sp->fd, sp->wrk->resp, "Via: 1.1 varnish");
	http_PrintfHeader(sp->wrk, sp->fd, sp->wrk->resp,
	    "X-Varnish: %u", sp->xid);
	if (sp->obj->last_modified) {
		TIM_format(sp->obj->last_modified, lm);
		http_PrintfHeader(sp->wrk, sp->fd, sp->wrk->resp,
		    "Last-Modified: %s", lm);
	}

	/* http://www.w3.org/Protocols/rfc2616/rfc2616-sec10.html#sec10.3.5 */
	if (http_GetHdr(sp->obj->http, H_Cache_Control, &p))
		http_PrintfHeader(sp->wrk, sp->fd, sp->wrk->resp,
		    "Cache-Control: %s", p);
	if (http_GetHdr(sp->obj->http, H_Content_Location, &p))
		http_PrintfHeader(sp->wrk, sp->fd, sp->wrk->resp,
		    "Content-Location: %s", p);
	if (http_GetHdr(sp->obj->http, H_ETag, &p))
		http_PrintfHeader(sp->wrk, sp->fd, sp->wrk->resp,
		    "ETag: %s", p);
	if (http_GetHdr(sp->obj->http, H_Expires, &p))
		http_PrintfHeader(sp->wrk, sp->fd, sp->wrk->resp,
		    "Expires: %s", p);
	if (http_GetHdr(sp->obj->http, H_Vary, &p))
		http_PrintfHeader(sp->wrk, sp->fd, sp->wrk->resp,
		    "Vary: %s", p);

	http_PrintfHeader(sp->wrk, sp->fd, sp->wrk->resp, "Connection: %s",
	    sp->doclose ? "close" : "keep-alive");
	sp->wantbody = 0;
}

/*--------------------------------------------------------------------*/

static int
res_do_conds(struct sess *sp)
{
	char *p, *e;
	double ims;
	int do_cond = 0;

	/* RFC 2616 13.3.4 states we need to match both ETag
	   and If-Modified-Since if present*/

	if (http_GetHdr(sp->http, H_If_Modified_Since, &p) ) {
		if (!sp->obj->last_modified)
			return (0);
		ims = TIM_parse(p);
		if (ims > sp->t_req)	/* [RFC2616 14.25] */
			return (0);
		if (sp->obj->last_modified > ims)
			return (0);
		do_cond = 1;
	}

	if (http_GetHdr(sp->http, H_If_None_Match, &p) &&
	    http_GetHdr(sp->obj->http, H_ETag, &e)) {
		if (strcmp(p,e) != 0)
			return (0);
		do_cond = 1;
	}

	if (do_cond == 1) {
		res_do_304(sp);
		return (1);
	}
	return (0);
}

/*--------------------------------------------------------------------*/

static void
res_dorange(struct sess *sp, const char *r, unsigned *plow, unsigned *phigh)
{
	unsigned low, high, has_low;

	(void)sp;
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
	    "Content-Range: bytes %u-%u/%u", low, high, sp->obj->len);
	http_Unset(sp->wrk->resp, H_Content_Length);
	http_PrintfHeader(sp->wrk, sp->fd, sp->wrk->resp,
	    "Content-Length: %u", 1 + high - low);
	http_SetResp(sp->wrk->resp, "HTTP/1.1", 206, "Partial Content");


	*plow = low;
	*phigh = high;
}

/*--------------------------------------------------------------------*/

void
RES_BuildHttp(struct sess *sp)
{
	char time_str[30];

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	if (sp->obj->response == 200 && sp->http->conds && res_do_conds(sp))
		return;

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
	    sp->obj->age + sp->t_resp - sp->obj->entered);
	http_SetHeader(sp->wrk, sp->fd, sp->wrk->resp, "Via: 1.1 varnish");
	http_PrintfHeader(sp->wrk, sp->fd, sp->wrk->resp, "Connection: %s",
	    sp->doclose ? "close" : "keep-alive");
}

/*--------------------------------------------------------------------*/

void
RES_WriteObj(struct sess *sp)
{
	struct storage *st;
	unsigned u = 0;
	char lenbuf[20];
	char *r;
	unsigned low, high, ptr, off, len;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	WRW_Reserve(sp->wrk, &sp->fd);

	/*
	 * ESI objects get special delivery
	 */
	if (sp->wrk->res_mode & RES_ESI) {
		if (!(sp->wrk->res_mode & RES_ESI_CHILD))
			/* no headers for interior ESI includes */
			sp->acct_tmp.hdrbytes +=
			    http_Write(sp->wrk, sp->wrk->resp, 1);

		if (WRW_FlushRelease(sp->wrk)) {
			vca_close_session(sp, "remote closed");
		} else if (sp->wantbody)
			ESI_Deliver(sp);
		return;
	}

	if (sp->wrk->res_mode & RES_GUNZIP) {
		RES_WriteGunzipObj(sp);
		return;
	}

	/*
	 * How much of the object we want to deliver
	 */
	low = 0;
	high = sp->obj->len - 1;

	if (!(sp->wrk->res_mode & (RES_ESI|RES_ESI_CHILD))) {
		/* For non-ESI and non ESI-included objects, try Range */
		if (params->http_range_support &&
		    (sp->disable_esi || sp->esis == 0) &&
		    sp->obj->response == 200 &&
		    sp->wantbody &&
		    http_GetHdr(sp->http, H_Range, &r))
			res_dorange(sp, r, &low, &high);

		sp->acct_tmp.hdrbytes += http_Write(sp->wrk, sp->wrk->resp, 1);
	} else if (sp->obj->len > 0 && (sp->wrk->res_mode & RES_CHUNKED)) {
		assert(sp->wantbody);
		sprintf(lenbuf, "%x\r\n", sp->obj->len);
		(void)WRW_Write(sp->wrk, lenbuf, -1);
	}

	if (!sp->wantbody) {
		/* This was a HEAD request */
		assert(sp->esis == 0);
		if (WRW_FlushRelease(sp->wrk))
			vca_close_session(sp, "remote closed");
		return;
	}

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

		sp->acct_tmp.bodybytes += len;
#ifdef SENDFILE_WORKS
		/*
		 * XXX: the overhead of setting up sendfile is not
		 * XXX: epsilon and maybe not even delta, so avoid
		 * XXX: engaging sendfile for small objects.
		 * XXX: Should use getpagesize() ?
		 */
		if (st->fd >= 0 &&
		    st->len >= params->sendfile_threshold) {
			VSC_main->n_objsendfile++;
			WRW_Sendfile(sp->wrk, st->fd, st->where + off, len);
			continue;
		}
#endif /* SENDFILE_WORKS */
		VSC_main->n_objwrite++;
		(void)WRW_Write(sp->wrk, st->ptr + off, len);
	}
	assert(u == sp->obj->len);
	if (!sp->disable_esi &&
	    sp->esis > 0 &&
	    sp->http->protover >= 1.1 &&
	    sp->obj->len > 0) {
		/* post-chunk new line */
		(void)WRW_Write(sp->wrk, "\r\n", -1);
	}

	if (WRW_FlushRelease(sp->wrk))
		vca_close_session(sp, "remote closed");
}

/*--------------------------------------------------------------------
 * We have a gzip'ed object and need to ungzip it for a client which
 * does not understand gzip.
 */

void
RES_WriteGunzipObj(struct sess *sp)
{
	struct storage *st;
	unsigned u = 0;
#if 0
	char lenbuf[20];
#endif
	struct vgz *vg;
	const void *dp;
	size_t dl;
	int i;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	AN(sp->wantbody);

	/* We don't know the length  (XXX: Cache once we do ?) */
	http_Unset(sp->wrk->resp, H_Content_Length);
	http_Unset(sp->wrk->resp, H_Content_Encoding);
	http_Unset(sp->wrk->resp, H_Connection);

	http_PrintfHeader(sp->wrk, sp->fd, sp->wrk->resp, "Connection: %s",
	    "close");
	sp->doclose = "Gunzip EOF";

	/*
	 * ESI objects get special delivery
	 */
	if (!sp->disable_esi && sp->obj->esidata != NULL) {
		INCOMPL();
#if 0
		if (sp->esis == 0)
			/* no headers for interior ESI includes */
			sp->acct_tmp.hdrbytes +=
			    http_Write(sp->wrk, sp->wrk->resp, 1);

		if (WRW_FlushRelease(sp->wrk)) {
			vca_close_session(sp, "remote closed");
		} else 
			ESI_Deliver(sp);
		return;
#endif
	}

	if (sp->disable_esi || sp->esis == 0) {
		sp->acct_tmp.hdrbytes += http_Write(sp->wrk, sp->wrk->resp, 1);
	} else if (!sp->disable_esi &&
	    sp->esis > 0 &&
	    sp->http->protover >= 1.1 &&
	    sp->obj->len > 0) {
		INCOMPL();
#if 0
		/*
		 * Interior ESI includes (which are not themselves ESI
		 * objects) use chunked encoding (here) or EOF (nothing)
		 */
		sprintf(lenbuf, "%x\r\n", sp->obj->len);
		(void)WRW_Write(sp->wrk, lenbuf, -1);
#endif
	}

	vg = VGZ_NewUnzip(sp, sp->ws, sp->wrk->ws);
	AN(vg);

	VTAILQ_FOREACH(st, &sp->obj->store, list) {
		CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
		CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
		u += st->len;

		sp->acct_tmp.bodybytes += st->len;
		VSC_main->n_objwrite++;

		VGZ_Feed(vg, st->ptr, st->len);
		do {
			i = VGZ_Produce(vg, &dp, &dl);
			if (dl != 0) {
				(void)WRW_Write(sp->wrk, dp, dl);
				if (WRW_Flush(sp->wrk))
					break;
			}
		} while (i == 0);
	}
	VGZ_Destroy(&vg);
	assert(u == sp->obj->len);
	if (!sp->disable_esi &&
	    sp->esis > 0 &&
	    sp->http->protover >= 1.1 &&
	    sp->obj->len > 0) {
		INCOMPL();
#if 0
		/* post-chunk new line */
		(void)WRW_Write(sp->wrk, "\r\n", -1);
#endif
	}

	if (WRW_FlushRelease(sp->wrk))
		vca_close_session(sp, "remote closed");
}
