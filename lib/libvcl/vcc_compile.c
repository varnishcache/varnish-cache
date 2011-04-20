/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Linpro AS
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

/*
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
 */

#include "config.h"

#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vqueue.h"

#include "vsb.h"

#include "vcc_priv.h"
#include "vcc_compile.h"

#include "libvcl.h"
#include "libvarnish.h"

struct method method_tab[] = {
#define VCL_MET_MAC(l,U,m)	{ "vcl_"#l, m, VCL_MET_##U },
#include "vcl_returns.h"
#undef VCL_MET_MAC
	{ NULL, 0U, 0}
};

/*--------------------------------------------------------------------*/

static void
TlDoFree(struct vcc *tl, void *p)
{
	struct membit *mb;

	mb = calloc(sizeof *mb, 1);
	assert(mb != NULL);
	mb->ptr = p;
	VTAILQ_INSERT_TAIL(&tl->membits, mb, list);
}


void *
TlAlloc(struct vcc *tl, unsigned len)
{
	void *p;

	p = calloc(len, 1);
	assert(p != NULL);
	TlDoFree(tl, p);
	return (p);
}

char *
TlDup(struct vcc *tl, const char *s)
{
	char *p;

	p = TlAlloc(tl, strlen(s) + 1);
	AN(p);
	strcpy(p, s);
	return (p);
}

char *
TlDupTok(struct vcc *tl, const struct token *tok)
{
	char *p;
	int i;

	i = tok->e - tok->b;
	p = TlAlloc(tl, i + 1);
	AN(p);
	memcpy(p, tok->b, i);
	p[i] = '\0';
	return (p);
}

/*--------------------------------------------------------------------*/

int
IsMethod(const struct token *t)
{
	struct method *m;

	assert(t->tok == ID);
	for(m = method_tab; m->name != NULL; m++) {
		if (vcc_IdIs(t, m->name))
			return (m - method_tab);
	}
	return (-1);
}

/*--------------------------------------------------------------------
 * Printf output to the vsbs, possibly indented
 */

void
Fh(const struct vcc *tl, int indent, const char *fmt, ...)
{
	va_list ap;

	if (indent)
		vsb_printf(tl->fh, "%*.*s", tl->hindent, tl->hindent, "");
	va_start(ap, fmt);
	vsb_vprintf(tl->fh, fmt, ap);
	va_end(ap);
}

void
Fb(const struct vcc *tl, int indent, const char *fmt, ...)
{
	va_list ap;

	assert(tl->fb != NULL);
	if (indent)
		vsb_printf(tl->fb, "%*.*s", tl->indent, tl->indent, "");
	va_start(ap, fmt);
	vsb_vprintf(tl->fb, fmt, ap);
	va_end(ap);
}

void
Fc(const struct vcc *tl, int indent, const char *fmt, ...)
{
	va_list ap;

	if (indent)
		vsb_printf(tl->fc, "%*.*s", tl->indent, tl->indent, "");
	va_start(ap, fmt);
	vsb_vprintf(tl->fc, fmt, ap);
	va_end(ap);
}

void
Fi(const struct vcc *tl, int indent, const char *fmt, ...)
{
	va_list ap;

	if (indent)
		vsb_printf(tl->fi, "%*.*s", tl->iindent, tl->iindent, "");
	va_start(ap, fmt);
	vsb_vprintf(tl->fi, fmt, ap);
	va_end(ap);
}

void
Ff(const struct vcc *tl, int indent, const char *fmt, ...)
{
	va_list ap;

	if (indent)
		vsb_printf(tl->ff, "%*.*s", tl->findent, tl->findent, "");
	va_start(ap, fmt);
	vsb_vprintf(tl->ff, fmt, ap);
	va_end(ap);
}

/*--------------------------------------------------------------------*/

void
EncString(struct vsb *sb, const char *b, const char *e, int mode)
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
		case '\n':
			vsb_printf(sb, "\\n");
			if (mode)
				vsb_printf(sb, "\"\n\t\"");
			break;
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
EncToken(struct vsb *sb, const struct token *t)
{

	assert(t->tok == CSTR);
	EncString(sb, t->dec, NULL, 0);
}

/*--------------------------------------------------------------------
 * Output the location/profiling table.  For each counted token, we
 * record source+line+charpos for the first character in the token.
 */

static void
LocTable(const struct vcc *tl)
{
	struct token *t;
	unsigned lin, pos;
	struct source *sp;
	const char *p;

	Fh(tl, 0, "\n#define VGC_NREFS %u\n", tl->cnt + 1);
	Fc(tl, 0, "\nstatic struct vrt_ref VGC_ref[VGC_NREFS] = {\n");
	lin = 1;
	pos = 0;
	sp = 0;
	p = NULL;
	VTAILQ_FOREACH(t, &tl->tokens, list) {
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
		Fc(tl, 0, "  [%3u] = { %d, %8u, %4u, %3u, 0, ",
		    t->cnt, sp->idx, t->b - sp->b, lin, pos + 1);
		if (t->tok == CSRC)
			Fc(tl, 0, " \"C{\"},\n");
		else
			Fc(tl, 0, " \"%.*s\" },\n", PF(t));
	}
	Fc(tl, 0, "};\n");
}

/*--------------------------------------------------------------------*/

static void
EmitInitFunc(const struct vcc *tl)
{

	Fc(tl, 0, "\nstatic void\nVGC_Init(struct cli *cli)\n{\n\n");
	AZ(vsb_finish(tl->fi));
	vsb_cat(tl->fc, vsb_data(tl->fi));
	Fc(tl, 0, "}\n");
}

static void
EmitFiniFunc(const struct vcc *tl)
{
	unsigned u;

	Fc(tl, 0, "\nstatic void\nVGC_Fini(struct cli *cli)\n{\n\n");

	/*
	 * We do this here, so we are sure they happen before any
	 * per-vcl vmod_privs get cleaned.
	 */
	for (u = 0; u < tl->nvmodpriv; u++)
		Fc(tl, 0, "\tvmod_priv_fini(&vmod_priv_%u);\n", u);

	AZ(vsb_finish(tl->ff));
	vsb_cat(tl->fc, vsb_data(tl->ff));
	Fc(tl, 0, "}\n");
}

/*--------------------------------------------------------------------*/

static void
EmitStruct(const struct vcc *tl)
{
	struct source *sp;

	Fc(tl, 0, "\nconst char *srcname[%u] = {\n", tl->nsources);
	VTAILQ_FOREACH(sp, &tl->sources, list) {
		Fc(tl, 0, "\t");
		EncString(tl->fc, sp->name, NULL, 0);
		Fc(tl, 0, ",\n");
	}
	Fc(tl, 0, "};\n");

	Fc(tl, 0, "\nconst char *srcbody[%u] = {\n", tl->nsources);
	VTAILQ_FOREACH(sp, &tl->sources, list) {
		Fc(tl, 0, "    /* ");
		EncString(tl->fc, sp->name, NULL, 0);
		Fc(tl, 0, "*/\n");
		Fc(tl, 0, "\t");
		EncString(tl->fc, sp->b, sp->e, 1);
		Fc(tl, 0, ",\n");
	}
	Fc(tl, 0, "};\n");

	Fc(tl, 0, "\nstatic struct director\t*directors[%d];\n",
	    tl->ndirector);

	Fc(tl, 0, "\nconst struct VCL_conf VCL_conf = {\n");
	Fc(tl, 0, "\t.magic = VCL_CONF_MAGIC,\n");
	Fc(tl, 0, "\t.init_func = VGC_Init,\n");
	Fc(tl, 0, "\t.fini_func = VGC_Fini,\n");
	Fc(tl, 0, "\t.ndirector = %d,\n", tl->ndirector);
	Fc(tl, 0, "\t.director = directors,\n");
	Fc(tl, 0, "\t.ref = VGC_ref,\n");
	Fc(tl, 0, "\t.nref = VGC_NREFS,\n");
	Fc(tl, 0, "\t.nsrc = %u,\n", tl->nsources);
	Fc(tl, 0, "\t.srcname = srcname,\n");
	Fc(tl, 0, "\t.srcbody = srcbody,\n");
#define VCL_MET_MAC(l,u,b) \
	Fc(tl, 0, "\t." #l "_func = VGC_function_vcl_" #l ",\n");
#include "vcl_returns.h"
#undef VCL_MET_MAC
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
	AN(sp->name);
	sp->b = b;
	sp->e = e;
	return (sp);
}

static void
vcc_destroy_source(struct source *sp)
{

	if (sp->freeit != NULL)
		free(sp->freeit);
	free(sp->name);
	free(sp);
}

/*--------------------------------------------------------------------*/

static struct source *
vcc_file_source(const struct vcc *tl, struct vsb *sb, const char *fn)
{
	char *f;
	struct source *sp;

	f = vreadfile(tl->vcl_dir, fn);
	if (f == NULL) {
		vsb_printf(sb, "Cannot read file '%s': %s\n",
		    fn, strerror(errno));
		return (NULL);
	}
	sp = vcc_new_source(f, NULL, fn);
	sp->freeit = f;
	return (sp);
}

/*--------------------------------------------------------------------*/

static void
vcc_resolve_includes(struct vcc *tl)
{
	struct token *t, *t1, *t2;
	struct source *sp;

	VTAILQ_FOREACH(t, &tl->tokens, list) {
		if (t->tok != T_INCLUDE)
			continue;

		t1 = VTAILQ_NEXT(t, list);
		assert(t1 != NULL);	/* There's always an EOI */
		if (t1->tok != CSTR) {
			vsb_printf(tl->sb,
			    "include not followed by string constant.\n");
			vcc_ErrWhere(tl, t1);
			return;
		}
		t2 = VTAILQ_NEXT(t1, list);
		assert(t2 != NULL);	/* There's always an EOI */
		if (t2->tok != ';') {
			vsb_printf(tl->sb,
			    "include <string> not followed by semicolon.\n");
			vcc_ErrWhere(tl, t1);
			return;
		}
		assert(t2 != NULL);

		sp = vcc_file_source(tl, tl->sb, t1->dec);
		if (sp == NULL) {
			vcc_ErrWhere(tl, t1);
			return;
		}
		VTAILQ_INSERT_TAIL(&tl->sources, sp, list);
		sp->idx = tl->nsources++;
		tl->t = t2;
		vcc_Lexer(tl, sp);

		VTAILQ_REMOVE(&tl->tokens, t, list);
		VTAILQ_REMOVE(&tl->tokens, t1, list);
		VTAILQ_REMOVE(&tl->tokens, t2, list);
		if (!tl->err)
			vcc_resolve_includes(tl);
		return;
	}
}

/*--------------------------------------------------------------------*/

static struct vcc *
vcc_NewVcc(const struct vcc *tl0)
{
	struct vcc *tl;
	int i;

	ALLOC_OBJ(tl, VCC_MAGIC);
	AN(tl);
	if (tl0 != NULL) {
		REPLACE(tl->default_vcl, tl0->default_vcl);
		REPLACE(tl->vcl_dir, tl0->vcl_dir);
		REPLACE(tl->vmod_dir, tl0->vmod_dir);
		tl->vars = tl0->vars;
		tl->err_unref = tl0->err_unref;
	} else {
		tl->err_unref = 1;
	}
	VTAILQ_INIT(&tl->symbols);
	VTAILQ_INIT(&tl->hosts);
	VTAILQ_INIT(&tl->membits);
	VTAILQ_INIT(&tl->tokens);
	VTAILQ_INIT(&tl->sources);

	tl->nsources = 0;
	tl->ndirector = 1;

	/* General C code */
	tl->fc = vsb_new_auto();
	assert(tl->fc != NULL);

	/* Forward decls (.h like) */
	tl->fh = vsb_new_auto();
	assert(tl->fh != NULL);

	/* Init C code */
	tl->fi = vsb_new_auto();
	assert(tl->fi != NULL);

	/* Finish C code */
	tl->ff = vsb_new_auto();
	assert(tl->ff != NULL);

	/* body code of methods */
	for (i = 0; i < VCL_MET_MAX; i++) {
		tl->fm[i] = vsb_new_auto();
		assert(tl->fm[i] != NULL);
	}
	return (tl);
}

/*--------------------------------------------------------------------*/

static char *
vcc_DestroyTokenList(struct vcc *tl, char *ret)
{
	struct membit *mb;
	struct source *sp;
	struct symbol *sym;
	int i;

	while (!VTAILQ_EMPTY(&tl->membits)) {
		mb = VTAILQ_FIRST(&tl->membits);
		VTAILQ_REMOVE(&tl->membits, mb, list);
		free(mb->ptr);
		free(mb);
	}
	while (!VTAILQ_EMPTY(&tl->sources)) {
		sp = VTAILQ_FIRST(&tl->sources);
		VTAILQ_REMOVE(&tl->sources, sp, list);
		vcc_destroy_source(sp);
	}

	while (!VTAILQ_EMPTY(&tl->symbols)) {
		sym = VTAILQ_FIRST(&tl->symbols);
		VTAILQ_REMOVE(&tl->symbols, sym, list);
		FREE_OBJ(sym);
	}

	vsb_delete(tl->fh);
	vsb_delete(tl->fc);
	vsb_delete(tl->fi);
	vsb_delete(tl->ff);
	for (i = 0; i < VCL_MET_MAX; i++)
		vsb_delete(tl->fm[i]);

	free(tl);
	return (ret);
}

/*--------------------------------------------------------------------
 * Compile the VCL code from the given source and return the C-source
 */

static char *
vcc_CompileSource(const struct vcc *tl0, struct vsb *sb, struct source *sp)
{
	struct vcc *tl;
	struct symbol *sym;
	const struct var *v;
	char *of;
	int i;

	tl = vcc_NewVcc(tl0);
	tl->sb = sb;

	vcc_Expr_Init(tl);

	for (v = tl->vars; v->name != NULL; v++) {
		if (v->fmt == HEADER) {
			sym = VCC_AddSymbolStr(tl, v->name, SYM_WILDCARD);
			sym->wildcard = vcc_Var_Wildcard;
		} else {
			sym = VCC_AddSymbolStr(tl, v->name, SYM_VAR);
		}
		sym->var = v;
		sym->fmt = v->fmt;
		sym->eval = vcc_Eval_Var;
		sym->r_methods = v->r_methods;
	}

	sym = VCC_AddSymbolStr(tl, "storage", SYM_WILDCARD);
	sym->wildcard = vcc_Stv_Wildcard;

	vcl_output_lang_h(tl->fh);
	Fh(tl, 0, "\n/* ---===### VCC generated below here ###===---*/\n");
	Fh(tl, 0, "\nextern const struct VCL_conf VCL_conf;\n");

	/* Macro for accessing directors */
	Fh(tl, 0, "#define VGCDIR(n) VCL_conf.director[VGC_backend_##n]\n");

	/* Register and lex the main source */
	VTAILQ_INSERT_TAIL(&tl->sources, sp, list);
	sp->idx = tl->nsources++;
	vcc_Lexer(tl, sp);
	if (tl->err)
		return (vcc_DestroyTokenList(tl, NULL));

	/* Register and lex the default VCL */
	sp = vcc_new_source(tl->default_vcl, NULL, "Default");
	assert(sp != NULL);
	VTAILQ_INSERT_TAIL(&tl->sources, sp, list);
	sp->idx = tl->nsources++;
	vcc_Lexer(tl, sp);
	if (tl->err)
		return (vcc_DestroyTokenList(tl, NULL));

	/* Add "END OF INPUT" token */
	vcc_AddToken(tl, EOI, sp->e, sp->e);
	if (tl->err)
		return (vcc_DestroyTokenList(tl, NULL));

	/* Expand and lex any includes in the token string */
	vcc_resolve_includes(tl);
	if (tl->err)
		return (vcc_DestroyTokenList(tl, NULL));

	/* Parse the token string */
	tl->t = VTAILQ_FIRST(&tl->tokens);
	vcc_Parse(tl);
	if (tl->err)
		return (vcc_DestroyTokenList(tl, NULL));

	/* Check if we have any backends at all */
	if (tl->ndirector == 1) {
		vsb_printf(tl->sb,
		    "No backends or directors found in VCL program, "
		    "at least one is necessary.\n");
		tl->err = 1;
		return (vcc_DestroyTokenList(tl, NULL));
	}

	/* Configure the default director */
	Fi(tl, 0, "\tVCL_conf.director[0] = VCL_conf.director[%d];\n",
	    tl->defaultdir);
	vcc_AddRef(tl, tl->t_defaultdir, SYM_BACKEND);

	/* Check for orphans */
	if (vcc_CheckReferences(tl))
		return (vcc_DestroyTokenList(tl, NULL));

	/* Check that all action returns are legal */
	if (vcc_CheckAction(tl) || tl->err)
		return (vcc_DestroyTokenList(tl, NULL));

	/* Check that all variable uses are legal */
	if (vcc_CheckUses(tl) || tl->err)
		return (vcc_DestroyTokenList(tl, NULL));

	/* Emit method functions */
	for (i = 0; i < VCL_MET_MAX; i++) {
		Fc(tl, 1, "\nstatic int\n");
		Fc(tl, 1, "VGC_function_%s (struct sess *sp)\n",
		    method_tab[i].name);
		AZ(vsb_finish(tl->fm[i]));
		Fc(tl, 1, "{\n");
		Fc(tl, 1, "%s", vsb_data(tl->fm[i]));
		Fc(tl, 1, "}\n");
	}

	LocTable(tl);

	EmitInitFunc(tl);

	EmitFiniFunc(tl);

	EmitStruct(tl);

	/* Combine it all in the fh vsb */
	AZ(vsb_finish(tl->fc));
	vsb_cat(tl->fh, vsb_data(tl->fc));
	AZ(vsb_finish(tl->fh));

	of = strdup(vsb_data(tl->fh));
	AN(of);

	/* done */
	return (vcc_DestroyTokenList(tl, of));
}

/*--------------------------------------------------------------------
 * Compile the VCL code in the argument.  Error messages, if any are
 * formatted into the vsb.
 */

char *
VCC_Compile(const struct vcc *tl, struct vsb *sb, const char *b)
{
	struct source *sp;
	char *r;

	sp = vcc_new_source(b, NULL, "input");
	if (sp == NULL)
		return (NULL);
	r = vcc_CompileSource(tl, sb, sp);
	return (r);
}

/*--------------------------------------------------------------------*/

const char *
VCC_Return_Name(unsigned method)
{

	switch (method) {
#define VCL_RET_MAC(l, U, B) case VCL_RET_##U: return(#l);
#include "vcl_returns.h"
#undef VCL_RET_MAC
	default:
		return (NULL);
	}
}

/*--------------------------------------------------------------------
 * Allocate a compiler instance
 */

struct vcc *
VCC_New(void)
{
	struct vcc *tl;

	tl = vcc_NewVcc(NULL);

	tl->vars = vcc_vars;

	return (tl);
}

/*--------------------------------------------------------------------
 * Configure default VCL source code
 */

void
VCC_Default_VCL(struct vcc *tl, const char *str)
{

	CHECK_OBJ_NOTNULL(tl, VCC_MAGIC);
	REPLACE(tl->default_vcl, str);
}

/*--------------------------------------------------------------------
 * Configure default VCL source directory
 */

void
VCC_VCL_dir(struct vcc *tl, const char *str)
{

	CHECK_OBJ_NOTNULL(tl, VCC_MAGIC);
	REPLACE(tl->vcl_dir, str);
}

/*--------------------------------------------------------------------
 * Configure default VMOD directory
 */

void
VCC_VMOD_dir(struct vcc *tl, const char *str)
{

	CHECK_OBJ_NOTNULL(tl, VCC_MAGIC);
	REPLACE(tl->vmod_dir, str);
}

/*--------------------------------------------------------------------
 * Configure default
 */

void
VCC_Err_Unref(struct vcc *tl, unsigned u)
{

	CHECK_OBJ_NOTNULL(tl, VCC_MAGIC);
	tl->err_unref = u;
}
