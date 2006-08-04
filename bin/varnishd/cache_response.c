/*
 * $Id$
 */

#include <stdio.h>		/* XXX: for NULL ?? */
#include <string.h>		/* XXX: for NULL ?? */
#include <sys/types.h>
#include <sys/time.h>

#include "libvarnish.h"
#include "shmlog.h"
#include "cache.h"


/*--------------------------------------------------------------------*/

void
RES_Error(struct sess *sp, int error, const char *msg)
{
	char buf[40];
	struct sbuf *sb;

	sb = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
	assert(sb != NULL);

	if (msg == NULL) {
		switch (error) {
		case 400:	msg = "Bad Request"; break;
		case 500:	msg = "Internal Error"; break;
		default:	msg = "HTTP request error"; break;
		}
	}

	sbuf_clear(sb);
	sbuf_printf(sb, "HTTP/1.1 %03d %s\r\n", error, msg);
	TIM_format(sp->t_req.tv_sec, buf);
	sbuf_printf(sb, "Date: %s\r\n", buf);
	sbuf_cat(sb,
		"Server: Varnish\r\n"
		"Connection: close\r\n"
		"content-Type: text/html; charset=iso-8859-1\r\n"
		"\r\n"
		"<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\r\n"
		"<HTML>\r\n"
		"  <HEAD>\r\n");
	sbuf_printf(sb, "    <TITLE>%03d %s</TITLE>\r\n", error, msg);
	sbuf_cat(sb,
		"  </HEAD>\r\n"
		"  <BODY>\r\n");
	sbuf_printf(sb, "    <H1>Error %03d %s</H1>\r\n", error, msg);
	switch(error) {
	case 400:
		sbuf_cat(sb,
		    "    Your HTTP protocol request did not make sense.\r\n");
		break;
	case 500:
	default:
		sbuf_cat(sb,
		    "    Something unexpected happened.\r\n");
		break;
	}
	sbuf_cat(sb,
		"    <P>\r\n"
		"    <I>\r\n"
		"    <A href=\"http://varnish.linpro.no/\">Varnish</A>\r\n"
		"  </BODY>\r\n"
		"</HTML>\r\n");
	sbuf_finish(sb);
	WRK_Write(sp->wrk, sbuf_data(sb), sbuf_len(sb));
	WRK_Flush(sp->wrk);
	vca_close_session(sp, msg);
	sbuf_delete(sb);
}


/*--------------------------------------------------------------------*/

static void
res_do_304(struct sess *sp, char *p)
{

	VSL(SLT_Length, sp->fd, "%u", 0);

	http_ClrHeader(sp->http);
	sp->http->logtag = HTTP_Tx;
	http_SetResp(sp->fd, sp->http, "HTTP/1.1", "304", "Not Modified");
	http_SetHeader(sp->fd, sp->http, "Via: 1.1 varnish");
	http_PrintfHeader(sp->fd, sp->http, "X-Varnish: %u", sp->xid);
	http_PrintfHeader(sp->fd, sp->http, "Last-Modified: %s", p);
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
		res_do_304(sp, p);
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
	
	/* XXX: conditional request handling */
	if (sp->wantbody) {
		TAILQ_FOREACH(st, &sp->obj->store, list) {
			CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
			CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
			assert(st->stevedore != NULL);
			u += st->len;
			sp->wrk->acct.bodybytes += st->len;
			if (st->stevedore->send == NULL) {
				WRK_Write(sp->wrk, st->ptr, st->len);
			} else {
				st->stevedore->send(st, sp);
				CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
				CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
				sp->wrk->niov = 0;
				sp->wrk->liov = 0;
			}
		}
		assert(u == sp->obj->len);
	}
	if (WRK_Flush(sp->wrk))
		vca_close_session(sp, "remote closed");
}
