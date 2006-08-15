/*
 * $Id$
 */

#include <stdio.h>		/* XXX: for NULL ?? */
#include <string.h>		/* XXX: for NULL ?? */
#include <sys/types.h>
#include <sys/time.h>

#include "shmlog.h"
#include "cache.h"


/*--------------------------------------------------------------------*/

void
RES_Error(struct sess *sp, int code, const char *msg, const char *expl)
{
	char buf[40];
	struct vsb *sb;

	sb = vsb_new(NULL, NULL, 0, VSB_AUTOEXTEND);
	assert(sb != NULL);
	assert(code >= 100 && code <= 999);

	if (msg == NULL) {
		switch (code) {
		case 400:	msg = "Bad Request"; break;
		case 500:	msg = "Internal Error"; break;
		default:	msg = "HTTP request error"; break;
		}
	}
	if (expl == NULL) {
		switch (code) {
		case 400:	expl = "Your HTTP protocol request did not make sense."; break;
		case 404:	expl = "The document you requested was not found."; break;
		default:	expl = "Something unexpected happened."; break;
		}
	}

	vsb_clear(sb);
	vsb_printf(sb, "HTTP/1.1 %03d %s\r\n", code, msg);
	TIM_format(sp->t_req.tv_sec, buf);
	vsb_printf(sb, "Date: %s\r\n", buf);
	vsb_cat(sb,
		"Server: Varnish\r\n"
		"Connection: close\r\n"
		"content-Type: text/html; charset=iso-8859-1\r\n"
		"\r\n"
		"<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\r\n"
		"<HTML>\r\n"
		"  <HEAD>\r\n");
	vsb_printf(sb, "    <TITLE>%03d %s</TITLE>\r\n", code, msg);
	vsb_cat(sb,
		"  </HEAD>\r\n"
		"  <BODY>\r\n");
	vsb_printf(sb, "    <H1>Error %03d %s</H1>\r\n", code, msg);
	vsb_printf(sb, "    <P>%s</P>\r\n", expl);
	vsb_cat(sb,
		"    <I><A href=\"http://varnish.linpro.no/\">Varnish</A></I>\r\n"
		"  </BODY>\r\n"
		"</HTML>\r\n");
	vsb_finish(sb);
	WRK_Reset(sp->wrk, &sp->fd);
	WRK_Write(sp->wrk, vsb_data(sb), vsb_len(sb));
	WRK_Flush(sp->wrk);
	vca_close_session(sp, msg);
	vsb_delete(sb);
}


/*--------------------------------------------------------------------*/

static void
res_do_304(struct sess *sp)
{
	char lm[64];

	VSL(SLT_Length, sp->fd, "%u", 0);

	http_ClrHeader(sp->http);
	sp->http->logtag = HTTP_Tx;
	http_SetResp(sp->fd, sp->http, "HTTP/1.1", "304", "Not Modified");
	TIM_format(sp->t_req.tv_sec, lm);
	http_PrintfHeader(sp->fd, sp->http, "Date: %s", lm);
	http_SetHeader(sp->fd, sp->http, "Via: 1.1 varnish");
	http_PrintfHeader(sp->fd, sp->http, "X-Varnish: %u", sp->xid);
	TIM_format(sp->obj->last_modified, lm);
	http_PrintfHeader(sp->fd, sp->http, "Last-Modified: %s", lm);
	if (sp->doclose != NULL)
		http_SetHeader(sp->fd, sp->http, "Connection: close");
	WRK_Reset(sp->wrk, &sp->fd);
	http_Write(sp->wrk, sp->http, 1);
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
		if (ims > sp->obj->last_modified) {
			VSL(SLT_Debug, sp->fd,
			    "Cond: %d > %d ", sp->obj->last_modified, ims);
			return (0);
		}
		VSL(SLT_Debug, sp->fd,
		    "Cond: %d <= %d", sp->obj->last_modified, ims);
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
		
	VSL(SLT_Length, sp->fd, "%u", sp->obj->len);

	http_ClrHeader(sp->http);
	sp->http->logtag = HTTP_Tx;
	http_CopyResp(sp->fd, sp->http, &sp->obj->http);
	http_FilterHeader(sp->fd, sp->http, &sp->obj->http, HTTPH_A_DELIVER);
	if (sp->xid != sp->obj->xid)
		http_PrintfHeader(sp->fd, sp->http,
		    "X-Varnish: %u %u", sp->xid, sp->obj->xid);
	else
		http_PrintfHeader(sp->fd, sp->http, "X-Varnish: %u", sp->xid);
	http_PrintfHeader(sp->fd, sp->http, "Age: %u",
	    sp->obj->age + sp->t_req.tv_sec - sp->obj->entered);
	http_SetHeader(sp->fd, sp->http, "Via: 1.1 varnish");
	if (sp->doclose != NULL)
		http_SetHeader(sp->fd, sp->http, "Connection: close");
	WRK_Reset(sp->wrk, &sp->fd);
	sp->wrk->acct.hdrbytes += http_Write(sp->wrk, sp->http, 1);
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	
	if (sp->wantbody) {
		TAILQ_FOREACH(st, &sp->obj->store, list) {
			CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
			CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
			assert(st->stevedore != NULL);
			u += st->len;
			sp->wrk->acct.bodybytes += st->len;
#ifdef HAVE_SENDFILE
			/*
			 * XXX: the overhead of setting up senddile is not
			 * XXX: epsilon and maybe not even delta, so avoid
			 * XXX: engaging sendfile for small objects.
			 * XXX: Should use getpagesize() ?
			 */
			if (st->fd >= 0 && st->len >= 8192) {
				WRK_Sendfile(sp->wrk, st->fd,
				    st->where, st->len);
				continue;
			}
#endif /* HAVE_SENDFILE */
			WRK_Write(sp->wrk, st->ptr, st->len);
		}
		assert(u == sp->obj->len);
	}
	if (WRK_Flush(sp->wrk))
		vca_close_session(sp, "remote closed");
}
