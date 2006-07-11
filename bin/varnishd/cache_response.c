/*
 * $Id$
 */

#include <stdio.h>		/* XXX: for NULL ?? */
#include <sys/types.h>
#include <sys/time.h>
#include <event.h>

#include "libvarnish.h"
#include "sbuf.h"
#include "cache.h"


/*--------------------------------------------------------------------*/

void
RES_Error(struct worker *w, struct sess *sp, int error, const char *msg)
{
	char buf[40];

	if (msg == NULL) {
		switch (error) {
		case 400:	msg = "Bad Request"; break;
		case 500:	msg = "Internal Error"; break;
		default:	msg = "HTTP request error"; break;
		}
	}

	sbuf_clear(w->sb);
	sbuf_printf(w->sb, "HTTP/1.1 %03d %s\r\n", error, msg);
	TIM_format(sp->t_req, buf);
	sbuf_printf(w->sb, "Date: %s\r\n", buf);
	sbuf_cat(w->sb,
		"Server: Varnish\r\n"
		"Connection: close\r\n"
		"content-Type: text/html; charset=iso-8859-1\r\n"
		"\r\n"
		"<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\r\n"
		"<HTML>\r\n"
		"  <HEAD>\r\n");
	sbuf_printf(w->sb, "    <TITLE>%03d %s</TITLE>\r\n", error, msg);
	sbuf_cat(w->sb,
		"  </HEAD>\r\n"
		"  <BODY>\r\n");
	sbuf_printf(w->sb, "    <H1>Error %03d %s</H1>\r\n", error, msg);
	switch(error) {
	case 400:
		sbuf_cat(w->sb,
		    "    Your HTTP protocol request did not make sense.\r\n");
		break;
	case 500:
	default:
		sbuf_cat(w->sb,
		    "    Something unexpected happened.\r\n");
		break;
	}
	sbuf_cat(w->sb,
		"    <P>\r\n"
		"    <I>\r\n"
		"    <A href=\"http://varnish.linpro.no/\">Varnish</A>\r\n"
		"  </BODY>\r\n"
		"</HTML>\r\n");
	sbuf_finish(w->sb);
	vca_write(sp, sbuf_data(w->sb), sbuf_len(w->sb));
	vca_flush(sp);
	vca_close_session(sp, msg);
}
