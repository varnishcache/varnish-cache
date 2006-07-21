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

	sb = sp->wrk->sb;

	if (msg == NULL) {
		switch (error) {
		case 400:	msg = "Bad Request"; break;
		case 500:	msg = "Internal Error"; break;
		default:	msg = "HTTP request error"; break;
		}
	}

	sbuf_clear(sb);
	sbuf_printf(sb, "HTTP/1.1 %03d %s\r\n", error, msg);
	TIM_format(sp->t_req, buf);
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
}


/*--------------------------------------------------------------------*/

static void
res_do_304(struct sess *sp, char *p)
{
	struct sbuf *sb;

	sb = sp->wrk->sb;
	sbuf_clear(sb);

	VSL(SLT_Status, sp->fd, "%u", 304);
	VSL(SLT_Length, sp->fd, "%u", 0);
	WRK_Write(sp->wrk, "HTTP/1.1 304 Not Modified\r\n", -1);
	WRK_Write(sp->wrk, "Via: 1.1 varnish\r\n", -1);
	WRK_Write(sp->wrk, "Last-Modified: ", -1);
	WRK_Write(sp->wrk, p, -1);
	WRK_Write(sp->wrk, "\r\n", -1);
	if (strcmp(sp->http->hd[HTTP_HDR_PROTO].b, "HTTP/1.1")) 
		WRK_Write(sp->wrk, "Connection: close\r\n", -1);
	sbuf_printf(sb, "X-Varnish: xid %u\r\n", sp->obj->xid);
	sbuf_printf(sb, "\r\n");
	sbuf_finish(sb);
	WRK_Write(sp->wrk, sbuf_data(sb), sbuf_len(sb));
	WRK_Flush(sp->wrk);
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
		if (ims > sp->t_req)	/* [RFC2616 14.25] */
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
	struct sbuf *sb;
	unsigned u = 0;
	uint64_t bytes = 0;
	

	sb = sp->wrk->sb;

	if (sp->obj->response == 200 && sp->http->conds && res_do_conds(sp))
		return;
		
	VSL(SLT_Status, sp->fd, "%u", sp->obj->response);
	VSL(SLT_Length, sp->fd, "%u", sp->obj->len);

	WRK_Write(sp->wrk, sp->obj->header, strlen(sp->obj->header));

	sbuf_clear(sb);
	sbuf_printf(sb, "Age: %u\r\n",
		sp->obj->age + sp->t_req - sp->obj->entered);
	sbuf_printf(sb, "Via: 1.1 varnish\r\n");
	sbuf_printf(sb, "X-Varnish: xid %u\r\n", sp->obj->xid);
	if (strcmp(sp->http->hd[HTTP_HDR_PROTO].b, "HTTP/1.1")) 
		sbuf_printf(sb, "Connection: close\r\n");
	sbuf_printf(sb, "\r\n");
	sbuf_finish(sb);
	WRK_Write(sp->wrk, sbuf_data(sb), sbuf_len(sb));
	bytes += sbuf_len(sb);
	/* XXX: conditional request handling */
	if (!strcmp(sp->http->hd[HTTP_HDR_REQ].b, "GET")) {
		TAILQ_FOREACH(st, &sp->obj->store, list) {
			assert(st->stevedore != NULL);
			u += st->len;
			if (st->stevedore->send == NULL) {
				WRK_Write(sp->wrk, st->ptr, st->len);
				continue;
			}
			st->stevedore->send(st, sp,
			    sp->wrk->iov, sp->wrk->niov, sp->wrk->liov);
			sp->wrk->niov = 0;
			sp->wrk->liov = 0;
		}
		assert(u == sp->obj->len);
	}
	SES_ChargeBytes(sp, bytes + u);
	WRK_Flush(sp->wrk);
}
