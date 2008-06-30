/*-
 * Copyright (c) 2007-2008 Linpro AS
 * All rights reserved.
 *
 * Author: Dag-Erling Smørgrav <des@des.no>
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

#include "config.h"

#include <sys/types.h>
#include <sys/time.h>

#include <stdlib.h>

#include "shmlog.h"
#include "cache.h"
#include "stevedore.h"

/*
 * Synthesize an error page including headers.
 * XXX: For now close the connection.  Long term that should probably
 * XXX: be either a paramter or VCL decision.
 * XXX: VCL should get a shot at generating the page.
 */

void
SYN_ErrorPage(struct sess *sp, int status, const char *reason)
{
	struct http *h;
	struct worker *w;
	const char *msg;
	char date[40];
	double now;
	unsigned u;
	struct vsb vsb;
	int fd;
	int ttl = 0;				/* XXX: ?? */

	WSL_Flush(sp->wrk, 0);
	assert(status >= 100 && status <= 999);
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);

	/* shortcuts */
	w = sp->wrk;
	h = sp->http;
	fd = sp->fd;
	now = TIM_real();		/* XXX: use cached val ? */

	WRK_Reset(w, &sp->fd);

	/* look up HTTP response */
	msg = http_StatusMessage(status);
	AN(msg);
	if (reason == NULL)
		reason = msg;

	/* generate header */
	http_ClrHeader(h);
	h->logtag = HTTP_Tx;
	http_PutProtocol(w, fd, h, "HTTP/1.0"); /* XXX */
	http_PutStatus(w, fd, h, status);
	http_PutResponse(w, fd, h, msg);
	TIM_format(now, date);
	http_PrintfHeader(w, fd, h, "Date: %s", date);
	http_PrintfHeader(w, fd, h, "Server: Varnish");
	http_PrintfHeader(w, fd, h, "Retry-After: %d", ttl);
	http_PrintfHeader(w, fd, h, "Content-Type: text/html; charset=utf-8");
	http_PrintfHeader(w, sp->fd, sp->http, "X-Varnish: %u", sp->xid);
	http_PrintfHeader(w, fd, h, "Connection: close");

	w->acct.hdrbytes += http_Write(w, h, 1);

	/* generate body */
	/* XXX: VCL should do this */
	u = WS_Reserve(h->ws, 0);
	AN(vsb_new(&vsb, h->ws->f, u, VSB_FIXEDLEN));
	vsb_printf(&vsb,
	    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
	    "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\"\n"
	    " \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n"
	    "<html>\n"
	    "  <head>\n"
	    "    <title>%03d %s</title>\n", status, msg);
	vsb_printf(&vsb,
	    "  </head>\n"
	    "  <body>\n"
	    "    <h1>Error %03d %s</h1>\n", status, msg);
	vsb_printf(&vsb,
	    "    <p>%s</p>\n", reason);
	vsb_printf(&vsb,
	    "    <h3>Guru Meditation:</h3>\n"
	    "    <p>XID: %u</p>\n", sp->xid);
	vsb_printf(&vsb,
	    "    <address><a href=\"http://www.varnish-cache.org/\">Varnish</a></address>\n"
	    "  </body>\n"
	    "</html>\n");
	vsb_finish(&vsb);
	AZ(vsb_overflowed(&vsb));
	w->acct.hdrbytes = WRK_Write(w, vsb_data(&vsb), vsb_len(&vsb));
	(void)WRK_Flush(w);
	vsb_delete(&vsb);
	vca_close_session(sp, "error returned");
}
