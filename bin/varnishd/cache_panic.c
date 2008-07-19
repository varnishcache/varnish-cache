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
#include "vcl.h"

#ifndef WITHOUT_ASSERTS

/*
 * The panic string is constructed in memory, then printed to stderr.  It
 * can be extracted post-mortem from a core dump using gdb:
 *
 * (gdb) printf "%s", panicstr
 */
char panicstr[65536];
static char *pstr = panicstr;

#define fp(...)							\
	do {							\
		pstr += snprintf(pstr,				\
		    (panicstr + sizeof panicstr) - pstr,	\
		    __VA_ARGS__);				\
	} while (0)

#define vfp(fmt, ap)						\
	do {							\
		pstr += vsnprintf(pstr,				\
		    (panicstr + sizeof panicstr) - pstr,	\
		    (fmt), (ap));				\
	} while (0)

/* step names */
static const char *steps[] = {
#define STEP(l, u) [STP_##u] = "STP_" #u,
#include "steps.h"
#undef STEP
};
static int nsteps = sizeof steps / sizeof *steps;

/* dump a struct VCL_conf */
static void
dump_vcl(const struct VCL_conf *vcl)
{
	int i;

	fp("    vcl = {\n");
	fp("      srcname = {\n");
	for (i = 0; i < vcl->nsrc; ++i)
		fp("        \"%s\",\n", vcl->srcname[i]);
	fp("      },\n");
	fp("    },\n");
}

/* dump a struct storage */
static void
dump_storage(const struct storage *st)
{
	int i, j;

#define MAX_BYTES (4*16)
#define show(ch) (((ch) > 31 && (ch) < 127) ? (ch) : '.')

	fp("      %u {\n", st->len);
	for (i = 0; i < MAX_BYTES && i < st->len; i += 16) {
		fp("        ");
		for (j = 0; j < 16; ++j) {
			if (i + j < st->len)
				fp("%02x ", st->ptr[i + j]);
			else
				fp("   ");
		}
		fp("|");
		for (j = 0; j < 16; ++j)
			if (i + j < st->len)
				fp("%c", show(st->ptr[i + j]));
		fp("|\n");
	}
	if (st->len > MAX_BYTES)
		fp("        [%u more]\n", st->len - MAX_BYTES);
	fp("      },\n");

#undef show
#undef MAX_BYTES
}

/* dump a struct http */
static void
dump_http(const struct http *h)
{
	int i;

	fp("    http = {\n");
	if (h->nhd > HTTP_HDR_FIRST) {
		fp("      hd = {\n");
		for (i = HTTP_HDR_FIRST; i < h->nhd; ++i)
			fp("        \"%.*s\",\n",
			    (int)(h->hd[i].e - h->hd[i].b),
			    h->hd[i].b);
		fp("      },\n");
	}
	fp("    },\n");
}

/* dump a struct object */
static void
dump_object(const struct object *o)
{
	const struct storage *st;

	fp("  obj = %p {\n", o);
	fp("    refcnt = %u, xid = %u,\n", o->refcnt, o->xid);
	dump_http(o->http);
	fp("    len = %u,\n", o->len);
	fp("    store = {\n");
	VTAILQ_FOREACH(st, &o->store, list) {
		dump_storage(st);
	}
	fp("    },\n");
	fp("  },\n");
}

#if 0
/* dump a struct backend */
static void
dump_backend(const struct backend *be)
{

	fp("  backend = %p {\n", be);
	fp("    vcl_name = \"%s\",\n",
	    be->vcl_name ? be->vcl_name : "(null)");
	fp("  },\n");
}
#endif

/* dump a struct sess */
static void
dump_sess(const struct sess *sp)
{
#if 0
	const struct backend *be = sp->backend;
#endif
	const struct object *obj = sp->obj;
	const struct VCL_conf *vcl = sp->vcl;

	fp("sp = %p {\n", sp);
	fp("  fd = %d, id = %d, xid = %u,\n", sp->fd, sp->id, sp->xid);
	fp("  client = %s:%s,\n",
	    sp->addr ? sp->addr : "?.?.?.?",
	    sp->port ? sp->port : "?");
	if (sp->step < nsteps)
		fp("  step = %s,\n", steps[sp->step]);
	else
		fp("  step = %d,\n", sp->step);
	if (sp->err_code)
		fp("  err_code = %d, err_reason = %s,\n", sp->err_code,
		    sp->err_reason ? sp->err_reason : "(null)");

	if (VALID_OBJ(vcl, VCL_CONF_MAGIC))
		dump_vcl(vcl);

#if 0
	if (VALID_OBJ(be, BACKEND_MAGIC))
		dump_backend(be);
	INCOMPL():
#endif

	if (VALID_OBJ(obj, OBJECT_MAGIC))
		dump_object(obj);

	fp("},\n");
}

/* report as much information as we can before we croak */
void
panic(const char *file, int line, const char *func,
    const struct sess *sp, const char *fmt, ...)
{
	va_list ap;

	fp("panic in %s() at %s:%d\n", func, file, line);
	va_start(ap, fmt);
	vfp(fmt, ap);
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
