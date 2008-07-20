/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache.h"
#include "cache_backend.h"
#include "vcl.h"

/*
 * The panic string is constructed in memory, then copied to the
 * shared memory.
 *
 * It can be extracted post-mortem from a core dump using gdb:
 *
 * (gdb) printf "%s", panicstr
 */

char panicstr[65536];
static struct vsb vsps, *vsp;

#if 0

void
panic(const char *file, int line, const char *func,
    const struct sess *sp, const char *fmt, ...)
{
	va_list ap;

	vsb_printf(vsp, "panic in %s() at %s:%d\n", func, file, line);
	va_start(ap, fmt);
	vvsb_printf(vsp, fmt, ap);
	va_end(ap);

	if (VALID_OBJ(sp, SESS_MAGIC))
		dump_sess(sp);

	(void)fputs(panicstr, stderr);

	/* I wish there was a way to flush the log buffers... */
	(void)signal(SIGABRT, SIG_DFL);
#ifdef HAVE_ABORT2
	{
	void *arg[1];
	char *p;

	for (p = panicstr; *p; p++)
		if (*p == '\n')
			*p = ' ';
	arg[0] = panicstr;
	abort2(panicstr, 1, arg);
	}
#endif
	(void)raise(SIGABRT);
}

#endif

/*--------------------------------------------------------------------*/

static void
pan_backend(const struct backend *be)
{

	vsb_printf(vsp, "  backend = %p {\n", be);
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
pan_http(const struct http *h)
{
	int i;

	vsb_printf(vsp, "    http = {\n");
	if (h->nhd > HTTP_HDR_FIRST) {
		vsb_printf(vsp, "      hd = {\n");
		for (i = HTTP_HDR_FIRST; i < h->nhd; ++i)
			vsb_printf(vsp, "        \"%.*s\",\n",
			    (int)(h->hd[i].e - h->hd[i].b),
			    h->hd[i].b);
		vsb_printf(vsp, "      },\n");
	}
	vsb_printf(vsp, "    },\n");
}


/*--------------------------------------------------------------------*/

static void
pan_object(const struct object *o)
{
	const struct storage *st;

	vsb_printf(vsp, "  obj = %p {\n", o);
	vsb_printf(vsp, "    refcnt = %u, xid = %u,\n", o->refcnt, o->xid);
	pan_http(o->http);
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
pan_sess(const struct sess *sp)
{
	const char *stp;

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
	if (stp != NULL)
		vsb_printf(vsp, "  step = %s,\n", stp);
	else
		vsb_printf(vsp, "  step = 0x%x,\n", sp->step);
	if (sp->err_code)
		vsb_printf(vsp,
		    "  err_code = %d, err_reason = %s,\n", sp->err_code,
		    sp->err_reason ? sp->err_reason : "(null)");

	if (VALID_OBJ(sp->vcl, VCL_CONF_MAGIC))
		pan_vcl(sp->vcl);

	if (VALID_OBJ(sp->backend, BACKEND_MAGIC))
		pan_backend(sp->backend);

	if (VALID_OBJ(sp->obj, OBJECT_MAGIC))
		pan_object(sp->obj);

	vsb_printf(vsp, "},\n");
}

/*--------------------------------------------------------------------*/

static void
pan_ic(const char *func, const char *file, int line, const char *cond, int err, int xxx)
{
	int l;
	char *p;
	const char *q;
	const struct sess *sp;

	if (xxx) {
		vsb_printf(vsp,
		    "Missing errorhandling code in %s(), %s line %d:\n"
		    "  Condition(%s) not true.",
		    func, file, line, cond);
	} else {
		vsb_printf(vsp,
		    "Assert error in %s(), %s line %d:\n"
		    "  Condition(%s) not true.",
		    func, file, line, cond);
	}
	if (err)
		vsb_printf(vsp, "  errno = %d (%s)", err, strerror(err));

	q = THR_GetName();
	if (q != NULL)
		vsb_printf(vsp, "  thread = (%s)", q);
	sp = THR_GetSession();
	if (sp != NULL) 
		pan_sess(sp);
	vsb_printf(vsp, "\n");
	VSL_Panic(&l, &p);
	if (l < sizeof(panicstr))
		l = sizeof(panicstr);
	memcpy(p, panicstr, l);
	abort();
}

/*--------------------------------------------------------------------*/

void
PAN_Init(void)
{

	lbv_assert = pan_ic;
	vsp = &vsps;
	AN(vsb_new(vsp, panicstr, sizeof panicstr, VSB_FIXEDLEN));
}
