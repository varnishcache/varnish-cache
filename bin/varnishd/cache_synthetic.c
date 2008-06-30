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
#include "heritage.h"
#include "cache.h"

/*
 * Synthesize an error page.  This assumes the session already has an
 * object - if it doesn't, you need to either call HSH_Lookup(), or call
 * HSH_Prealloc() and grab sp->obj->nobj, before calling this.
 */
void
SYN_ErrorPage(struct sess *sp, int status, const char *reason, int ttl)
{
	struct storage *st;
	struct object *o;
	struct worker *w;
	struct http *h;
	struct vsb vsb;
	const char *msg;
	char date[40];
	double now;
	int fd;

	WSL_Flush(sp->wrk);
	assert(status >= 100 && status <= 999);
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);
	CHECK_OBJ_NOTNULL(&sp->obj->http, HTTP_MAGIC);
	assert(sp->obj->busy > 0);

	/* shortcuts */
	w = sp->wrk;
	fd = sp->fd;
	o = sp->obj;
	h = &o->http;
	now = TIM_real();

	/* look up HTTP response */
	msg = http_StatusMessage(status);
	if (reason == NULL)
		reason = msg;
	AN(reason);
	AN(msg);

	/* populate metadata */
	o->response = status;
	o->valid = 1;
	o->entered = now;
	o->ttl = now + ttl;
	o->last_modified = now;
	o->xid = sp->xid;

	/* allocate space for body */
	/* XXX what if the object already has a body? */
	st = STV_alloc(1024);
	XXXAN(st->stevedore);
	VTAILQ_INSERT_TAIL(&sp->obj->store, st, list);

	/* generate body */
	vsb_new(&vsb, (char *)st->ptr, st->space, VSB_FIXEDLEN);
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
	o->len = st->len = vsb_len(&vsb);
	vsb_delete(&vsb);

	/* allocate space for header */

	WS_Init(h->ws, malloc(1024), 1024);

	/* generate header */
	http_ClrHeader(h);
	http_PutProtocol(w, fd, h, "HTTP/1.0"); /* XXX */
	http_PutStatus(w, fd, h, status);
	http_PutResponse(w, fd, h, msg);
	TIM_format(now, date);
	http_PrintfHeader(w, fd, h, "Date: %s", date);
	http_PrintfHeader(w, fd, h, "Server: Varnish");
	http_PrintfHeader(w, fd, h, "Retry-After: %ju", (uintmax_t)ttl);
	http_PrintfHeader(w, fd, h, "Content-Type: text/html; charset=utf-8");
	http_PrintfHeader(w, fd, h, "Content-Length: %u", o->len);
	/* DO NOT generate X-Varnish header, RES_BuildHttp will */
}
