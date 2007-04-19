/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006 Linpro AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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

/*
 * XXX:
 *	generate interface structure
 *
 * XXX:
 *	Better error messages, throughout.
 *	>It also accured to me that we could link the errors to the error
 *	>documentation.
 *	>
 *	>Unreferenced  function 'request_policy', first mention is
 *	>         Line 8 Pos 4
 *	>         sub request_policy {
 *	>         ----##############--
 *	>Read more about this type of error:
 *	>http://varnish/doc/error.html#Unreferenced%20function
 *	>
 *	>
 *	>         Unknown variable 'obj.bandwidth'
 *	>         At: Line 88 Pos 12
 *	>                 if (obj.bandwidth < 1 kb/h) {
 *	>         ------------#############------------
 *	>Read more about this type of error:
 *	>http://varnish/doc/error.html#Unknown%20variable
 *
 * XXX:
 *	Create proper tmp filenames for .h, .c and .o
 *
 * XXX:
 *	and all the rest...
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <queue.h>
#include <unistd.h>

#include "compat/asprintf.h"
#include "vsb.h"

#include "vcc_priv.h"
#include "vcc_compile.h"

#include "vrt.h"
#include "libvcl.h"

static struct method method_tab[] = {
#define VCL_RET_MAC(l,U,b,n)
#define VCL_MET_MAC(l,U,m)	{ "vcl_"#l, "default_vcl_"#l, m },
#include "vcl_returns.h"
#undef VCL_MET_MAC
#undef VCL_RET_MAC
	{ NULL, 0U }
};

/*--------------------------------------------------------------------*/

const char *vcc_default_vcl_b, *vcc_default_vcl_e;

/*--------------------------------------------------------------------
 * Printf output to the two vsbs, possibly indented
 */

void
Fh(struct tokenlist *tl, int indent, const char *fmt, ...)
{
	va_list ap;

	if (indent)
		vsb_printf(tl->fh, "%*.*s", tl->indent, tl->indent, "");
	va_start(ap, fmt);
	vsb_vprintf(tl->fh, fmt, ap);
	va_end(ap);
}

void
Fc(struct tokenlist *tl, int indent, const char *fmt, ...)
{
	va_list ap;

	if (indent)
		vsb_printf(tl->fc, "%*.*s", tl->indent, tl->indent, "");
	va_start(ap, fmt);
	vsb_vprintf(tl->fc, fmt, ap);
	va_end(ap);
}

void
Fi(struct tokenlist *tl, int indent, const char *fmt, ...)
{
	va_list ap;

	if (indent)
		vsb_printf(tl->fi, "%*.*s", tl->indent, tl->indent, "");
	va_start(ap, fmt);
	vsb_vprintf(tl->fi, fmt, ap);
	va_end(ap);
}

void
Ff(struct tokenlist *tl, int indent, const char *fmt, ...)
{
	va_list ap;

	if (indent)
		vsb_printf(tl->ff, "%*.*s", tl->indent, tl->indent, "");
	va_start(ap, fmt);
	vsb_vprintf(tl->ff, fmt, ap);
	va_end(ap);
}

/*--------------------------------------------------------------------*/

void
EncString(struct vsb *sb, const char *b, const char *e)
{

	if (e == NULL)
		e = strchr(b, '\0');

	vsb_cat(sb, "\"");
	for (; b < e; b++) {
		switch (*b) {
		case '\\':
		case '"':
			vsb_printf(sb, "\\%c", *b);
			break;
		case '\n': vsb_printf(sb, "\\n"); break;
		case '\t': vsb_printf(sb, "\\t"); break;
		case '\r': vsb_printf(sb, "\\r"); break;
		case ' ': vsb_printf(sb, " "); break;
		default:
			if (isgraph(*b))
				vsb_printf(sb, "%c", *b);
			else
				vsb_printf(sb, "\\%03o", *b);
			break;
		}
	}
	vsb_cat(sb, "\"");
}

void
EncToken(struct vsb *sb, struct token *t)
{

	assert(t->tok == CSTR);
	EncString(sb, t->dec, NULL);
}

/*--------------------------------------------------------------------
 * Keep track of definitions and references
 */

static struct ref *
FindRef(struct tokenlist *tl, struct token *t, enum ref_type type)
{
	struct ref *r;

	TAILQ_FOREACH(r, &tl->refs, list) {
		if (r->type != type)
			continue;
		if (vcc_Teq(r->name, t))
			return (r);
	}
	r = calloc(sizeof *r, 1);
	assert(r != NULL);
	r->name = t;
	r->type = type;
	TAILQ_INSERT_TAIL(&tl->refs, r, list);
	return (r);
}

static int
FindRefStr(struct tokenlist *tl, const char *s, enum ref_type type)
{
	struct ref *r;

	TAILQ_FOREACH(r, &tl->refs, list) {
		if (r->type != type)
			continue;
		if (vcc_IdIs(r->name, s))
			return (1);
	}
	return (0);
}

void
AddRef(struct tokenlist *tl, struct token *t, enum ref_type type)
{

	FindRef(tl, t, type)->refcnt++;
}

static void
AddRefStr(struct tokenlist *tl, const char *s, enum ref_type type)
{
	struct token *t;

	t = calloc(sizeof *t, 1);
	assert(t != NULL);
	t->b = s;
	t->e = strchr(s, '\0');
	t->tok = METHOD;
	AddRef(tl, t, type);
	/* XXX: possibly leaking t */
}

void
AddDef(struct tokenlist *tl, struct token *t, enum ref_type type)
{
	struct ref *r;

	r = FindRef(tl, t, type);
	r->defcnt++;
	r->name = t;
}

/*--------------------------------------------------------------------*/

static struct var *
HeaderVar(struct tokenlist *tl, struct token *t, struct var *vh)
{
	char *p;
	struct var *v;
	int i, w;

	(void)tl;

	v = calloc(sizeof *v, 1);
	assert(v != NULL);
	i = t->e - t->b;
	p = malloc(i + 1);
	assert(p != NULL);
	memcpy(p, t->b, i);
	p[i] = '\0';
	v->name = p;
	v->fmt = STRING;
	if (!memcmp(vh->name, "req.", 4))
		w = 1;
	else
		w = 2;
	asprintf(&p, "VRT_GetHdr(sp, %d, \"\\%03o%s:\")", w,
	    (unsigned)(strlen(v->name + vh->len) + 1), v->name + vh->len);
	assert(p != NULL);
	v->rname = p;
	return (v);
}

/*--------------------------------------------------------------------*/

struct var *
FindVar(struct tokenlist *tl, struct token *t, struct var *vl)
{
	struct var *v;

	for (v = vl; v->name != NULL; v++) {
		if (v->fmt == HEADER  && t->e - t->b <= v->len)
			continue;
		if (v->fmt != HEADER  && t->e - t->b != v->len)
			continue;
		if (memcmp(t->b, v->name, v->len))
			continue;
		if (v->fmt != HEADER)
			return (v);
		return (HeaderVar(tl, t, v));
	}
	vsb_printf(tl->sb, "Unknown variable ");
	vcc_ErrToken(tl, t);
	vsb_cat(tl->sb, "\nAt: ");
	vcc_ErrWhere(tl, t);
	return (NULL);
}

/*--------------------------------------------------------------------
 * Consistency check
 */

struct proc *
AddProc(struct tokenlist *tl, struct token *t, int def)
{
	struct proc *p;

	TAILQ_FOREACH(p, &tl->procs, list) {
		if (!vcc_Teq(p->name, t))
			continue;
		if (def)
			p->name = t;
		return (p);
	}
	p = calloc(sizeof *p, 1);
	assert(p != NULL);
	p->name = t;
	TAILQ_INIT(&p->calls);
	TAILQ_INSERT_TAIL(&tl->procs, p, list);
	return (p);
}

void
AddCall(struct tokenlist *tl, struct token *t)
{
	struct proccall *pc;
	struct proc *p;

	p = AddProc(tl, t, 0);
	TAILQ_FOREACH(pc, &tl->curproc->calls, list) {
		if (pc->p == p)
			return;
	}
	pc = calloc(sizeof *pc, 1);
	assert(pc != NULL);
	pc->p = p;
	pc->t = t;
	TAILQ_INSERT_TAIL(&tl->curproc->calls, pc, list);
}

static int
Consist_Decend(struct tokenlist *tl, struct proc *p, unsigned returns)
{
	unsigned u;
	struct proccall *pc;

	if (!p->exists) {
		vsb_printf(tl->sb, "Function %.*s does not exist\n",
		    PF(p->name));
		return (1);
	}
	if (p->active) {
		vsb_printf(tl->sb, "Function recurses on\n");
		vcc_ErrWhere(tl, p->name);
		return (1);
	}
	u = p->returns & ~returns;
	if (u) {
#define VCL_RET_MAC(a, b, c, d) \
		if (u & VCL_RET_##b) { \
			vsb_printf(tl->sb, "Illegal return for method\n"); \
			vcc_ErrWhere(tl, p->returnt[d]); \
		}
#include "vcl_returns.h"
#undef VCL_RET_MAC
		vsb_printf(tl->sb, "In function\n");
		vcc_ErrWhere(tl, p->name);
		return (1);
	}
	p->active = 1;
	TAILQ_FOREACH(pc, &p->calls, list) {
		if (Consist_Decend(tl, pc->p, returns)) {
			vsb_printf(tl->sb, "\nCalled from\n");
			vcc_ErrWhere(tl, p->name);
			vsb_printf(tl->sb, "at\n");
			vcc_ErrWhere(tl, pc->t);
			return (1);
		}
	}
	p->active = 0;
	p->called++;
	return (0);
}

static int
Consistency(struct tokenlist *tl)
{
	struct proc *p;
	struct method *m;

	TAILQ_FOREACH(p, &tl->procs, list) {
		for(m = method_tab; m->name != NULL; m++) {
			if (vcc_IdIs(p->name, m->defname))
				p->called = 1;
			if (vcc_IdIs(p->name, m->name))
				break;
		}
		if (m->name == NULL)
			continue;
		if (Consist_Decend(tl, p, m->returns)) {
			vsb_printf(tl->sb,
			    "\nwhich is a %s method\n", m->name);
			return (1);
		}
	}
	TAILQ_FOREACH(p, &tl->procs, list) {
		if (p->called)
			continue;
		vsb_printf(tl->sb, "Function unused\n");
		vcc_ErrWhere(tl, p->name);
		return (1);
	}
	return (0);
}

/*--------------------------------------------------------------------*/

static int
CheckRefs(struct tokenlist *tl)
{
	struct ref *r;
	const char *type;
	int nerr = 0;

	TAILQ_FOREACH(r, &tl->refs, list) {
		if (r->defcnt != 0 && r->refcnt != 0)
			continue;
		nerr++;

		switch(r->type) {
		case R_FUNC:
			type = "function";
			break;
		case R_ACL:
			type = "acl";
			break;
		case R_BACKEND:
			type = "backend";
			break;
		default:
			ErrInternal(tl);
			vsb_printf(tl->sb, "Ref ");
			vcc_ErrToken(tl, r->name);
			vsb_printf(tl->sb, " has unknown type %d\n",
			    r->type);
			continue;
		}
		if (r->defcnt == 0 && r->name->tok == METHOD) {
			vsb_printf(tl->sb,
			    "No definition for method %.*s\n", PF(r->name));
			continue;
		}

		if (r->defcnt == 0) {
			vsb_printf(tl->sb,
			    "Undefined %s %.*s, first reference:\n",
			    type, PF(r->name));
			vcc_ErrWhere(tl, r->name);
			continue;
		}

		vsb_printf(tl->sb, "Unused %s %.*s, defined:\n",
		    type, PF(r->name));
		vcc_ErrWhere(tl, r->name);
	}
	return (nerr);
}

/*--------------------------------------------------------------------
 * Output the location/profiling table.  For each counted token, we
 * record source+line+charpos for the first character in the token.
 */

static void
LocTable(struct tokenlist *tl)
{
	struct token *t;
	unsigned lin, pos;
	struct source *sp;
	const char *p;

	Fh(tl, 0, "#define VGC_NREFS %u\n", tl->cnt + 1);
	Fh(tl, 0, "static struct vrt_ref VGC_ref[VGC_NREFS];\n");
	Fc(tl, 0, "static struct vrt_ref VGC_ref[VGC_NREFS] = {\n");
	lin = 1;
	pos = 0;
	sp = 0;
	p = NULL;
	TAILQ_FOREACH(t, &tl->tokens, list) {
		if (t->cnt == 0)
			continue;
		assert(t->src != NULL);
		if (t->src != sp) {
			lin = 1;
			pos = 0;
			sp = t->src;
			p = sp->b;
		}
		assert(sp != NULL);
		assert(p != NULL);
		for (;p < t->b; p++) {
			if (*p == '\n') {
				lin++;
				pos = 0;
			} else if (*p == '\t') {
				pos &= ~7;
				pos += 8;
			} else
				pos++;

		}
		Fc(tl, 0, "  [%3u] = { %d, %8u, %4u, %3u, 0, \"%.*s\" },\n",
		    t->cnt, sp->idx, t->b - sp->b, lin, pos + 1, PF(t));
	}
	Fc(tl, 0, "};\n");
}

/*--------------------------------------------------------------------*/

static void
EmitInitFunc(struct tokenlist *tl)
{

	Fc(tl, 0, "\nstatic void\nVGC_Init(void)\n{\n\n");
	vsb_finish(tl->fi);
	vsb_cat(tl->fc, vsb_data(tl->fi));
	Fc(tl, 0, "}\n");
}

static void
EmitFiniFunc(struct tokenlist *tl)
{

	Fc(tl, 0, "\nstatic void\nVGC_Fini(void)\n{\n\n");
	vsb_finish(tl->ff);
	vsb_cat(tl->fc, vsb_data(tl->ff));
	Fc(tl, 0, "}\n");
}

/*--------------------------------------------------------------------*/

static void
EmitStruct(struct tokenlist *tl)
{
	struct source *sp;

	Fc(tl, 0, "\nconst char *srcname[%u] = {\n", tl->nsources);
	TAILQ_FOREACH(sp, &tl->sources, list) {
		Fc(tl, 0, "\t");
		EncString(tl->fc, sp->name, NULL);
		Fc(tl, 0, ",\n");
	}
	Fc(tl, 0, "};\n");
	
	Fc(tl, 0, "\nconst char *srcbody[%u] = {\n", tl->nsources);
	TAILQ_FOREACH(sp, &tl->sources, list) {
		Fc(tl, 0, "\t");
		EncString(tl->fc, sp->b, sp->e);
		Fc(tl, 0, ",\n");
	}
	Fc(tl, 0, "};\n");

	Fc(tl, 0, "\nstruct VCL_conf VCL_conf = {\n");
	Fc(tl, 0, "\t.magic = VCL_CONF_MAGIC,\n");
	Fc(tl, 0, "\t.init_func = VGC_Init,\n");
	Fc(tl, 0, "\t.fini_func = VGC_Fini,\n");
	Fc(tl, 0, "\t.nbackend = %d,\n", tl->nbackend);
	Fc(tl, 0, "\t.ref = VGC_ref,\n");
	Fc(tl, 0, "\t.nref = VGC_NREFS,\n");
	Fc(tl, 0, "\t.nsrc = %u,\n", tl->nsources);
	Fc(tl, 0, "\t.srcname = srcname,\n");
	Fc(tl, 0, "\t.srcbody = srcbody,\n");
#define VCL_RET_MAC(l,u,b,n)
#define VCL_MET_MAC(l,u,b) \
	if (FindRefStr(tl, "vcl_" #l, R_FUNC)) { \
		Fc(tl, 0, "\t." #l "_func = VGC_function_vcl_" #l ",\n"); \
		AddRefStr(tl, "vcl_" #l, R_FUNC); \
	} else { \
		Fc(tl, 0, "\t." #l "_func = VGC_function_default_vcl_" #l ",\n"); \
	} \
	AddRefStr(tl, "default_vcl_" #l, R_FUNC);
#include "vcl_returns.h"
#undef VCL_MET_MAC
#undef VCL_RET_MAC
	Fc(tl, 0, "};\n");
}

/*--------------------------------------------------------------------*/

static struct source *
vcc_new_source(const char *b, const char *e, const char *name)
{
	struct source *sp;

	if (e == NULL)
		e = strchr(b, '\0');
	sp = calloc(sizeof *sp, 1);
	assert(sp != NULL);
	sp->name = strdup(name);
	sp->b = b;
	sp->e = e;
	return (sp);
}

static void
vcc_destroy_source(struct source *sp)
{

	free(sp->name);	
	free(sp);
}

/*--------------------------------------------------------------------*/

static struct source *
vcc_file_source(struct vsb *sb, const char *fn)
{
	char *f;
	int fd, i;
	struct stat st;

	fd = open(fn, O_RDONLY);
	if (fd < 0) {
		vsb_printf(sb, "Cannot open file '%s': %s\n",
		    fn, strerror(errno));
		return (NULL);
	}
	assert(0 == fstat(fd, &st));
	f = malloc(st.st_size + 1);
	assert(f != NULL);
	i = read(fd, f, st.st_size);
	assert(i == st.st_size);
	close(fd);
	f[i] = '\0';
	return (vcc_new_source(f, f + i, fn));
}

/*--------------------------------------------------------------------*/

static void
vcc_resolve_includes(struct tokenlist *tl)
{
	struct token *t, *t1, *t2;
	struct source *sp;

	TAILQ_FOREACH(t, &tl->tokens, list) {
		if (t->tok != T_INCLUDE)
			continue;

		t1 = TAILQ_NEXT(t, list);
		assert(t1 != NULL);	/* There's always an EOI */
		if (t1->tok != CSTR) {
			vsb_printf(tl->sb,
			    "include not followed by string constant.\n");
			vcc_ErrWhere(tl, t1);
			return;
		}
		t2 = TAILQ_NEXT(t1, list);
		assert(t2 != NULL);	/* There's always an EOI */
		if (t2->tok != ';') {
			vsb_printf(tl->sb,
			    "include <string> not followed by semicolon.\n");
			vcc_ErrWhere(tl, t1);
			return;
		}
		assert(t2 != NULL);

		sp = vcc_file_source(tl->sb, t1->dec);
		if (sp == NULL) {
			vcc_ErrWhere(tl, t1);
			return;
		}
		TAILQ_INSERT_TAIL(&tl->sources, sp, list);
		sp->idx = tl->nsources++;
		tl->t = t2;
		vcc_Lexer(tl, sp);

		TAILQ_REMOVE(&tl->tokens, t, list);
		TAILQ_REMOVE(&tl->tokens, t1, list);
		TAILQ_REMOVE(&tl->tokens, t2, list);
		vcc_FreeToken(t);
		vcc_FreeToken(t1);
		vcc_FreeToken(t2);
		if (!tl->err)
			vcc_resolve_includes(tl);
		return;
	}
}

/*--------------------------------------------------------------------*/

static char *
vcc_CompileSource(struct vsb *sb, struct source *sp)
{
	struct tokenlist tokenlist, *tl;
	struct ref *r;
	struct token *t;
	FILE *fo;
	char *of = NULL;
	char buf[BUFSIZ];
	int i;

	memset(&tokenlist, 0, sizeof tokenlist);
	tl = &tokenlist;
	TAILQ_INIT(&tl->tokens);
	TAILQ_INIT(&tl->refs);
	TAILQ_INIT(&tl->procs);
	TAILQ_INIT(&tl->sources);
	tl->sb = sb;

	tl->nsources = 0;

	/* General C code */
	tl->fc = vsb_new(NULL, NULL, 0, VSB_AUTOEXTEND);
	assert(tl->fc != NULL);

	/* Forward decls (.h like) */
	tl->fh = vsb_new(NULL, NULL, 0, VSB_AUTOEXTEND);
	assert(tl->fh != NULL);

	/* Init C code */
	tl->fi = vsb_new(NULL, NULL, 0, VSB_AUTOEXTEND);
	assert(tl->fi != NULL);

	/* Finish C code */
	tl->ff = vsb_new(NULL, NULL, 0, VSB_AUTOEXTEND);
	assert(tl->ff != NULL);

	/* body code of methods */
#define VCL_MET_MAC(l,U,m) \
		tl->fm_##l = vsb_new(NULL, NULL, 0, VSB_AUTOEXTEND); \
		assert(tl->fm_##l != NULL);
#include "vcl_returns.h"
#undef VCL_MET_MAC

	Fh(tl, 0, "extern struct VCL_conf VCL_conf;\n");

	Fi(tl, 0, "\tVRT_alloc_backends(&VCL_conf);\n");

	TAILQ_INSERT_TAIL(&tl->sources, sp, list);
	sp->idx = tl->nsources++;
	vcc_Lexer(tl, sp);
	if (tl->err)
		goto done;

	sp = vcc_new_source(vcc_default_vcl_b, vcc_default_vcl_e, "Default");
	TAILQ_INSERT_TAIL(&tl->sources, sp, list);
	sp->idx = tl->nsources++;
	vcc_Lexer(tl, sp);
	vcc_AddToken(tl, EOI, sp->e, sp->e);
	if (tl->err)
		goto done;

	vcc_resolve_includes(tl);
	if (tl->err)
		goto done;

	tl->t = TAILQ_FIRST(&tl->tokens);
	vcc_Parse(tl);
	if (tl->err)
		goto done;
	Consistency(tl);
	if (tl->err)
		goto done;
	LocTable(tl);

	Ff(tl, 0, "\tVRT_free_backends(&VCL_conf);\n");

	EmitInitFunc(tl);

	EmitFiniFunc(tl);

	EmitStruct(tl);

	if (CheckRefs(tl))
		goto done;

	of = strdup("/tmp/vcl.XXXXXXXX");
	assert(of != NULL);
	mktemp(of);

	sprintf(buf,
	    "tee /tmp/_.c |"
	    "cc -fpic -shared -Wl,-x -o %s -x c - ", of);

	fo = popen(buf, "w");
	assert(fo != NULL);

	vcl_output_lang_h(fo);
	fputs(vrt_obj_h, fo);

	vsb_finish(tl->fh);
	fputs(vsb_data(tl->fh), fo);
	vsb_delete(tl->fh);

	vsb_finish(tl->fc);
	fputs(vsb_data(tl->fc), fo);
	vsb_delete(tl->fc);

	i = pclose(fo);
	fprintf(stderr, "pclose=%d\n", i);
	if (i) {
		vsb_printf(sb, "Internal error: GCC returned 0x%04x\n", i);
		unlink(of);
		free(of);
		return (NULL);
	}
done:

#define VCL_MET_MAC(l,U,m) vsb_delete(tl->fm_##l);
#include "vcl_returns.h"
#undef VCL_MET_MAC

	/* Free References */
	while (!TAILQ_EMPTY(&tl->refs)) {
		r = TAILQ_FIRST(&tl->refs);
		TAILQ_REMOVE(&tl->refs, r, list);
		free(r);
	}

	/* Free Tokens */
	while (!TAILQ_EMPTY(&tl->tokens)) {
		t = TAILQ_FIRST(&tl->tokens);
		TAILQ_REMOVE(&tl->tokens, t, list);
		vcc_FreeToken(t);
	}
	return (of);
}

/*--------------------------------------------------------------------*/

char *
VCC_Compile(struct vsb *sb, const char *b, const char *e)
{
	struct source *sp;
	char *r;

	sp = vcc_new_source(b, e, "input");
	if (sp == NULL)
		return (NULL);
	r = vcc_CompileSource(sb, sp);
	vcc_destroy_source(sp);
	return (r);
}

/*--------------------------------------------------------------------*/

char *
VCC_CompileFile(struct vsb *sb, const char *fn)
{
	struct source *sp;
	char *r;

	sp = vcc_file_source(sb, fn);
	if (sp == NULL)
		return (NULL);
	r = vcc_CompileSource(sb, sp);
	vcc_destroy_source(sp);
	return (r);
}

/*--------------------------------------------------------------------*/

void
VCC_InitCompile(const char *default_vcl)
{
	struct var *v;

	vcc_default_vcl_b = default_vcl;
	vcc_default_vcl_e = strchr(default_vcl, '\0');
	assert(vcc_default_vcl_e != NULL);

	vcl_init_tnames();
	for (v = vcc_vars; v->name != NULL; v++)
		v->len = strlen(v->name);
	for (v = vcc_be_vars; v->name != NULL; v++)
		v->len = strlen(v->name);
}
