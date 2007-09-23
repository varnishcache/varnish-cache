/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2007 Linpro AS
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
 *
 * $Id$
 */

#include <sys/types.h>
#include <sys/time.h>

#include <stdio.h>
#include <stdlib.h>

#include "shmlog.h"
#include "heritage.h"
#include "cache.h"

/*--------------------------------------------------------------------*/

void
RES_Error(struct sess *sp, int code, const char *reason)
{

	/* get a pristine object */
	HSH_Prealloc(sp);
	sp->obj = sp->wrk->nobj;
	sp->wrk->nobj = NULL;
	sp->obj->busy = 1;

	/* synthesize error page and send it */
	SYN_ErrorPage(sp, code, reason, 0);
	RES_BuildHttp(sp);
	RES_WriteObj(sp);

	/* GC the error page */
	HSH_Unbusy(sp->obj);
	HSH_Deref(sp->obj);
	sp->obj = NULL;
}

/*--------------------------------------------------------------------*/

static void
res_do_304(struct sess *sp)
{
	char lm[64];

	WSL(sp->wrk, SLT_Length, sp->fd, "%u", 0);

	http_ClrHeader(sp->http);
	sp->http->logtag = HTTP_Tx;
	http_SetResp(sp->http, "HTTP/1.1", "304", "Not Modified");
	TIM_format(sp->t_req, lm);
	http_PrintfHeader(sp->wrk, sp->fd, sp->http, "Date: %s", lm);
	http_SetHeader(sp->wrk, sp->fd, sp->http, "Via: 1.1 varnish");
	http_PrintfHeader(sp->wrk, sp->fd, sp->http, "X-Varnish: %u", sp->xid);
	TIM_format(sp->obj->last_modified, lm);
	http_PrintfHeader(sp->wrk, sp->fd, sp->http, "Last-Modified: %s", lm);
	http_PrintfHeader(sp->wrk, sp->fd, sp->http, "Connection: %s",
	    sp->doclose ? "close" : "keep-alive");
	sp->wantbody = 0;
}

/*--------------------------------------------------------------------*/

static int
res_do_conds(struct sess *sp)
{
	char *p;
	double ims;

	if (sp->obj->last_modified > 0 &&
	    http_GetHdr(sp->http, H_If_Modified_Since, &p)) {
		ims = TIM_parse(p);
		if (ims > sp->t_req)	/* [RFC2616 14.25] */
			return (0);
		if (sp->obj->last_modified > ims) {
			return (0);
		}
		res_do_304(sp);
		return (1);
	}
	return (0);
}

/*--------------------------------------------------------------------*/

void
RES_BuildHttp(struct sess *sp)
{
	char *time_str;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	if (sp->obj->response == 200 && sp->http->conds && res_do_conds(sp))
		return;

	WSL(sp->wrk, SLT_Length, sp->fd, "%u", sp->obj->len);

	http_ClrHeader(sp->http);
	sp->http->logtag = HTTP_Tx;
	http_CopyResp(sp->http, &sp->obj->http);
	http_FilterFields(sp->wrk, sp->fd, sp->http, &sp->obj->http,
	    HTTPH_A_DELIVER);
	
	/* Replace Date header with current date instead of keeping the date
	 * originally given by the backend when the object was fetched (which
	 * could be a long time ago).
	 */
	http_Unset(sp->http, H_Date);
	time_str = malloc(50);
	sprintf(time_str, "Date: ");
	TIM_format(TIM_real(), &(time_str[6]));
	http_SetHeader(sp->wrk, sp->fd, sp->http, time_str);
	
	if (sp->xid != sp->obj->xid)
		http_PrintfHeader(sp->wrk, sp->fd, sp->http,
		    "X-Varnish: %u %u", sp->xid, sp->obj->xid);
	else
		http_PrintfHeader(sp->wrk, sp->fd, sp->http,
		    "X-Varnish: %u", sp->xid);
	http_PrintfHeader(sp->wrk, sp->fd, sp->http, "Age: %.0f",
	    sp->obj->age + sp->t_resp - sp->obj->entered);
	http_SetHeader(sp->wrk, sp->fd, sp->http, "Via: 1.1 varnish");
	http_PrintfHeader(sp->wrk, sp->fd, sp->http, "Connection: %s",
	    sp->doclose ? "close" : "keep-alive");
}

/*--------------------------------------------------------------------*/

void
RES_WriteObj(struct sess *sp)
{
	struct storage *st;
	unsigned u = 0;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	WRK_Reset(sp->wrk, &sp->fd);
	sp->wrk->acct.hdrbytes += http_Write(sp->wrk, sp->http, 1);
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	if (sp->wantbody) {
		TAILQ_FOREACH(st, &sp->obj->store, list) {
			CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
			CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
			AN(st->stevedore);
			u += st->len;
			sp->wrk->acct.bodybytes += st->len;
#ifdef HAVE_SENDFILE
			/*
			 * XXX: the overhead of setting up sendfile is not
			 * XXX: epsilon and maybe not even delta, so avoid
			 * XXX: engaging sendfile for small objects.
			 * XXX: Should use getpagesize() ?
			 */
			if (st->fd >= 0 &&
			    st->len >= params->sendfile_threshold) {
				VSL_stats->n_objsendfile++;
				WRK_Sendfile(sp->wrk, st->fd,
				    st->where, st->len);
				continue;
			}
#endif /* HAVE_SENDFILE */
			VSL_stats->n_objwrite++;
			WRK_Write(sp->wrk, st->ptr, st->len);
		}
		assert(u == sp->obj->len);
	}
	if (WRK_Flush(sp->wrk))
		vca_close_session(sp, "remote closed");
}
