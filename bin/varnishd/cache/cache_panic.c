/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 */

#include "config.h"

#ifndef HAVE_EXECINFO_H
#include "compat/execinfo.h"
#else
#include <execinfo.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include "cache.h"
#include "common/heritage.h"

#include "cache_backend.h"
#include "waiter/cache_waiter.h"
#include "vcl.h"

/*
 * The panic string is constructed in memory, then copied to the
 * shared memory.
 *
 * It can be extracted post-mortem from a core dump using gdb:
 *
 * (gdb) printf "%s", panicstr
 */

static struct vsb pan_vsp_storage, *pan_vsp;
static pthread_mutex_t panicstr_mtx = PTHREAD_MUTEX_INITIALIZER;

/*--------------------------------------------------------------------*/

static void
pan_ws(const struct ws *ws, int indent)
{

	VSB_printf(pan_vsp, "%*sws = %p { %s\n", indent, "",
	    ws, ws->overflow ? "overflow" : "");
	VSB_printf(pan_vsp, "%*sid = \"%s\",\n", indent + 2, "", ws->id);
	VSB_printf(pan_vsp, "%*s{s,f,r,e} = {%p", indent + 2, "", ws->s);
	if (ws->f > ws->s)
		VSB_printf(pan_vsp, ",+%ld", (long) (ws->f - ws->s));
	else
		VSB_printf(pan_vsp, ",%p", ws->f);
	if (ws->r > ws->s)
		VSB_printf(pan_vsp, ",+%ld", (long) (ws->r - ws->s));
	else
		VSB_printf(pan_vsp, ",%p", ws->r);
	if (ws->e > ws->s)
		VSB_printf(pan_vsp, ",+%ld", (long) (ws->e - ws->s));
	else
		VSB_printf(pan_vsp, ",%p", ws->e);
	VSB_printf(pan_vsp, "},\n");
	VSB_printf(pan_vsp, "%*s},\n", indent, "" );
}

/*--------------------------------------------------------------------*/

static void
pan_vbc(const struct vbc *vbc)
{

	struct backend *be;

	be = vbc->backend;

	VSB_printf(pan_vsp, "  backend = %p fd = %d {\n", be, vbc->fd);
	VSB_printf(pan_vsp, "    display_name = \"%s\",\n", be->display_name);
	VSB_printf(pan_vsp, "  },\n");
}

/*--------------------------------------------------------------------*/

static void
pan_storage(const struct storage *st)
{
	int i, j;

#define MAX_BYTES (4*16)
#define show(ch) (((ch) > 31 && (ch) < 127) ? (ch) : '.')

	VSB_printf(pan_vsp, "      %u {\n", st->len);
	for (i = 0; i < MAX_BYTES && i < st->len; i += 16) {
		VSB_printf(pan_vsp, "        ");
		for (j = 0; j < 16; ++j) {
			if (i + j < st->len)
				VSB_printf(pan_vsp, "%02x ", st->ptr[i + j]);
			else
				VSB_printf(pan_vsp, "   ");
		}
		VSB_printf(pan_vsp, "|");
		for (j = 0; j < 16; ++j)
			if (i + j < st->len)
				VSB_printf(pan_vsp,
				    "%c", show(st->ptr[i + j]));
		VSB_printf(pan_vsp, "|\n");
	}
	if (st->len > MAX_BYTES)
		VSB_printf(pan_vsp,
		    "        [%u more]\n", st->len - MAX_BYTES);
	VSB_printf(pan_vsp, "      },\n");

#undef show
#undef MAX_BYTES
}

/*--------------------------------------------------------------------*/

static void
pan_http(const char *id, const struct http *h, int indent)
{
	int i;

	VSB_printf(pan_vsp, "%*shttp[%s] = {\n", indent, "", id);
	VSB_printf(pan_vsp, "%*sws = %p[%s]\n", indent + 2, "",
	    h->ws, h->ws ? h->ws->id : "");
	for (i = 0; i < h->nhd; ++i) {
		if (h->hd[i].b == NULL && h->hd[i].e == NULL)
			continue;
		VSB_printf(pan_vsp, "%*s\"%.*s\",\n", indent + 4, "",
		    (int)(h->hd[i].e - h->hd[i].b),
		    h->hd[i].b);
	}
	VSB_printf(pan_vsp, "%*s},\n", indent, "");
}


/*--------------------------------------------------------------------*/

static void
pan_object(const struct object *o)
{
	const struct storage *st;

	VSB_printf(pan_vsp, "  obj = %p {\n", o);
	VSB_printf(pan_vsp, "    xid = %u,\n", o->xid);
	pan_ws(o->ws_o, 4);
	pan_http("obj", o->http, 4);
	VSB_printf(pan_vsp, "    len = %jd,\n", (intmax_t)o->len);
	VSB_printf(pan_vsp, "    store = {\n");
	VTAILQ_FOREACH(st, &o->store, list)
		pan_storage(st);
	VSB_printf(pan_vsp, "    },\n");
	VSB_printf(pan_vsp, "  },\n");
}

/*--------------------------------------------------------------------*/

static void
pan_vcl(const struct VCL_conf *vcl)
{
	int i;

	VSB_printf(pan_vsp, "    vcl = {\n");
	VSB_printf(pan_vsp, "      srcname = {\n");
	for (i = 0; i < vcl->nsrc; ++i)
		VSB_printf(pan_vsp, "        \"%s\",\n", vcl->srcname[i]);
	VSB_printf(pan_vsp, "      },\n");
	VSB_printf(pan_vsp, "    },\n");
}


/*--------------------------------------------------------------------*/

static void
pan_wrk(const struct worker *wrk)
{

	VSB_printf(pan_vsp, "  worker = %p {\n", wrk);
	pan_ws(wrk->ws, 4);
	if (wrk->bereq->ws != NULL)
		pan_http("bereq", wrk->bereq, 4);
	if (wrk->beresp->ws != NULL)
		pan_http("beresp", wrk->beresp, 4);
	if (wrk->resp->ws != NULL)
		pan_http("resp", wrk->resp, 4);
	VSB_printf(pan_vsp, "    },\n");
}

/*--------------------------------------------------------------------*/

static void
pan_sess(const struct sess *sp)
{
	const char *stp, *hand;

	VSB_printf(pan_vsp, "sp = %p {\n", sp);
	VSB_printf(pan_vsp,
	    "  fd = %d, id = %d, xid = %u,\n",
	    sp->fd, sp->vsl_id & VSL_IDENTMASK, sp->xid);
	VSB_printf(pan_vsp, "  client = %s %s,\n",
	    sp->addr ? sp->addr : "?.?.?.?",
	    sp->port ? sp->port : "?");
	switch (sp->step) {
#define STEP(l, u) case STP_##u: stp = "STP_" #u; break;
#include "tbl/steps.h"
#undef STEP
		default: stp = NULL;
	}
	hand = VCL_Return_Name(sp->handling);
	if (stp != NULL)
		VSB_printf(pan_vsp, "  step = %s,\n", stp);
	else
		VSB_printf(pan_vsp, "  step = 0x%x,\n", sp->step);
	if (hand != NULL)
		VSB_printf(pan_vsp, "  handling = %s,\n", hand);
	else
		VSB_printf(pan_vsp, "  handling = 0x%x,\n", sp->handling);
	if (sp->err_code)
		VSB_printf(pan_vsp,
		    "  err_code = %d, err_reason = %s,\n", sp->err_code,
		    sp->err_reason ? sp->err_reason : "(null)");

	VSB_printf(pan_vsp, "  restarts = %d, esi_level = %d\n",
	    sp->restarts, sp->esi_level);

	VSB_printf(pan_vsp, "  flags = ");
	if (sp->wrk->do_stream)	VSB_printf(pan_vsp, " do_stream");
	if (sp->wrk->do_gzip)	VSB_printf(pan_vsp, " do_gzip");
	if (sp->wrk->do_gunzip)	VSB_printf(pan_vsp, " do_gunzip");
	if (sp->wrk->do_esi)	VSB_printf(pan_vsp, " do_esi");
	if (sp->wrk->do_close)	VSB_printf(pan_vsp, " do_close");
	if (sp->wrk->is_gzip)	VSB_printf(pan_vsp, " is_gzip");
	if (sp->wrk->is_gunzip)	VSB_printf(pan_vsp, " is_gunzip");
	VSB_printf(pan_vsp, "\n");
	VSB_printf(pan_vsp, "  bodystatus = %d\n", sp->wrk->body_status);

	pan_ws(sp->ws, 2);
	pan_http("req", sp->http, 2);

	if (sp->wrk != NULL)
		pan_wrk(sp->wrk);

	if (VALID_OBJ(sp->vcl, VCL_CONF_MAGIC))
		pan_vcl(sp->vcl);

	if (VALID_OBJ(sp->wrk->vbc, BACKEND_MAGIC))
		pan_vbc(sp->wrk->vbc);

	if (VALID_OBJ(sp->wrk->obj, OBJECT_MAGIC))
		pan_object(sp->wrk->obj);

	VSB_printf(pan_vsp, "},\n");
}

/*--------------------------------------------------------------------*/

static void
pan_backtrace(void)
{
	void *array[10];
	size_t size;
	size_t i;

	size = backtrace (array, 10);
	if (size == 0)
		return;
	VSB_printf(pan_vsp, "Backtrace:\n");
	for (i = 0; i < size; i++) {
		VSB_printf (pan_vsp, "  ");
		if (Symbol_Lookup(pan_vsp, array[i]) < 0) {
			char **strings;
			strings = backtrace_symbols(&array[i], 1);
			if (strings != NULL && strings[0] != NULL)
				VSB_printf(pan_vsp,
				     "%p: %s", array[i], strings[0]);
			else
				VSB_printf(pan_vsp, "%p: (?)", array[i]);
		}
		VSB_printf (pan_vsp, "\n");
	}
}

/*--------------------------------------------------------------------*/

static void
pan_ic(const char *func, const char *file, int line, const char *cond,
    int err, int xxx)
{
	const char *q;
	const struct sess *sp;

	AZ(pthread_mutex_lock(&panicstr_mtx)); /* Won't be released,
						  we're going to die
						  anyway */
	switch(xxx) {
	case 3:
		VSB_printf(pan_vsp,
		    "Wrong turn at %s:%d:\n%s\n", file, line, cond);
		break;
	case 2:
		VSB_printf(pan_vsp,
		    "Panic from VCL:\n  %s\n", cond);
		break;
	case 1:
		VSB_printf(pan_vsp,
		    "Missing errorhandling code in %s(), %s line %d:\n"
		    "  Condition(%s) not true.",
		    func, file, line, cond);
		break;
	default:
	case 0:
		VSB_printf(pan_vsp,
		    "Assert error in %s(), %s line %d:\n"
		    "  Condition(%s) not true.\n",
		    func, file, line, cond);
		break;
	}
	if (err)
		VSB_printf(pan_vsp, "errno = %d (%s)\n", err, strerror(err));

	q = THR_GetName();
	if (q != NULL)
		VSB_printf(pan_vsp, "thread = (%s)\n", q);

	VSB_printf(pan_vsp, "ident = %s,%s\n",
	    VSB_data(vident) + 1, WAIT_GetName());

	pan_backtrace();

	if (!(cache_param->diag_bitmap & 0x2000)) {
		sp = THR_GetSession();
		if (sp != NULL)
			pan_sess(sp);
	}
	VSB_printf(pan_vsp, "\n");
	VSB_bcat(pan_vsp, "", 1);	/* NUL termination */

	if (cache_param->diag_bitmap & 0x4000)
		(void)fputs(heritage.panic_str, stderr);

	if (cache_param->diag_bitmap & 0x1000)
		exit(4);
	else
		abort();
}

/*--------------------------------------------------------------------*/

void
PAN_Init(void)
{

	VAS_Fail = pan_ic;
	pan_vsp = &pan_vsp_storage;
	AN(heritage.panic_str);
	AN(heritage.panic_str_len);
	AN(VSB_new(pan_vsp, heritage.panic_str, heritage.panic_str_len,
	    VSB_FIXEDLEN));
}
