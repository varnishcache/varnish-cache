/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006 Linpro AS
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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

#include <stdio.h>		/* XXX: for NULL ?? */
#include <string.h>		/* XXX: for NULL ?? */
#include <sys/types.h>
#include <sys/time.h>

#ifndef HAVE_CLOCK_GETTIME
#include "compat/clock_gettime.h"
#endif

#include "shmlog.h"
#include "heritage.h"
#include "cache.h"

/*--------------------------------------------------------------------*/
/* List of canonical HTTP response code names from RFC2616 */

static struct http_msg {
	unsigned	nbr;
	const char	*txt;
	const char	*reason;
} http_msg[] = {
	{ 101, "Switching Protocols" },
	{ 200, "OK" },
	{ 201, "Created" },
	{ 202, "Accepted" },
	{ 203, "Non-Authoritative Information" },
	{ 204, "No Content" },
	{ 205, "Reset Content" },
	{ 206, "Partial Content" },
	{ 300, "Multiple Choices" },
	{ 301, "Moved Permanently" },
	{ 302, "Found" },
	{ 303, "See Other" },
	{ 304, "Not Modified" },
	{ 305, "Use Proxy" },
	{ 306, "(Unused)" },
	{ 307, "Temporary Redirect" },
	{ 400, "Bad Request" },
	{ 401, "Unauthorized" },
	{ 402, "Payment Required" },
	{ 403, "Forbidden" },
	{ 404, "Not Found" },
	{ 405, "Method Not Allowed" },
	{ 406, "Not Acceptable" },
	{ 407, "Proxy Authentication Required" },
	{ 408, "Request Timeout" },
	{ 409, "Conflict" },
	{ 410, "Gone" },
	{ 411, "Length Required" },
	{ 412, "Precondition Failed" },
	{ 413, "Request Entity Too Large" },
	{ 414, "Request-URI Too Long" },
	{ 415, "Unsupported Media Type" },
	{ 416, "Requested Range Not Satisfiable" },
	{ 417, "Expectation Failed" },
	{ 500, "Internal Server Error" },
	{ 501, "Not Implemented" },
	{ 502, "Bad Gateway" },
	{ 503, "Service Unavailable" },
	{ 504, "Gateway Timeout" },
	{ 505, "HTTP Version Not Supported" },
	{ 0, NULL }
};

/*--------------------------------------------------------------------*/

void
RES_Error(struct sess *sp, int code, const char *reason)
{
	char buf[40];
	struct vsb *sb;
	struct http_msg *mp;
	const char *msg;

	assert(code >= 100 && code <= 999);
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	clock_gettime(CLOCK_REALTIME, &sp->t_resp);

	msg = "Unknown error";
	for (mp = http_msg; mp->nbr != 0 && mp->nbr <= code; mp++)  {
		if (mp->nbr < code)
			continue;
		if (mp->nbr > code)
			break;
		msg = mp->txt;
		if (reason == NULL)
			reason = mp->reason;
		break;
	}
	if (reason == NULL)
		reason = msg;
	AN(reason);
	AN(msg);

	sb = vsb_new(NULL, NULL, 0, VSB_AUTOEXTEND);
	XXXAN(sb);

	vsb_clear(sb);
	vsb_printf(sb, "HTTP/1.1 %03d %s\r\n", code, msg);
	TIM_format(sp->t_req.tv_sec, buf);
	vsb_printf(sb, "Date: %s\r\n", buf);
	vsb_cat(sb,
		"Server: Varnish\r\n"
		"Connection: close\r\n"
		"Content-Type: text/html; charset=iso-8859-1\r\n"
		"\r\n"
		"<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\r\n"
		"<HTML>\r\n"
		"  <HEAD>\r\n");
	vsb_printf(sb, "    <TITLE>%03d %s</TITLE>\r\n", code, msg);
	vsb_cat(sb,
		"  </HEAD>\r\n"
		"  <BODY>\r\n");
	vsb_printf(sb, "    <H1>Error %03d %s</H1>\r\n", code, msg);
	vsb_printf(sb, "    <P>%s</P>\r\n", reason);
	vsb_printf(sb, "    <H3>Guru Meditation:</H3>\r\n");
	vsb_printf(sb, "    <P>XID: %u</P>\r\n", sp->xid);
	vsb_cat(sb,
		"    <I><A href=\"http://www.varnish-cache.org/\">Varnish</A></I>\r\n"
		"  </BODY>\r\n"
		"</HTML>\r\n");
	vsb_finish(sb);
	WRK_Reset(sp->wrk, &sp->fd);
	sp->wrk->acct.hdrbytes += WRK_Write(sp->wrk, vsb_data(sb), vsb_len(sb));
	WRK_Flush(sp->wrk);
	WSL(sp->wrk, SLT_TxStatus, sp->id, "%d", code);
	WSL(sp->wrk, SLT_TxProtocol, sp->id, "HTTP/1.1");
	WSL(sp->wrk, SLT_TxResponse, sp->id, msg);
	vca_close_session(sp, reason);
	vsb_delete(sb);
}


/*--------------------------------------------------------------------*/

static void
res_do_304(struct sess *sp)
{
	char lm[64];

	WSL(sp->wrk, SLT_Length, sp->fd, "%u", 0);

	http_ClrHeader(sp->http);
	sp->http->logtag = HTTP_Tx;
	http_SetResp(sp->wrk, sp->fd, sp->http,
	    "HTTP/1.1", "304", "Not Modified");
	TIM_format(sp->t_req.tv_sec, lm);
	http_PrintfHeader(sp->wrk, sp->fd, sp->http, "Date: %s", lm);
	http_SetHeader(sp->wrk, sp->fd, sp->http, "Via: 1.1 varnish");
	http_PrintfHeader(sp->wrk, sp->fd, sp->http, "X-Varnish: %u", sp->xid);
	TIM_format(sp->obj->last_modified, lm);
	http_PrintfHeader(sp->wrk, sp->fd, sp->http, "Last-Modified: %s", lm);
	if (sp->doclose != NULL)
		http_SetHeader(sp->wrk, sp->fd, sp->http, "Connection: close");
	WRK_Reset(sp->wrk, &sp->fd);
	sp->wrk->acct.hdrbytes += http_Write(sp->wrk, sp->http, 1);
	if (WRK_Flush(sp->wrk))
		vca_close_session(sp, "remote closed");
}

/*--------------------------------------------------------------------*/

static int
res_do_conds(struct sess *sp)
{
	char *p;
	time_t ims;

	if (sp->obj->last_modified > 0 &&
	    http_GetHdr(sp->http, H_If_Modified_Since, &p)) {
		ims = TIM_parse(p);
		if (ims > sp->t_req.tv_sec)	/* [RFC2616 14.25] */
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
RES_WriteObj(struct sess *sp)
{
	struct storage *st;
	unsigned u = 0;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	clock_gettime(CLOCK_REALTIME, &sp->t_resp);

	if (sp->obj->response == 200 && sp->http->conds && res_do_conds(sp))
		return;

	WSL(sp->wrk, SLT_Length, sp->fd, "%u", sp->obj->len);

	http_ClrHeader(sp->http);
	sp->http->logtag = HTTP_Tx;
	http_CopyResp(sp->wrk, sp->fd, sp->http, &sp->obj->http);
	http_FilterHeader(sp->wrk, sp->fd, sp->http, &sp->obj->http, HTTPH_A_DELIVER);
	if (sp->xid != sp->obj->xid)
		http_PrintfHeader(sp->wrk, sp->fd, sp->http,
		    "X-Varnish: %u %u", sp->xid, sp->obj->xid);
	else
		http_PrintfHeader(sp->wrk, sp->fd, sp->http, "X-Varnish: %u", sp->xid);
	http_PrintfHeader(sp->wrk, sp->fd, sp->http, "Age: %u",
	    sp->obj->age + sp->t_resp.tv_sec - sp->obj->entered);
	http_SetHeader(sp->wrk, sp->fd, sp->http, "Via: 1.1 varnish");
	if (sp->doclose != NULL)
		http_SetHeader(sp->wrk, sp->fd, sp->http, "Connection: close");
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
