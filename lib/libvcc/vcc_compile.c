/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 *	>It also occurred to me that we could link the errors to the error
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

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vcc_compile.h"

#include "libvcc.h"
#include "vfil.h"

struct method method_tab[] = {
	{ "none", 0U, 0},
#define VCL_MET_MAC(l,U,t,b)	{ "vcl_"#l, b, VCL_MET_##U },
#include "tbl/vcl_returns.h"
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

struct inifin *
New_IniFin(struct vcc *tl)
{
	struct inifin *p;

	p = TlAlloc(tl, sizeof *p);
	AN(p);
	p->magic = INIFIN_MAGIC;
	p->ini = VSB_new_auto();
	p->fin = VSB_new_auto();
	p->event = VSB_new_auto();
	p->n = ++tl->ninifin;
	VTAILQ_INSERT_TAIL(&tl->inifin, p, list);
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
	if ((t->b[0] == 'v'|| t->b[0] == 'V') &&
	    (t->b[1] == 'c'|| t->b[1] == 'C') &&
	    (t->b[2] == 'l'|| t->b[2] == 'L'))
		return (-2);
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
		VSB_printf(tl->fh, "%*.*s", tl->hindent, tl->hindent, "");
	va_start(ap, fmt);
	VSB_vprintf(tl->fh, fmt, ap);
	va_end(ap);
}

void
Fb(const struct vcc *tl, int indent, const char *fmt, ...)
{
	va_list ap;

	assert(tl->fb != NULL);
	if (indent)
		VSB_printf(tl->fb, "%*.*s", tl->indent, tl->indent, "");
	va_start(ap, fmt);
	VSB_vprintf(tl->fb, fmt, ap);
	va_end(ap);
}

void
Fc(const struct vcc *tl, int indent, const char *fmt, ...)
{
	va_list ap;

	if (indent)
		VSB_printf(tl->fc, "%*.*s", tl->indent, tl->indent, "");
	va_start(ap, fmt);
	VSB_vprintf(tl->fc, fmt, ap);
	va_end(ap);
}

/*--------------------------------------------------------------------*/

void
EncString(struct vsb *sb, const char *b, const char *e, int mode)
{

	if (e == NULL)
		e = strchr(b, '\0');

	VSB_cat(sb, "\"");
	for (; b < e; b++) {
		switch (*b) {
		case '?':	// Trigraphs
		case '\\':
		case '"':
			VSB_printf(sb, "\\%c", *b);
			break;
		case '\n':
			VSB_printf(sb, "\\n");
			if (mode)
				VSB_printf(sb, "\"\n\t\"");
			break;
		case '\t': VSB_printf(sb, "\\t"); break;
		case '\r': VSB_printf(sb, "\\r"); break;
		case ' ': VSB_printf(sb, " "); break;
		default:
			if (isgraph(*b))
				VSB_printf(sb, "%c", *b);
			else
				VSB_printf(sb, "\\%03o", (uint8_t)*b);
			break;
		}
	}
	VSB_cat(sb, "\"");
}

void
EncToken(struct vsb *sb, const struct token *t)
{

	assert(t->tok == CSTR);
	EncString(sb, t->dec, NULL, 1);
}

/*--------------------------------------------------------------------
 * Output the location/profiling table.  For each counted token, we
 * record source+line+charpos for the first character in the token.
 */

static void
EmitCoordinates(const struct vcc *tl, struct vsb *vsb)
{
	struct token *t;
	unsigned lin, pos;
	struct source *sp;
	const char *p;

	VSB_printf(vsb, "/* ---===### Source Code ###===---*/\n");

	VSB_printf(vsb, "\n#define VGC_NSRCS %u\n", tl->nsources);

	VSB_printf(vsb, "\nstatic const char *srcname[VGC_NSRCS] = {\n");
	VTAILQ_FOREACH(sp, &tl->sources, list) {
		VSB_printf(vsb, "\t");
		EncString(vsb, sp->name, NULL, 0);
		VSB_printf(vsb, ",\n");
	}
	VSB_printf(vsb, "};\n");

	VSB_printf(vsb, "\nstatic const char *srcbody[%u] = {\n", tl->nsources);
	VTAILQ_FOREACH(sp, &tl->sources, list) {
		VSB_printf(vsb, "    /* ");
		EncString(vsb, sp->name, NULL, 0);
		VSB_printf(vsb, "*/\n");
		VSB_printf(vsb, "\t");
		EncString(vsb, sp->b, sp->e, 1);
		VSB_printf(vsb, ",\n");
	}
	VSB_printf(vsb, "};\n\n");

	VSB_printf(vsb, "/* ---===### Location Counters ###===---*/\n");

	VSB_printf(vsb, "\n#define VGC_NREFS %u\n", tl->cnt + 1);

	VSB_printf(vsb, "\nstatic struct vrt_ref VGC_ref[VGC_NREFS] = {\n");
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
		VSB_printf(vsb, "  [%3u] = { %u, %8tu, %4u, %3u, ",
		    t->cnt, sp->idx, t->b - sp->b, lin, pos + 1);
		if (t->tok == CSRC)
			VSB_printf(vsb, " \"C{\"},\n");
		else
			VSB_printf(vsb, " \"%.*s\" },\n", PF(t));
	}
	VSB_printf(vsb, "};\n\n");
}

/*--------------------------------------------------------------------
 * Init/Fini/Event
 *
 * We call DISCARD and COLD events in the opposite order of LOAD and
 * WARM. The child will panic if a USE event fails, since a WARM event
 * leads to a usable state.
 */

static void
EmitInitFini(const struct vcc *tl)
{
	struct inifin *p, *q = NULL;
	unsigned has_event = 0;

	Fh(tl, 0, "\n");
	Fh(tl, 0, "static unsigned vgc_inistep;\n");
	Fh(tl, 0, "static unsigned vgc_warmupstep;\n");

	/*
	 * LOAD
	 */
	Fc(tl, 0, "\nstatic int\nVGC_Load(VRT_CTX)\n{\n\n");
	Fc(tl, 0, "\tvgc_inistep = 0;\n\n");
	VTAILQ_FOREACH(p, &tl->inifin, list) {
		AZ(VSB_finish(p->ini));
		assert(p->n > 0);
		if (VSB_len(p->ini))
			Fc(tl, 0, "\t/* %u */\n%s\n", p->n, VSB_data(p->ini));
		Fc(tl, 0, "\tvgc_inistep = %u;\n\n", p->n);
		VSB_delete(p->ini);

		AZ(VSB_finish(p->event));
		if (VSB_len(p->event))
			has_event = 1;
	}

	Fc(tl, 0, "\t(void)VGC_function_vcl_init(ctx);\n");
	Fc(tl, 0, "\treturn(*ctx->handling == VCL_RET_OK ? 0: -1);\n");
	Fc(tl, 0, "}\n");

	/*
	 * DISCARD
	 */
	Fc(tl, 0, "\nstatic int\nVGC_Discard(VRT_CTX)\n{\n\n");

	Fc(tl, 0, "\t(void)VGC_function_vcl_fini(ctx);\n\n");
	VTAILQ_FOREACH_REVERSE(p, &tl->inifin, inifinhead, list) {
		AZ(VSB_finish(p->fin));
		if (q)
			assert(q->n > p->n);
		q = p;
		if (VSB_len(p->fin)) {
			Fc(tl, 0, "\t/* %u */\n", p->n);
			Fc(tl, 0, "\tif (vgc_inistep >= %u) {\n", p->n);
			Fc(tl, 0, "%s\n", VSB_data(p->fin));
			Fc(tl, 0, "\t}\n\n");
		}
		VSB_delete(p->fin);
	}

	Fc(tl, 0, "\treturn(0);\n");
	Fc(tl, 0, "}\n");

	if (has_event) {
		/*
		 * WARM
		 */
		Fc(tl, 0, "\nstatic int\n");
		Fc(tl, 0, "VGC_Warmup(VRT_CTX, enum vcl_event_e ev)\n{\n\n");

		Fc(tl, 0, "\tvgc_warmupstep = 0;\n\n");
		VTAILQ_FOREACH(p, &tl->inifin, list) {
			assert(p->n > 0);
			if (VSB_len(p->event)) {
				Fc(tl, 0, "\t/* %u */\n", p->n);
				Fc(tl, 0, "\tif (%s)\n", VSB_data(p->event));
				Fc(tl, 0, "\t\treturn (1);\n");
				Fc(tl, 0, "\tvgc_warmupstep = %u;\n\n", p->n);
			}
		}

		Fc(tl, 0, "\treturn (0);\n");
		Fc(tl, 0, "}\n");

		/*
		 * USE (deprecated)
		 */
		Fc(tl, 0, "\nstatic int\n");
		Fc(tl, 0, "VGC_Use(VRT_CTX, enum vcl_event_e ev)\n{\n\n");

		VTAILQ_FOREACH(p, &tl->inifin, list) {
			assert(p->n > 0);
			if (VSB_len(p->event)) {
				Fc(tl, 0, "\t/* %u */\n", p->n);
				Fc(tl, 0, "\tif (%s)\n", VSB_data(p->event));
				Fc(tl, 0, "\t\treturn (1);\n\n");
			}
		}

		Fc(tl, 0, "\treturn (0);\n");
		Fc(tl, 0, "}\n");

		/*
		 * COLD
		 */
		Fc(tl, 0, "\nstatic int\n");
		Fc(tl, 0, "VGC_Cooldown(VRT_CTX, enum vcl_event_e ev)\n{\n");
		Fc(tl, 0, "\tint retval = 0;\n\n");

		VTAILQ_FOREACH_REVERSE(p, &tl->inifin, inifinhead, list) {
			if (VSB_len(p->event)) {
				Fc(tl, 0, "\t/* %u */\n", p->n);
				Fc(tl, 0, "\tif (vgc_warmupstep >= %u &&\n", p->n);
				Fc(tl, 0, "\t    %s != 0)\n", VSB_data(p->event));
				Fc(tl, 0, "\t\tretval = 1;\n\n");
			}
			VSB_delete(p->event);
		}

		Fc(tl, 0, "\treturn (retval);\n");
		Fc(tl, 0, "}\n");
	}

	/*
	 * EVENTS
	 */
	Fc(tl, 0, "\nstatic int\n");
	Fc(tl, 0, "VGC_Event(VRT_CTX, enum vcl_event_e ev)\n");
	Fc(tl, 0, "{\n");
	Fc(tl, 0, "\tif (ev == VCL_EVENT_LOAD)\n");
	Fc(tl, 0, "\t\treturn(VGC_Load(ctx));\n");
	if (has_event) {
		Fc(tl, 0, "\tif (ev == VCL_EVENT_WARM)\n");
		Fc(tl, 0, "\t\treturn(VGC_Warmup(ctx, ev));\n");
		Fc(tl, 0, "\tif (ev == VCL_EVENT_USE)\n");
		Fc(tl, 0, "\t\treturn(VGC_Use(ctx, ev));\n");
		Fc(tl, 0, "\tif (ev == VCL_EVENT_COLD)\n");
		Fc(tl, 0, "\t\treturn(VGC_Cooldown(ctx, ev));\n");
	}
	Fc(tl, 0, "\tif (ev == VCL_EVENT_DISCARD)\n");
	Fc(tl, 0, "\t\treturn(VGC_Discard(ctx));\n");
	Fc(tl, 0, "\n");
	if (!has_event)
		Fc(tl, 0, "\t(void)vgc_warmupstep;\n");
	Fc(tl, 0, "\treturn (%d);\n", has_event ? 1 : 0);
	Fc(tl, 0, "}\n");
}

/*--------------------------------------------------------------------*/

static void
EmitStruct(const struct vcc *tl)
{
	Fc(tl, 0, "\nconst struct VCL_conf VCL_conf = {\n");
	Fc(tl, 0, "\t.magic = VCL_CONF_MAGIC,\n");
	Fc(tl, 0, "\t.event_vcl = VGC_Event,\n");
	Fc(tl, 0, "\t.default_director = &%s,\n", tl->default_director);
	if (tl->default_probe != NULL)
		Fc(tl, 0, "\t.default_probe = &%s,\n", tl->default_probe);
	Fc(tl, 0, "\t.ref = VGC_ref,\n");
	Fc(tl, 0, "\t.nref = VGC_NREFS,\n");
	Fc(tl, 0, "\t.nsrc = VGC_NSRCS,\n");
	Fc(tl, 0, "\t.srcname = srcname,\n");
	Fc(tl, 0, "\t.srcbody = srcbody,\n");
#define VCL_MET_MAC(l,u,t,b) \
	Fc(tl, 0, "\t." #l "_func = VGC_function_vcl_" #l ",\n");
#include "tbl/vcl_returns.h"
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
vcc_file_source(const struct vcp * const vcp, struct vsb *sb, const char *fn)
{
	char *f, *fnp;
	struct source *sp;

	if (!vcp->unsafe_path && strchr(fn, '/') != NULL) {
		VSB_printf(sb, "VCL filename '%s' is unsafe.\n", fn);
		return (NULL);
	}
	f = NULL;
	if (VFIL_searchpath(vcp->vcl_path, NULL, &f, fn, &fnp) || f == NULL) {
		VSB_printf(sb, "Cannot read file '%s' (%s)\n",
		    fnp != NULL ? fnp : fn, strerror(errno));
		free(fnp);
		return (NULL);
	}
	sp = vcc_new_source(f, NULL, fnp);
	free(fnp);
	sp->freeit = f;
	return (sp);
}

/*--------------------------------------------------------------------*/

static void
vcc_resolve_includes(struct vcc *tl)
{
	struct token *t, *t1, *t2;
	struct source *sp;
	struct vsb *vsb;
	const char *p;

	VTAILQ_FOREACH(t, &tl->tokens, list) {
		if (t->tok != ID || !vcc_IdIs(t, "include"))
			continue;

		t1 = VTAILQ_NEXT(t, list);
		assert(t1 != NULL);	/* There's always an EOI */
		if (t1->tok != CSTR) {
			VSB_printf(tl->sb,
			    "include not followed by string constant.\n");
			vcc_ErrWhere(tl, t1);
			return;
		}
		t2 = VTAILQ_NEXT(t1, list);
		assert(t2 != NULL);	/* There's always an EOI */

		if (t2->tok != ';') {
			VSB_printf(tl->sb,
			    "include <string> not followed by semicolon.\n");
			vcc_ErrWhere(tl, t1);
			return;
		}

		if (t1->dec[0] == '.' && t1->dec[1] == '/') {
			/*
			 * Nested include filenames, starting with "./" are
			 * resolved relative to the VCL file which contains
			 * the include directive.
			 */
			if (t1->src->name[0] != '/') {
				VSB_printf(tl->sb,
				    "include \"./xxxxx\"; needs absolute "
				    "filename of including file.\n");
				vcc_ErrWhere(tl, t1);
				return;
			}
			vsb = VSB_new_auto();
			AN(vsb);
			p = strrchr(t1->src->name, '/');
			AN(p);
			VSB_bcat(vsb, t1->src->name, p - t1->src->name);
			VSB_cat(vsb, t1->dec + 1);
			AZ(VSB_finish(vsb));
			sp = vcc_file_source(tl->param, tl->sb, VSB_data(vsb));
			VSB_delete(vsb);
		} else {
			sp = vcc_file_source(tl->param, tl->sb, t1->dec);
		}
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
vcc_NewVcc(const struct vcp *vcp)
{
	struct vcc *tl;
	int i;

	CHECK_OBJ_NOTNULL(vcp, VCP_MAGIC);
	ALLOC_OBJ(tl, VCC_MAGIC);
	AN(tl);
	tl->param = vcp;
	tl->vars = vcc_vars;
	VTAILQ_INIT(&tl->symbols);
	VTAILQ_INIT(&tl->inifin);
	VTAILQ_INIT(&tl->membits);
	VTAILQ_INIT(&tl->tokens);
	VTAILQ_INIT(&tl->sources);

	tl->nsources = 0;

	/* General C code */
	tl->fc = VSB_new_auto();
	assert(tl->fc != NULL);

	/* Forward decls (.h like) */
	tl->fh = VSB_new_auto();
	assert(tl->fh != NULL);

	/* body code of methods */
	for (i = 0; i < VCL_MET_MAX; i++) {
		tl->fm[i] = VSB_new_auto();
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

	VSB_delete(tl->fh);
	VSB_delete(tl->fc);
	for (i = 0; i < VCL_MET_MAX; i++)
		VSB_delete(tl->fm[i]);

	free(tl);
	return (ret);
}

/*--------------------------------------------------------------------
 * Compile the VCL code from the given source and return the C-source
 */

static char *
vcc_CompileSource(const struct vcp * const vcp, struct vsb *sb,
    struct source *sp)
{
	struct vcc *tl;
	struct symbol *sym;
	const struct var *v;
	struct vsb *vsb;

	char *of;
	int i;

	tl = vcc_NewVcc(vcp);
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

	sym = VCC_AddSymbolStr(tl, "storage.", SYM_WILDCARD);
	sym->wildcard = vcc_Stv_Wildcard;

	Fh(tl, 0, "/* ---===### VCC generated .h code ###===---*/\n");
	Fc(tl, 0, "\n/* ---===### VCC generated .c code ###===---*/\n");
	Fh(tl, 0, "\nextern const struct VCL_conf VCL_conf;\n");

	/* Register and lex the main source */
	VTAILQ_INSERT_TAIL(&tl->sources, sp, list);
	sp->idx = tl->nsources++;
	vcc_Lexer(tl, sp);
	if (tl->err)
		return (vcc_DestroyTokenList(tl, NULL));

	/* Register and lex the builtin VCL */
	sp = vcc_new_source(tl->param->builtin_vcl, NULL, "Builtin");
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
	if (tl->default_director == NULL) {
		VSB_printf(tl->sb,
		    "No backends or directors found in VCL program, "
		    "at least one is necessary.\n");
		tl->err = 1;
		return (vcc_DestroyTokenList(tl, NULL));
	}

	/* Configure the default director */
	vcc_AddRef(tl, tl->t_default_director, SYM_BACKEND);

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
	Fh(tl, 1, "\n");
	for (i = 1; i < VCL_MET_MAX; i++) {
		Fh(tl, 1,
		    "int __match_proto__(vcl_func_f) "
		    "VGC_function_%s(VRT_CTX);\n",
		    method_tab[i].name);
		Fc(tl, 1, "\nint __match_proto__(vcl_func_f)\n");
		Fc(tl, 1,
		    "VGC_function_%s(VRT_CTX)\n",
		    method_tab[i].name);
		AZ(VSB_finish(tl->fm[i]));
		Fc(tl, 1, "{\n");
		/*
		 * We want vmods to be able set a FAIL return value
		 * in members called from vcl_init, so set OK up front
		 * and return with whatever was set last.
		 */
		if (method_tab[i].bitval == VCL_MET_INIT)
			Fc(tl, 1, "  VRT_handling(ctx, VCL_RET_OK);\n");
		Fc(tl, 1, "%s", VSB_data(tl->fm[i]));
		if (method_tab[i].bitval == VCL_MET_INIT)
			Fc(tl, 1, "  return(1);\n");
		Fc(tl, 1, "}\n");
	}

	EmitInitFini(tl);

	EmitStruct(tl);

	/* Combine it all */

	vsb = VSB_new_auto();
	AN(vsb);

	vcl_output_lang_h(vsb);

	EmitCoordinates(tl, vsb);

	AZ(VSB_finish(tl->fh));
	VSB_cat(vsb, VSB_data(tl->fh));

	AZ(VSB_finish(tl->fc));
	VSB_cat(vsb, VSB_data(tl->fc));

	AZ(VSB_finish(vsb));

	of = strdup(VSB_data(vsb));
	AN(of);

	VSB_delete(vsb);

	/* done */
	return (vcc_DestroyTokenList(tl, of));
}

/*--------------------------------------------------------------------
 * Compile the VCL code in the argument.  Error messages, if any are
 * formatted into the vsb.
 */

char *
VCC_Compile(const struct vcp *vcp, struct vsb *sb,
    const char *vclsrc, const char *vclsrcfile)
{
	struct source *sp;
	char *r;
	CHECK_OBJ_NOTNULL(vcp, VCP_MAGIC);
	AN(sb);
	AN(vclsrcfile);

	if (vclsrc != NULL)
		sp = vcc_new_source(vclsrc, NULL, vclsrcfile);
	else
		sp = vcc_file_source(vcp, sb, vclsrcfile);
	if (sp == NULL)
		return (NULL);
	r = vcc_CompileSource(vcp, sb, sp);
	return (r);
}

/*--------------------------------------------------------------------
 * Allocate a compiler instance
 */

struct vcp *
VCP_New(void)
{
	struct vcp *vcp;

	ALLOC_OBJ(vcp, VCP_MAGIC);
	AN(vcp);

	return (vcp);
}

/*--------------------------------------------------------------------
 * Configure builtin VCL source code
 */

void
VCP_Builtin_VCL(struct vcp *vcp, const char *str)
{

	CHECK_OBJ_NOTNULL(vcp, VCP_MAGIC);
	REPLACE(vcp->builtin_vcl, str);
}

/*--------------------------------------------------------------------
 * Configure default VCL source directory
 */

void
VCP_VCL_dir(struct vcp *vcp, const char *str)
{

	CHECK_OBJ_NOTNULL(vcp, VCP_MAGIC);
	REPLACE(vcp->vcl_dir, str);
	VFIL_setpath(&vcp->vcl_path, str);
}

/*--------------------------------------------------------------------
 * Configure default VMOD directory
 */

void
VCP_VMOD_dir(struct vcp *vcp, const char *str)
{

	CHECK_OBJ_NOTNULL(vcp, VCP_MAGIC);
	REPLACE(vcp->vmod_dir, str);
	VFIL_setpath(&vcp->vmod_path, str);
}

/*--------------------------------------------------------------------
 * Configure settings
 */

void
VCP_Err_Unref(struct vcp *vcp, unsigned u)
{

	CHECK_OBJ_NOTNULL(vcp, VCP_MAGIC);
	vcp->err_unref = u;
}

void
VCP_Allow_InlineC(struct vcp *vcp, unsigned u)
{

	CHECK_OBJ_NOTNULL(vcp, VCP_MAGIC);
	vcp->allow_inline_c = u;
}

void
VCP_Unsafe_Path(struct vcp *vcp, unsigned u)
{

	CHECK_OBJ_NOTNULL(vcp, VCP_MAGIC);
	vcp->unsafe_path = u;
}
