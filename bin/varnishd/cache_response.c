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
	RES_Write(sp, sbuf_data(sb), sbuf_len(sb));
	RES_Flush(sp);
	vca_close_session(sp, msg);
}


/*--------------------------------------------------------------------
 * Write data to client
 * We try to use writev() if possible in order to minimize number of
 * syscalls made and packets sent.  It also just might allow the worker
 * thread to complete the request without holding stuff locked.
 */

void
RES_Flush(struct sess *sp)
{
	int i;

	if (sp->fd < 0 || sp->wrk->niov == 0)
		return;
	i = writev(sp->fd, sp->wrk->iov, sp->wrk->niov);
	if (i != sp->wrk->liov)
		vca_close_session(sp, "remote closed");
	sp->wrk->liov = 0;
	sp->wrk->niov = 0;
}

void
RES_Write(struct sess *sp, void *ptr, size_t len)
{

	if (sp->fd < 0 || len == 0)
		return;
	if (sp->wrk->niov == MAX_IOVS)
		RES_Flush(sp);
	if (sp->fd < 0)
		return;
	sp->wrk->iov[sp->wrk->niov].iov_base = ptr;
	sp->wrk->iov[sp->wrk->niov++].iov_len = len;
	sp->wrk->liov += len;
}

void
RES_WriteObj(struct sess *sp)
{
	struct storage *st;
	struct sbuf *sb;
	unsigned u = 0;
	uint64_t bytes = 0;
	

	sb = sp->wrk->sb;

	VSL(SLT_Status, sp->fd, "%u", sp->obj->response);
	VSL(SLT_Length, sp->fd, "%u", sp->obj->len);

	RES_Write(sp, sp->obj->header, strlen(sp->obj->header));

	sbuf_clear(sb);
	sbuf_printf(sb, "Age: %u\r\n",
		sp->obj->age + sp->t_req - sp->obj->entered);
	sbuf_printf(sb, "Via: 1.1 varnish\r\n");
	sbuf_printf(sb, "X-Varnish: xid %u\r\n", sp->obj->xid);
	if (strcmp(sp->http->proto, "HTTP/1.1")) 
		sbuf_printf(sb, "Connection: close\r\n");
	sbuf_printf(sb, "\r\n");
	sbuf_finish(sb);
	RES_Write(sp, sbuf_data(sb), sbuf_len(sb));
	bytes += sbuf_len(sb);
	/* XXX: conditional request handling */
	if (!strcmp(sp->http->req, "GET")) {
		TAILQ_FOREACH(st, &sp->obj->store, list) {
			u += st->len;
			if (st->stevedore->send == NULL) {
				RES_Write(sp, st->ptr, st->len);
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
	RES_Flush(sp);
}
