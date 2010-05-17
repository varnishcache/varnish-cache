/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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

#include "svnid.h"
SVNID("$Id$")

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef HAVE_EXECINFO_H
#include "compat/execinfo.h"
#else
#include <execinfo.h>
#endif
#include "cache.h"
#include "shmlog.h"
#include "cache_backend.h"
#include "vcl.h"
#include "libvcl.h"

/*
 * The panic string is constructed in memory, then copied to the
 * shared memory.
 *
 * It can be extracted post-mortem from a core dump using gdb:
 *
 * (gdb) printf "%s", panicstr
 */

static struct vsb vsps, *vsp;

/*--------------------------------------------------------------------*/

static void
pan_ws(const struct ws *ws, int indent)
{

	vsb_printf(vsp, "%*sws = %p { %s\n", indent, "",
	    ws, ws->overflow ? "overflow" : "");
	vsb_printf(vsp, "%*sid = \"%s\",\n", indent + 2, "", ws->id);
	vsb_printf(vsp, "%*s{s,f,r,e} = {%p", indent + 2, "", ws->s);
	if (ws->f > ws->s)
		vsb_printf(vsp, ",+%d", ws->f - ws->s);
	else
		vsb_printf(vsp, ",%p", ws->f);
	if (ws->r > ws->s)
		vsb_printf(vsp, ",+%d", ws->r - ws->s);
	else
		vsb_printf(vsp, ",%p", ws->r);
	if (ws->e > ws->s)
		vsb_printf(vsp, ",+%d", ws->e - ws->s);
	else
		vsb_printf(vsp, ",%p", ws->e);
	vsb_printf(vsp, "},\n");
	vsb_printf(vsp, "%*s},\n", indent, "" );
}

/*--------------------------------------------------------------------*/

static void
pan_vbe(const struct vbe_conn *vbe)
{

	struct backend *be;

	be = vbe->backend;

	vsb_printf(vsp, "  backend = %p fd = %d {\n", be, vbe->fd);
	vsb_printf(vsp, "    vcl_name = \"%s\",\n", be->vcl_name);
	vsb_printf(vsp, "  },\n");
}

/*--------------------------------------------------------------------*/

static void
pan_storage(const struct storage *st)
{
	int i, j;

#define MAX_BYTES (4*16)
#define show(ch) (((ch) > 31 && (ch) < 127) ? (ch) : '.')

	vsb_printf(vsp, "      %u {\n", st->len);
	for (i = 0; i < MAX_BYTES && i < st->len; i += 16) {
		vsb_printf(vsp, "        ");
		for (j = 0; j < 16; ++j) {
			if (i + j < st->len)
				vsb_printf(vsp, "%02x ", st->ptr[i + j]);
			else
				vsb_printf(vsp, "   ");
		}
		vsb_printf(vsp, "|");
		for (j = 0; j < 16; ++j)
			if (i + j < st->len)
				vsb_printf(vsp, "%c", show(st->ptr[i + j]));
		vsb_printf(vsp, "|\n");
	}
	if (st->len > MAX_BYTES)
		vsb_printf(vsp, "        [%u more]\n", st->len - MAX_BYTES);
	vsb_printf(vsp, "      },\n");

#undef show
#undef MAX_BYTES
}

/*--------------------------------------------------------------------*/

static void
pan_http(const char *id, const struct http *h, int indent)
{
	int i;

	vsb_printf(vsp, "%*shttp[%s] = {\n", indent, "", id);
	vsb_printf(vsp, "%*sws = %p[%s]\n", indent + 2, "",
	    h->ws, h->ws ? h->ws->id : "");
	for (i = 0; i < h->nhd; ++i) {
		if (h->hd[i].b == NULL && h->hd[i].e == NULL)
			continue;
		vsb_printf(vsp, "%*s\"%.*s\",\n", indent + 4, "",
		    (int)(h->hd[i].e - h->hd[i].b),
		    h->hd[i].b);
	}
	vsb_printf(vsp, "%*s},\n", indent, "");
}


/*--------------------------------------------------------------------*/

static void
pan_object(const struct object *o)
{
	const struct storage *st;

	vsb_printf(vsp, "  obj = %p {\n", o);
	vsb_printf(vsp, "    xid = %u,\n", o->xid);
	pan_ws(o->ws_o, 4);
	pan_http("obj", o->http, 4);
	vsb_printf(vsp, "    len = %u,\n", o->len);
	vsb_printf(vsp, "    store = {\n");
	VTAILQ_FOREACH(st, &o->store, list)
		pan_storage(st);
	vsb_printf(vsp, "    },\n");
	vsb_printf(vsp, "  },\n");
}

/*--------------------------------------------------------------------*/

static void
pan_vcl(const struct VCL_conf *vcl)
{
	int i;

	vsb_printf(vsp, "    vcl = {\n");
	vsb_printf(vsp, "      srcname = {\n");
	for (i = 0; i < vcl->nsrc; ++i)
		vsb_printf(vsp, "        \"%s\",\n", vcl->srcname[i]);
	vsb_printf(vsp, "      },\n");
	vsb_printf(vsp, "    },\n");
}


/*--------------------------------------------------------------------*/

static void
pan_wrk(const struct worker *wrk)
{

	vsb_printf(vsp, "  worker = %p {\n", wrk);
	pan_ws(wrk->ws, 4);
	if (wrk->bereq != NULL)
		pan_http("bereq", wrk->bereq, 4);
	if (wrk->beresp != NULL)
		pan_http("beresp", wrk->beresp, 4);
	vsb_printf(vsp, "    },\n");
}

/*--------------------------------------------------------------------*/

static void
pan_sess(const struct sess *sp)
{
	const char *stp, *hand;

	vsb_printf(vsp, "sp = %p {\n", sp);
	vsb_printf(vsp,
	    "  fd = %d, id = %d, xid = %u,\n", sp->fd, sp->id, sp->xid);
	vsb_printf(vsp, "  client = %s:%s,\n",
	    sp->addr ? sp->addr : "?.?.?.?",
	    sp->port ? sp->port : "?");
	switch (sp->step) {
/*lint -save -e525 */
#define STEP(l, u) case STP_##u: stp = "STP_" #u; break;
#include "steps.h"
#undef STEP
/*lint -restore */
		default: stp = NULL;
	}
	hand = VCC_Return_Name(sp->handling);
	if (stp != NULL)
		vsb_printf(vsp, "  step = %s,\n", stp);
	else
		vsb_printf(vsp, "  step = 0x%x,\n", sp->step);
	if (hand != NULL)
		vsb_printf(vsp, "  handling = %s,\n", hand);
	else
		vsb_printf(vsp, "  handling = 0x%x,\n", sp->handling);
	if (sp->err_code)
		vsb_printf(vsp,
		    "  err_code = %d, err_reason = %s,\n", sp->err_code,
		    sp->err_reason ? sp->err_reason : "(null)");

	vsb_printf(vsp, "  restarts = %d, esis = %d\n",
	    sp->restarts, sp->esis);

	pan_ws(sp->ws, 2);
	pan_http("req", sp->http, 2);

	if (sp->wrk != NULL)
		pan_wrk(sp->wrk);

	if (VALID_OBJ(sp->vcl, VCL_CONF_MAGIC))
		pan_vcl(sp->vcl);

	if (VALID_OBJ(sp->vbe, BACKEND_MAGIC))
		pan_vbe(sp->vbe);

	if (VALID_OBJ(sp->obj, OBJECT_MAGIC))
		pan_object(sp->obj);

	vsb_printf(vsp, "},\n");
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
	vsb_printf(vsp, "Backtrace:\n");
	for (i = 0; i < size; i++) {
		vsb_printf (vsp, "  ");
		if (Symbol_Lookup(vsp, array[i]) < 0) {
			char **strings;
			strings = backtrace_symbols(&array[i], 1);
			if (strings != NULL && strings[0] != NULL)
				vsb_printf(vsp, "%p: %s", array[i], strings[0]);
			else
				vsb_printf(vsp, "%p: (?)", array[i]);
		}
		vsb_printf (vsp, "\n");
	}
}

/*--------------------------------------------------------------------*/

static void
pan_ic(const char *func, const char *file, int line, const char *cond,
    int err, int xxx)
{
	char *p;
	const char *q;
	const struct sess *sp;

	switch(xxx) {
	case 3:
		vsb_printf(vsp,
		    "Wrong turn at %s:%d:\n%s\n", file, line, cond);
		break;
	case 2:
		vsb_printf(vsp,
		    "Panic from VCL:\n  %s\n", cond);
		break;
	case 1:
		vsb_printf(vsp,
		    "Missing errorhandling code in %s(), %s line %d:\n"
		    "  Condition(%s) not true.",
		    func, file, line, cond);
		break;
	default:
	case 0:
		vsb_printf(vsp,
		    "Assert error in %s(), %s line %d:\n"
		    "  Condition(%s) not true.\n",
		    func, file, line, cond);
		break;
	}
	if (err)
		vsb_printf(vsp, "errno = %d (%s)\n", err, strerror(err));

	q = THR_GetName();
	if (q != NULL)
		vsb_printf(vsp, "thread = (%s)\n", q);

	vsb_printf(vsp, "ident = %s,%s\n",
	    vsb_data(vident) + 1, VCA_waiter_name());

	pan_backtrace();

	if (!(params->diag_bitmap & 0x2000)) {
		sp = THR_GetSession();
		if (sp != NULL)
			pan_sess(sp);
	}
	vsb_printf(vsp, "\n");
	vsb_bcat(vsp, "", 1);	/* NUL termination */

	if (params->diag_bitmap & 0x4000)
		(void)fputs(loghead->panicstr, stderr);

#ifdef HAVE_ABORT2
	if (params->diag_bitmap & 0x8000) {
		void *arg[1];

		for (p = loghead->panicstr; *p; p++)
			if (*p == '\n')
				*p = ' ';
		arg[0] = loghead->panicstr;
		abort2(loghead->panicstr, 1, arg);
	}
#endif
	if (params->diag_bitmap & 0x1000)
		exit(4);
	else
		abort();
}

/*--------------------------------------------------------------------*/

void
PAN_Init(void)
{

	lbv_assert = pan_ic;
	vsp = &vsps;
	AN(vsb_new(vsp, loghead->panicstr, sizeof loghead->panicstr,
	    VSB_FIXEDLEN));
}
