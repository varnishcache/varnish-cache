/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 *
 * XXX: add VRT_count()'s
 */

#include "config.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vcc_compile.h"

/*--------------------------------------------------------------------
 * Recognize and convert units of time, return seconds.
 */

static double
vcc_TimeUnit(struct vcc *tl)
{
	double sc = 1.0;

	assert(tl->t->tok == ID);
	if (vcc_IdIs(tl->t, "ms"))
		sc = 1e-3;
	else if (vcc_IdIs(tl->t, "s"))
		sc = 1.0;
	else if (vcc_IdIs(tl->t, "m"))
		sc = 60.0;
	else if (vcc_IdIs(tl->t, "h"))
		sc = 60.0 * 60.0;
	else if (vcc_IdIs(tl->t, "d"))
		sc = 60.0 * 60.0 * 24.0;
	else if (vcc_IdIs(tl->t, "w"))
		sc = 60.0 * 60.0 * 24.0 * 7.0;
	else if (vcc_IdIs(tl->t, "y"))
		sc = 60.0 * 60.0 * 24.0 * 365.0;
	else {
		VSB_printf(tl->sb, "Unknown time unit ");
		vcc_ErrToken(tl, tl->t);
		VSB_printf(tl->sb,
		    ".  Legal are 'ms', 's', 'm', 'h', 'd', 'w' and 'y'\n");
		vcc_ErrWhere(tl, tl->t);
		return (1.0);
	}
	vcc_NextToken(tl);
	return (sc);
}

/*--------------------------------------------------------------------
 * Recognize and convert { CNUM } to unsigned value
 * The tokenizer made sure we only get digits.
 */

unsigned
vcc_UintVal(struct vcc *tl)
{
	unsigned d = 0;
	const char *p;

	Expect(tl, CNUM);
	for (p = tl->t->b; p < tl->t->e; p++) {
		d *= 10;
		d += *p - '0';
	}
	vcc_NextToken(tl);
	return (d);
}

/*--------------------------------------------------------------------
 * Recognize and convert { CNUM [ '.' [ CNUM ] ] } to double value
 * The tokenizer made sure we only get digits and a '.'
 */

static void
vcc_NumVal(struct vcc *tl, double *d, int *frac)
{
	double e = 0.1;
	const char *p;

	*frac = 0;
	*d = 0.0;
	Expect(tl, CNUM);
	if (tl->err) {
		*d = NAN;
		return;
	}
	for (p = tl->t->b; p < tl->t->e; p++) {
		*d *= 10;
		*d += *p - '0';
	}
	vcc_NextToken(tl);
	if (tl->t->tok != '.')
		return;
	*frac = 1;
	vcc_NextToken(tl);
	if (tl->t->tok != CNUM)
		return;
	for (p = tl->t->b; p < tl->t->e; p++) {
		*d += (*p - '0') * e;
		e *= 0.1;
	}
	vcc_NextToken(tl);
}

double
vcc_DoubleVal(struct vcc *tl)
{
	double d;
	int i;

	vcc_NumVal(tl, &d, &i);
	return (d);
}

/*--------------------------------------------------------------------*/

void
vcc_Duration(struct vcc *tl, double *d)
{
	double v, sc;

	v = vcc_DoubleVal(tl);
	ERRCHK(tl);
	ExpectErr(tl, ID);
	sc = vcc_TimeUnit(tl);
	*d = v * sc;
}

/*--------------------------------------------------------------------*/

static void
vcc_ByteVal(struct vcc *tl, double *d)
{
	double v, sc;

	v = vcc_DoubleVal(tl);
	ERRCHK(tl);
	if (tl->t->tok != ID) {
		VSB_printf(tl->sb, "Expected BYTES unit (B, KB, MB...) got ");
		vcc_ErrToken(tl, tl->t);
		VSB_printf(tl->sb, "\n");
		vcc_ErrWhere(tl, tl->t);
		return;
	}
	if (vcc_IdIs(tl->t, "B"))
		sc = 1.;
	else if (vcc_IdIs(tl->t, "KB"))
		sc = 1024.;
	else if (vcc_IdIs(tl->t, "MB"))
		sc = 1024. * 1024.;
	else if (vcc_IdIs(tl->t, "GB"))
		sc = 1024. * 1024. * 1024.;
	else if (vcc_IdIs(tl->t, "TB"))
		sc = 1024. * 1024. * 1024. * 1024.;
	else {
		VSB_printf(tl->sb, "Unknown BYTES unit ");
		vcc_ErrToken(tl, tl->t);
		VSB_printf(tl->sb,
		    ".  Legal are 'B', 'KB', 'MB', 'GB' and 'TB'\n");
		vcc_ErrWhere(tl, tl->t);
		return;
	}
	vcc_NextToken(tl);
	*d = v * sc;
}

/*--------------------------------------------------------------------
 * Facility for carrying expressions around and do text-processing on
 * them.
 */

struct expr {
	unsigned	magic;
#define EXPR_MAGIC	0x38c794ab
	vcc_type_t	fmt;
	struct vsb	*vsb;
	uint8_t		constant;
#define EXPR_VAR	(1<<0)
#define EXPR_CONST	(1<<1)
#define EXPR_STR_CONST	(1<<2)
	struct token	*t1, *t2;
};

static inline int
vcc_isconst(const struct expr *e)
{
	AN(e->constant);
	return (e->constant & EXPR_CONST);
}

static void vcc_expr0(struct vcc *tl, struct expr **e, vcc_type_t fmt);

static struct expr *
vcc_new_expr(void)
{
	struct expr *e;

	/* XXX: use TlAlloc() ? */
	ALLOC_OBJ(e, EXPR_MAGIC);
	AN(e);
	e->vsb = VSB_new_auto();
	e->fmt = VOID;
	e->constant = EXPR_VAR;
	return (e);
}

static struct expr *
vcc_mk_expr(vcc_type_t fmt, const char *str, ...)
    __v_printflike(2, 3);

static struct expr *
vcc_mk_expr(vcc_type_t fmt, const char *str, ...)
{
	va_list ap;
	struct expr *e;

	e = vcc_new_expr();
	e->fmt = fmt;
	va_start(ap, str);
	VSB_vprintf(e->vsb, str, ap);
	va_end(ap);
	AZ(VSB_finish(e->vsb));
	return (e);
}

static void
vcc_delete_expr(struct expr *e)
{
	if (e == NULL)
		return;
	CHECK_OBJ_NOTNULL(e, EXPR_MAGIC);
	VSB_destroy(&e->vsb);
	FREE_OBJ(e);
}

/*--------------------------------------------------------------------
 * We want to get the indentation right in the emitted C code so we have
 * to represent it symbolically until we are ready to render.
 *
 * Many of the operations have very schematic output syntaxes, so we
 * use the same facility to simplify the text-processing of emitting
 * a given operation on two subexpressions.
 *
 * We use '\v' as the magic escape character.
 *	\v1  insert subexpression 1
 *	\v2  insert subexpression 2
 *	\v+  increase indentation
 *	\v-  decrease indentation
 *	anything else is literal
 *
 * When editing, we check if any of the subexpressions contain a newline
 * and issue it as an indented block of so.
 *
 * XXX: check line lengths in edit, should pass indent in for this
 */

static struct expr *
vcc_expr_edit(vcc_type_t fmt, const char *p, struct expr *e1,
    struct expr *e2)
{
	struct expr *e;
	int nl = 1;

	AN(e1);
	e = vcc_new_expr();
	while (*p != '\0') {
		if (*p != '\v') {
			if (*p != '\n' || !nl)
				VSB_putc(e->vsb, *p);
			nl = (*p == '\n');
			p++;
			continue;
		}
		assert(*p == '\v');
		switch (*++p) {
		case '+': VSB_cat(e->vsb, "\v+"); break;
		case '-': VSB_cat(e->vsb, "\v-"); break;
		case '1':
			VSB_cat(e->vsb, VSB_data(e1->vsb));
			break;
		case '2':
			AN(e2);
			VSB_cat(e->vsb, VSB_data(e2->vsb));
			break;
		default:
			WRONG("Illegal edit in VCC expression");
		}
		p++;
	}
	AZ(VSB_finish(e->vsb));
	e->t1 = e1->t1;
	e->t2 = e1->t2;
	if (e2 != NULL)
		e->t2 = e2->t2;
	vcc_delete_expr(e1);
	vcc_delete_expr(e2);
	e->fmt = fmt;
	return (e);
}

/*--------------------------------------------------------------------
 * Expand finished expression into C-source code
 */

static void
vcc_expr_fmt(struct vsb *d, int ind, const struct expr *e1)
{
	char *p;
	int i;

	for (i = 0; i < ind; i++)
		VSB_cat(d, " ");
	p = VSB_data(e1->vsb);
	while (*p != '\0') {
		if (*p == '\n') {
			VSB_putc(d, '\n');
			if (p[1] != '\0') {
				for (i = 0; i < ind; i++)
					VSB_cat(d, " ");
			}
			p++;
			continue;
		}
		if (*p != '\v') {
			VSB_putc(d, *p);
			p++;
			continue;
		}
		p++;
		switch (*p) {
		case '+': ind += 2; break;
		case '-': ind -= 2; break;
		default:
			WRONG("Illegal format in VCC expression");
		}
		p++;
	}
}

/*--------------------------------------------------------------------
 */

static void
vcc_expr_tostring(struct vcc *tl, struct expr **e, vcc_type_t fmt)
{
	const char *p;
	uint8_t	constant = EXPR_VAR;

	CHECK_OBJ_NOTNULL(*e, EXPR_MAGIC);
	assert(fmt == STRING || fmt == STRING_LIST);
	assert(fmt != (*e)->fmt);

	if ((*e)->fmt == STRING ||
	    ((*e)->fmt == STRING_LIST && vcc_isconst(*e))) {
		(*e)->fmt = fmt;
		return;
	}
	p = (*e)->fmt->tostring;
	if (p == NULL && (*e)->fmt == BLOB) {
		VSB_printf(tl->sb,
		    "Wrong use of BLOB value.\n"
		    "BLOBs can only be used as arguments to VMOD"
		    " functions.\n");
		vcc_ErrWhere2(tl, (*e)->t1, tl->t);
		return;
	}
	if (p == NULL) {
		VSB_printf(tl->sb,
		    "Cannot convert %s to STRING.\n", (*e)->fmt->name);
		vcc_ErrWhere2(tl, (*e)->t1, tl->t);
		return;
	}
	AN(p);
	if (*p != '\0') {
		*e = vcc_expr_edit(fmt, p, *e, NULL);
		(*e)->constant = constant;
	}
}

/*--------------------------------------------------------------------
 */

static void __match_proto__(sym_expr_t)
vcc_Eval_Regsub(struct vcc *tl, struct expr **e, const struct symbol *sym,
    vcc_type_t fmt)
{
	struct expr *e2;
	int all = sym->eval_priv == NULL ? 0 : 1;
	const char *p;
	char buf[128];

	(void)fmt;
	vcc_delete_expr(*e);
	SkipToken(tl, ID);
	SkipToken(tl, '(');

	vcc_expr0(tl, &e2, STRING);
	if (e2 == NULL)
		return;

	SkipToken(tl, ',');
	ExpectErr(tl, CSTR);
	p = vcc_regexp(tl);
	vcc_NextToken(tl);

	bprintf(buf, "VRT_regsub(ctx, %d,\v+\n\v1,\n%s", all, p);
	*e = vcc_expr_edit(STRING, buf, e2, NULL);

	SkipToken(tl, ',');
	vcc_expr0(tl, &e2, STRING);
	if (e2 == NULL)
		return;
	*e = vcc_expr_edit(STRING, "\v1,\n\v2)\v-", *e, e2);
	SkipToken(tl, ')');
}

/*--------------------------------------------------------------------
 */

static void __match_proto__(sym_expr_t)
vcc_Eval_BoolConst(struct vcc *tl, struct expr **e, const struct symbol *sym,
    vcc_type_t fmt)
{

	(void)fmt;
	vcc_NextToken(tl);
	*e = vcc_mk_expr(BOOL, "(0==%d)", sym->eval_priv == NULL ? 1 : 0);
	(*e)->constant = EXPR_CONST;
}

/*--------------------------------------------------------------------
 */

void __match_proto__(sym_expr_t)
vcc_Eval_Handle(struct vcc *tl, struct expr **e, const struct symbol *sym,
    vcc_type_t fmt)
{

	AN(sym->rname);

	if (sym->fmt != STRING && (fmt == STRING || fmt == STRING_LIST)) {
		*e = vcc_mk_expr(STRING, "\"%s\"", sym->name);
		vcc_AddRef(tl, tl->t, sym->kind);
	} else {
		vcc_ExpectCid(tl, "handle");
		vcc_AddRef(tl, tl->t, sym->kind);
		*e = vcc_mk_expr(sym->fmt, "%s", sym->rname);
		(*e)->constant = EXPR_VAR;	/* XXX ? */
	}
	vcc_NextToken(tl);
}

/*--------------------------------------------------------------------
 */

void __match_proto__(sym_expr_t)
vcc_Eval_Var(struct vcc *tl, struct expr **e, const struct symbol *sym,
    vcc_type_t fmt)
{

	(void)fmt;
	assert(sym->kind == SYM_VAR);
	vcc_AddUses(tl, tl->t, sym->r_methods, "Not available");
	ERRCHK(tl);
	*e = vcc_mk_expr(sym->fmt, "%s", sym->rname);
	vcc_NextToken(tl);
}

/*--------------------------------------------------------------------
 */

static struct expr *
vcc_priv_arg(struct vcc *tl, const char *p, const char *name, const char *vmod)
{
	struct expr *e2;
	char buf[32];
	struct inifin *ifp;

	(void)name;
	if (!strcmp(p, "PRIV_VCL")) {
		e2 = vcc_mk_expr(VOID, "&vmod_priv_%s", vmod);
	} else if (!strcmp(p, "PRIV_CALL")) {
		bprintf(buf, "vmod_priv_%u", tl->unique++);
		ifp = New_IniFin(tl);
		Fh(tl, 0, "static struct vmod_priv %s;\n", buf);
		VSB_printf(ifp->fin, "\tVRT_priv_fini(&%s);", buf);
		e2 = vcc_mk_expr(VOID, "&%s", buf);
	} else if (!strcmp(p, "PRIV_TASK")) {
		e2 = vcc_mk_expr(VOID,
		    "VRT_priv_task(ctx, &VGC_vmod_%s)", vmod);
	} else if (!strcmp(p, "PRIV_TOP")) {
		e2 = vcc_mk_expr(VOID,
		    "VRT_priv_top(ctx, &VGC_vmod_%s)", vmod);
	} else {
		WRONG("Wrong PRIV_ type");
	}
	return (e2);
}

struct func_arg {
	vcc_type_t		type;
	const char		*enum_bits;
	const char		*name;
	const char		*val;
	struct expr		*result;
	VTAILQ_ENTRY(func_arg)	list;
};

static void
vcc_do_arg(struct vcc *tl, struct func_arg *fa)
{
	const char *p, *r;
	struct expr *e2;

	if (fa->type == ENUM) {
		ExpectErr(tl, ID);
		ERRCHK(tl);
		r = p = fa->enum_bits;
		do {
			if (vcc_IdIs(tl->t, p))
				break;
			p += strlen(p) + 1;
		} while (*p != '\1');
		if (*p == '\1') {
			VSB_printf(tl->sb, "Wrong enum value.");
			VSB_printf(tl->sb, "  Expected one of:\n");
			do {
				VSB_printf(tl->sb, "\t%s\n", r);
				r += strlen(r) + 1;
			} while (*r != '\0' && *r != '\1');
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		fa->result = vcc_mk_expr(VOID, "\"%.*s\"", PF(tl->t));
		SkipToken(tl, ID);
	} else {
		vcc_expr0(tl, &e2, fa->type);
		ERRCHK(tl);
		if (e2->fmt != fa->type) {
			VSB_printf(tl->sb, "Wrong argument type.");
			VSB_printf(tl->sb, "  Expected %s.",
				fa->type->name);
			VSB_printf(tl->sb, "  Got %s.\n",
				e2->fmt->name);
			vcc_ErrWhere2(tl, e2->t1, tl->t);
			return;
		}
		assert(e2->fmt == fa->type);
		if (e2->fmt == STRING_LIST) {
			e2 = vcc_expr_edit(STRING_LIST,
			    "\v+\n\v1,\nvrt_magic_string_end\v-",
			    e2, NULL);
		}
		fa->result = e2;
	}
}

static void
vcc_func(struct vcc *tl, struct expr **e, const char *spec,
    const char *extra, const struct symbol *sym)
{
	vcc_type_t rfmt;
	const char *args;
	const char *cfunc;
	const char *p;
	struct expr *e1;
	struct func_arg *fa, *fa2;
	VTAILQ_HEAD(,func_arg) head;
	struct token *t1;

	rfmt = VCC_Type(spec);
	spec += strlen(spec) + 1;
	cfunc = spec;
	spec += strlen(spec) + 1;
	args = spec;
	SkipToken(tl, '(');
	p = args;
	if (extra == NULL)
		extra = "";
	AN(rfmt);
	VTAILQ_INIT(&head);
	while (*p != '\0') {
		fa = calloc(sizeof *fa, 1);
		AN(fa);
		VTAILQ_INSERT_TAIL(&head, fa, list);
		if (!memcmp(p, "PRIV_", 5)) {
			fa->result = vcc_priv_arg(tl, p, sym->name, sym->vmod);
			fa->name = "";
			p += strlen(p) + 1;
			continue;
		}
		fa->type = VCC_Type(p);
		AN(fa->type);
		p += strlen(p) + 1;
		if (*p == '\1') {
			fa->enum_bits = ++p;
			while (*p != '\1')
				p += strlen(p) + 1;
			p++;
			assert(*p == '\0');
			p++;
		}
		if (*p == '\2') {
			fa->name = p + 1;
			p += strlen(p) + 1;
		}
		if (*p == '\3') {
			fa->val = p + 1;
			p += strlen(p) + 1;
		}
		assert(*p == 0 || *p > ' ');
	}

	VTAILQ_FOREACH(fa, &head, list) {
		if (tl->t->tok == ')')
			break;
		if (fa->result != NULL)
			continue;
		if (tl->t->tok == ID) {
			t1 = VTAILQ_NEXT(tl->t, list);
			if (t1->tok == '=')
				break;
		}
		vcc_do_arg(tl, fa);
		ERRCHK(tl);
		if (tl->t->tok == ')')
			break;
		SkipToken(tl, ',');
	}
	while (tl->t->tok == ID) {
		VTAILQ_FOREACH(fa, &head, list) {
			if (fa->name == NULL)
				continue;
			if (vcc_IdIs(tl->t, fa->name))
				break;
		}
		if (fa == NULL) {
			VSB_printf(tl->sb, "Unknown argument '%.*s'\n",
			    PF(tl->t));
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		if (fa->result != NULL) {
			VSB_printf(tl->sb, "Argument '%s' already used\n",
			    fa->name);
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		vcc_NextToken(tl);
		SkipToken(tl, '=');
		vcc_do_arg(tl, fa);
		ERRCHK(tl);
		if (tl->t->tok == ')')
			break;
		SkipToken(tl, ',');
	}

	e1 = vcc_mk_expr(rfmt, "%s(ctx%s\v+", cfunc, extra);
	VTAILQ_FOREACH_SAFE(fa, &head, list, fa2) {
		if (fa->result == NULL && fa->val != NULL)
			fa->result = vcc_mk_expr(fa->type, "%s", fa->val);
		if (fa->result != NULL)
			e1 = vcc_expr_edit(e1->fmt, "\v1,\n\v2",
			    e1, fa->result);
		else {
			VSB_printf(tl->sb, "Argument '%s' missing\n",
			    fa->name);
			vcc_ErrWhere(tl, tl->t);
		}
		free(fa);
	}
	e1 = vcc_expr_edit(e1->fmt, "\v1\n)\v-", e1, NULL);
	*e = e1;

	SkipToken(tl, ')');

}

/*--------------------------------------------------------------------
 */

void
vcc_Eval_Func(struct vcc *tl, const char *spec,
    const char *extra, const struct symbol *sym)
{
	struct expr *e = NULL;
	struct token *t1;

	t1 = tl->t;
	vcc_func(tl, &e, spec, extra, sym);
	if (!tl->err) {
		vcc_expr_fmt(tl->fb, tl->indent, e);
		VSB_cat(tl->fb, ";\n");
	} else if (t1 != tl->t) {
		vcc_ErrWhere2(tl, t1, tl->t);
	}
	vcc_delete_expr(e);
}

/*--------------------------------------------------------------------
 */

void __match_proto__(sym_expr_t)
vcc_Eval_SymFunc(struct vcc *tl, struct expr **e, const struct symbol *sym,
    vcc_type_t fmt)
{

	(void)fmt;
	assert(sym->kind == SYM_FUNC);
	AN(sym->eval_priv);

	SkipToken(tl, ID);
	assert(sym->fmt == VCC_Type(sym->eval_priv));
	vcc_func(tl, e, sym->eval_priv, sym->extra, sym);
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    Expr4:
 *	'(' Expr0 ')'
 *	symbol
 *	CNUM
 *	CSTR
 */

static void
vcc_expr4(struct vcc *tl, struct expr **e, vcc_type_t fmt)
{
	struct expr *e1, *e2;
	const char *ip, *sign;
	const struct symbol *sym;
	enum symkind kind;
	double d;
	int i;

	sign = "";
	*e = NULL;
	if (tl->t->tok == '(') {
		SkipToken(tl, '(');
		vcc_expr0(tl, &e2, fmt);
		ERRCHK(tl);
		SkipToken(tl, ')');
		*e = vcc_expr_edit(e2->fmt, "(\v1)", e2, NULL);
		return;
	}
	switch (tl->t->tok) {
	case ID:
		/*
		 * XXX: what if var and func/proc had same name ?
		 * XXX: look for SYM_VAR first for consistency ?
		 */
		sym = NULL;
		kind = VCC_HandleKind(fmt);
		if (kind != SYM_NONE)
			sym = VCC_SymbolTok(tl, NULL, tl->t, kind, 0);
		if (sym == NULL)
			sym = VCC_SymbolTok(tl, NULL, tl->t, SYM_NONE, 0);
		if (sym == NULL || sym->eval == NULL) {
			VSB_printf(tl->sb, "Symbol not found: ");
			vcc_ErrToken(tl, tl->t);
			VSB_printf(tl->sb, " (expected type %s):\n",
			    fmt->name);
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		AN(sym);
		if (sym->kind == SYM_FUNC && sym->fmt == VOID) {
			VSB_printf(tl->sb, "Function returns VOID:\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		switch (sym->kind) {
		case SYM_VAR:
		case SYM_FUNC:
		case SYM_ACL:
		case SYM_BACKEND:
		case SYM_STEVEDORE:
		case SYM_PROBE:
			AN(sym->eval);
			AZ(*e);
			sym->eval(tl, e, sym, fmt);
			ERRCHK(tl);
			/* Unless asked for a HEADER, fold to string here */
			if (*e && fmt != HEADER && (*e)->fmt == HEADER) {
				vcc_expr_tostring(tl, e, STRING);
				ERRCHK(tl);
			}
			return;
		default:
			break;
		}
		VSB_printf(tl->sb,
		    "Symbol type (%s) can not be used in expression.\n",
		    VCC_SymKind(tl, sym));
		vcc_ErrWhere(tl, tl->t);
		return;
	case CSTR:
		assert(fmt != VOID);
		if (fmt == IP) {
			Resolve_Sockaddr(tl, tl->t->dec, "80",
			    &ip, NULL, &ip, NULL, NULL, 1,
			    tl->t, "IP constant");
			ERRCHK(tl);
			e1 = vcc_mk_expr(IP, "%s", ip);
			ERRCHK(tl);
		} else {
			e1 = vcc_new_expr();
			EncToken(e1->vsb, tl->t);
			e1->fmt = STRING;
			AZ(VSB_finish(e1->vsb));
		}
		e1->t1 = tl->t;
		e1->constant = EXPR_CONST;
		vcc_NextToken(tl);
		*e = e1;
		return;
	case '-':
		if (fmt != INT && fmt != REAL && fmt != DURATION)
			break;
		vcc_NextToken(tl);
		ExpectErr(tl, CNUM);
		sign = "-";
		/* FALLTHROUGH */
	case CNUM:
		/*
		 * XXX: %g may not have enough decimals by default
		 * XXX: but %a is ugly, isn't it ?
		 */
		assert(fmt != VOID);
		if (fmt == BYTES) {
			vcc_ByteVal(tl, &d);
			ERRCHK(tl);
			e1 = vcc_mk_expr(BYTES, "%.1f", d);
		} else {
			vcc_NumVal(tl, &d, &i);
			ERRCHK(tl);
			if (tl->t->tok == ID) {
				e1 = vcc_mk_expr(DURATION, "%s%g",
				    sign, d * vcc_TimeUnit(tl));
				ERRCHK(tl);
			} else if (i || fmt == REAL)
				e1 = vcc_mk_expr(REAL, "%s%f",
				    sign, d);
			else
				e1 = vcc_mk_expr(INT, "%s%ld",
				    sign, (unsigned long)d);
		}
		e1->constant = EXPR_CONST;
		*e = e1;
		return;
	default:
		break;
	}
	VSB_printf(tl->sb, "Unknown token ");
	vcc_ErrToken(tl, tl->t);
	VSB_printf(tl->sb, " when looking for %s\n\n", fmt->name);
	vcc_ErrWhere(tl, tl->t);
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    Expr3:
 *      Expr4 { {'*'|'/'} Expr4 } *
 */

static void
vcc_expr_mul(struct vcc *tl, struct expr **e, vcc_type_t fmt)
{
	struct expr *e2;
	vcc_type_t f2, f3;
	struct token *tk;

	*e = NULL;
	vcc_expr4(tl, e, fmt);
	ERRCHK(tl);
	AN(*e);
	f3 = (*e)->fmt;

	f2 = f3->multype;
	if (f2 == NULL) {
		if (tl->t->tok != '*' && tl->t->tok != '/')
			return;
		VSB_printf(tl->sb, "Operator %.*s not possible on type %s.\n",
		    PF(tl->t), f3->name);
		vcc_ErrWhere(tl, tl->t);
		return;
	}

	while (tl->t->tok == '*' || tl->t->tok == '/') {
		tk = tl->t;
		vcc_NextToken(tl);
		vcc_expr4(tl, &e2, f2);
		ERRCHK(tl);
		if (e2->fmt != REAL && e2->fmt != INT) {
			VSB_printf(tl->sb,
			    "%s %.*s %s not possible.\n",
			    f2->name, PF(tk), e2->fmt->name);
			vcc_ErrWhere(tl, tk);
			return;
		}
		if (tk->tok == '*')
			*e = vcc_expr_edit(f3, "(\v1*\v2)", *e, e2);
		else
			*e = vcc_expr_edit(f3, "(\v1/\v2)", *e, e2);
	}
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    ExprAdd:
 *      ExprMul { {'+'|'-'} ExprMul } *
 *
 * For reasons of memory allocation/copying and general performance,
 * STRINGs in VCL are quite special.   Addition/concatenation is split
 * into it's own subfunction to encapsulate this.
 */

static void
vcc_expr_string_add(struct vcc *tl, struct expr **e, struct expr *e2)
{
	vcc_type_t f2;

	AN(e);
	AN(*e);
	AN(e2);
	f2 = (*e)->fmt;
	assert (f2 == STRING || f2 == STRING_LIST);

	while (e2 != NULL || tl->t->tok == '+') {
		if (e2 == NULL) {
			vcc_NextToken(tl);
			vcc_expr_mul(tl, &e2, STRING);
		}
		ERRCHK(tl);
		if (e2->fmt != STRING && e2->fmt != STRING_LIST)
			vcc_expr_tostring(tl, &e2, f2);
		ERRCHK(tl);
		assert(e2->fmt == STRING || e2->fmt == STRING_LIST);

		if (vcc_isconst(*e) && vcc_isconst(e2)) {
			assert((*e)->fmt == STRING);
			assert(e2->fmt == STRING);
			*e = vcc_expr_edit(STRING, "\v1\n\v2", *e, e2);
			(*e)->constant = EXPR_CONST;
		} else if (((*e)->constant & EXPR_STR_CONST) &&
		    vcc_isconst(e2)) {
			assert((*e)->fmt == STRING_LIST);
			assert(e2->fmt == STRING);
			*e = vcc_expr_edit(STRING_LIST, "\v1\n\v2", *e, e2);
			(*e)->constant = EXPR_VAR | EXPR_STR_CONST;
		} else if (e2->fmt == STRING && vcc_isconst(e2)) {
			*e = vcc_expr_edit(STRING_LIST, "\v1,\n\v2", *e, e2);
			(*e)->constant = EXPR_VAR | EXPR_STR_CONST;
		} else {
			*e = vcc_expr_edit(STRING_LIST, "\v1,\n\v2", *e, e2);
			(*e)->constant = EXPR_VAR;
		}
		e2 = NULL;
	}
}

static const struct adds {
	unsigned	op;
	vcc_type_t	a;
	vcc_type_t	b;
	vcc_type_t	fmt;
} vcc_adds[] = {
	{ '+', BYTES,		BYTES,		BYTES },
	{ '-', BYTES,		BYTES,		BYTES },
	{ '+', DURATION,	DURATION,	DURATION },
	{ '-', DURATION,	DURATION,	DURATION },
	{ '+', INT,		INT,		INT },
	{ '-', INT,		INT,		INT },
	{ '+', INT,		REAL,		REAL },
	{ '-', INT,		REAL,		REAL },
	{ '+', REAL,		INT,		REAL },
	{ '-', REAL,		INT,		REAL },
	{ '+', REAL,		REAL,		REAL },
	{ '-', REAL,		REAL,		REAL },
	{ '-', TIME,		TIME,		DURATION },
	{ '+', TIME,		DURATION,	TIME },
	{ '-', TIME,		DURATION,	TIME },

	{ EOI, VOID, VOID, VOID }
};

static void
vcc_expr_add(struct vcc *tl, struct expr **e, vcc_type_t fmt)
{
	const struct adds *ap;
	struct expr  *e2;
	vcc_type_t f2;
	struct token *tk;
	char buf[128];

	*e = NULL;
	vcc_expr_mul(tl, e, fmt);
	ERRCHK(tl);

	if (tl->t->tok != '+' && tl->t->tok != '-')
		return;

	f2 = (*e)->fmt;
	for (ap = vcc_adds; ap->op != EOI; ap++)
		if (ap->a == f2 && ap->op == tl->t->tok)
			break;

	if (ap->op == EOI &&
	    (fmt == STRING || fmt == STRING_LIST) &&
	    f2 != STRING && f2 != STRING_LIST) {
		vcc_expr_tostring(tl, e, fmt);
		f2 = (*e)->fmt;
	}

	while (tl->t->tok == '+' || tl->t->tok == '-') {
		tk = tl->t;
		vcc_NextToken(tl);
		vcc_expr_mul(tl, &e2, f2);
		ERRCHK(tl);

		for (ap = vcc_adds; ap->op != EOI; ap++) {
			if (tk->tok == ap->op && (*e)->fmt == ap->a &&
			    e2->fmt == ap->b)
				break;
		}

		if (ap->fmt != VOID) {
			bprintf(buf, "(\v1 %c \v2)", ap->op);
			*e = vcc_expr_edit(ap->fmt, buf, *e, e2);
			continue;
		}

		if (tk->tok == '+' && ap->op == EOI) {
			if ((*e)->fmt == STRING && e2->fmt == STRING) {
				vcc_expr_string_add(tl, e, e2);
				return;
			}

			if (fmt == STRING || fmt == STRING_LIST) {
				/* Time to fold and add as string */
				if ((*e)->fmt != fmt)
					vcc_expr_tostring(tl, e, fmt);
				vcc_expr_string_add(tl, e, e2);
				return;
			}
		}

		VSB_printf(tl->sb, "%s %.*s %s not possible.\n",
		    (*e)->fmt->name, PF(tk), e2->fmt->name);
		vcc_ErrWhere2(tl, tk, tl->t);
		return;
	}
}

/*--------------------------------------------------------------------
 * Fold the STRING types correctly
 */

static void
vcc_expr_strfold(struct vcc *tl, struct expr **e, vcc_type_t fmt)
{

	vcc_expr_add(tl, e, fmt);
	ERRCHK(tl);

	if (fmt != STRING_LIST && (*e)->fmt == STRING_LIST)
		vcc_expr_tostring(tl, e, STRING);
	else if (fmt == STRING_LIST && (*e)->fmt == STRING)
		(*e)->fmt = STRING_LIST;
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    ExprCmp:
 *	ExprAdd
 *      ExprAdd Relation ExprAdd
 *	ExprAdd(STRING) '~' CString
 *	ExprAdd(STRING) '!~' CString
 *	ExprAdd(IP) '~' IP
 *	ExprAdd(IP) '!~' IP
 */

#define NUM_REL(typ)					\
	{typ,		T_EQ,	"(\v1 == \v2)" },	\
	{typ,		T_NEQ,	"(\v1 != \v2)" },	\
	{typ,		T_LEQ,	"(\v1 <= \v2)" },	\
	{typ,		T_GEQ,	"(\v1 >= \v2)" },	\
	{typ,		'<',	"(\v1 < \v2)" },	\
	{typ,		'>',	"(\v1 > \v2)" }

static const struct cmps {
	vcc_type_t		fmt;
	unsigned		token;
	const char		*emit;
} vcc_cmps[] = {
	NUM_REL(INT),
	NUM_REL(DURATION),
	NUM_REL(BYTES),
	NUM_REL(REAL),
	NUM_REL(TIME),

	{IP,		T_EQ,	"!VRT_ipcmp(\v1, \v2)" },
	{IP,		T_NEQ,	"VRT_ipcmp(\v1, \v2)" },

	{STRING,	T_EQ,	"!VRT_strcmp(\v1, \v2)" },
	{STRING,	T_NEQ,	"VRT_strcmp(\v1, \v2)" },
	{STRING_LIST,	T_EQ,	"!VRT_strcmp(\v1, \v2)" },
	{STRING_LIST,	T_NEQ,	"VRT_strcmp(\v1, \v2)" },

	{VOID, 0, NULL}
};

#undef NUM_REL

static void
vcc_expr_cmp(struct vcc *tl, struct expr **e, vcc_type_t fmt)
{
	struct expr *e2;
	const struct cmps *cp;
	char buf[256];
	const char *re;
	const char *not;
	struct token *tk;
	enum symkind kind;

	*e = NULL;

	vcc_expr_strfold(tl, e, fmt);
	ERRCHK(tl);

	if ((*e)->fmt == BOOL)
		return;

	tk = tl->t;
	for (cp = vcc_cmps; cp->fmt != VOID; cp++)
		if ((*e)->fmt == cp->fmt && tl->t->tok == cp->token)
			break;
	if (cp->fmt != VOID) {
		vcc_NextToken(tl);
		if ((*e)->fmt == STRING_LIST) {
			// XXX: This is not optimal, but we can't pass two
			// STRING_LIST's to a function anyway...
			vcc_expr_tostring(tl, e, STRING);
		}
		vcc_expr_strfold(tl, &e2, (*e)->fmt);
		ERRCHK(tl);
		if (e2->fmt != (*e)->fmt) { /* XXX */
			VSB_printf(tl->sb, "Comparison of different types: ");
			VSB_printf(tl->sb, "%s ", (*e)->fmt->name);
			vcc_ErrToken(tl, tk);
			VSB_printf(tl->sb, " %s\n", e2->fmt->name);
			vcc_ErrWhere(tl, tk);
			return;
		}
		*e = vcc_expr_edit(BOOL, cp->emit, *e, e2);
		return;
	}
	if (((*e)->fmt == STRING || (*e)->fmt == STRING_LIST) &&
	    (tl->t->tok == '~' || tl->t->tok == T_NOMATCH)) {
		if ((*e)->fmt == STRING_LIST)
			vcc_expr_tostring(tl, e, STRING);
		not = tl->t->tok == '~' ? "" : "!";
		vcc_NextToken(tl);
		ExpectErr(tl, CSTR);
		re = vcc_regexp(tl);
		ERRCHK(tl);
		vcc_NextToken(tl);
		bprintf(buf, "%sVRT_re_match(ctx, \v1, %s)", not, re);
		*e = vcc_expr_edit(BOOL, buf, *e, NULL);
		return;
	}
	if ((*e)->fmt == IP &&
	    (tl->t->tok == '~' || tl->t->tok == T_NOMATCH)) {
		not = tl->t->tok == '~' ? "" : "!";
		vcc_NextToken(tl);
		vcc_ExpectCid(tl, "ACL");
		vcc_AddRef(tl, tl->t, SYM_ACL);
		bprintf(buf, "%smatch_acl_named_%.*s(ctx, \v1)",
		    not, PF(tl->t));
		vcc_NextToken(tl);
		*e = vcc_expr_edit(BOOL, buf, *e, NULL);
		return;
	}
	if ((*e)->fmt == IP && (tl->t->tok == T_EQ || tl->t->tok == T_NEQ)) {
		vcc_Acl_Hack(tl, buf, sizeof buf);
		*e = vcc_expr_edit(BOOL, buf, *e, NULL);
		return;
	}
	kind = VCC_HandleKind((*e)->fmt);
	if (kind != SYM_NONE && (tl->t->tok == T_EQ || tl->t->tok == T_NEQ)) {
		bprintf(buf, "(\v1 %.*s \v2)", PF(tk));
		vcc_NextToken(tl);
		e2 = NULL;
		vcc_expr0(tl, &e2, (*e)->fmt);
		ERRCHK(tl);
		if (e2->fmt != (*e)->fmt) {
			VSB_printf(tl->sb, "Comparison of different types: ");
			VSB_printf(tl->sb, "%s ", (*e)->fmt->name);
			vcc_ErrToken(tl, tk);
			VSB_printf(tl->sb, " %s\n", e2->fmt->name);
			vcc_ErrWhere(tl, tk);
			return;
		}
		*e = vcc_expr_edit(BOOL, buf, *e, e2);
		return;
	}
	switch (tl->t->tok) {
	case T_EQ:
	case T_NEQ:
	case '<':
	case T_LEQ:
	case '>':
	case T_GEQ:
	case '~':
	case T_NOMATCH:
		VSB_printf(tl->sb, "Operator %.*s not possible on %s\n",
		    PF(tl->t), (*e)->fmt->name);
		vcc_ErrWhere(tl, tl->t);
		return;
	default:
		break;
	}
	if (fmt != BOOL)
		return;
	if ((*e)->fmt == STRING || (*e)->fmt == BACKEND || (*e)->fmt == INT)
		*e = vcc_expr_edit(BOOL, "(\v1 != 0)", *e, NULL);
	else if ((*e)->fmt == DURATION)
		*e = vcc_expr_edit(BOOL, "(\v1 > 0)", *e, NULL);
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    ExprNot:
 *      '!' ExprCmp
 */

static void
vcc_expr_not(struct vcc *tl, struct expr **e, vcc_type_t fmt)
{
	struct expr *e2;
	struct token *tk;

	*e = NULL;
	if (fmt != BOOL || tl->t->tok != '!') {
		vcc_expr_cmp(tl, e, fmt);
		return;
	}

	vcc_NextToken(tl);
	tk = tl->t;
	vcc_expr_cmp(tl, &e2, fmt);
	ERRCHK(tl);
	if (e2->fmt == BOOL) {
		*e = vcc_expr_edit(BOOL, "!(\v1)", e2, NULL);
		return;
	}
	VSB_printf(tl->sb, "'!' must be followed by BOOL, found ");
	VSB_printf(tl->sb, "%s.\n", e2->fmt->name);
	vcc_ErrWhere2(tl, tk, tl->t);
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    ExprCand:
 *      ExprNot { '&&' ExprNot } *
 */

static void
vcc_expr_cand(struct vcc *tl, struct expr **e, vcc_type_t fmt)
{
	struct expr *e2;
	struct token *tk;

	*e = NULL;
	vcc_expr_not(tl, e, fmt);
	ERRCHK(tl);
	if ((*e)->fmt != BOOL || tl->t->tok != T_CAND)
		return;
	*e = vcc_expr_edit(BOOL, "(\v+\n\v1", *e, NULL);
	while (tl->t->tok == T_CAND) {
		vcc_NextToken(tl);
		tk = tl->t;
		vcc_expr_not(tl, &e2, fmt);
		ERRCHK(tl);
		if (e2->fmt != BOOL) {
			VSB_printf(tl->sb,
			    "'&&' must be followed by BOOL,"
			    " found %s.\n", e2->fmt->name);
			vcc_ErrWhere2(tl, tk, tl->t);
			return;
		}
		*e = vcc_expr_edit(BOOL, "\v1\v-\n&&\v+\n\v2", *e, e2);
	}
	*e = vcc_expr_edit(BOOL, "\v1\v-\n)", *e, NULL);
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    Expr0:
 *      ExprCand { '||' ExprCand } *
 */

static void
vcc_expr0(struct vcc *tl, struct expr **e, vcc_type_t fmt)
{
	struct expr *e2;
	struct token *tk;

	*e = NULL;
	vcc_expr_cand(tl, e, fmt);
	ERRCHK(tl);
	if ((*e)->fmt == BOOL && tl->t->tok == T_COR) {
		*e = vcc_expr_edit(BOOL, "(\v+\n\v1", *e, NULL);
		while (tl->t->tok == T_COR) {
			vcc_NextToken(tl);
			tk = tl->t;
			vcc_expr_cand(tl, &e2, fmt);
			ERRCHK(tl);
			if (e2->fmt != BOOL) {
				VSB_printf(tl->sb,
				    "'||' must be followed by BOOL,"
				    " found %s.\n", e2->fmt->name);
				vcc_ErrWhere2(tl, tk, tl->t);
				return;
			}
			*e = vcc_expr_edit(BOOL, "\v1\v-\n||\v+\n\v2", *e, e2);
		}
		*e = vcc_expr_edit(BOOL, "\v1\v-\n)", *e, NULL);
	}
	if (fmt != (*e)->fmt && (fmt == STRING || fmt == STRING_LIST)) {
		vcc_expr_tostring(tl, e, fmt);
		ERRCHK(tl);
	}
}

/*--------------------------------------------------------------------
 * This function parses and emits the C-code to evaluate an expression
 *
 * We know up front what kind of type we want the expression to be,
 * and this function is the backstop if that doesn't succeed.
 */

void
vcc_Expr(struct vcc *tl, vcc_type_t fmt)
{
	struct expr *e;
	struct token *t1;

	assert(fmt != VOID);

	t1 = tl->t;
	vcc_expr0(tl, &e, fmt);
	ERRCHK(tl);
	e->t1 = t1;
	if (!tl->err && fmt != e->fmt)  {
		VSB_printf(tl->sb, "Expression has type %s, expected %s\n",
		    e->fmt->name, fmt->name);
		tl->err = 1;
	}
	if (!tl->err) {
		if (e->fmt == STRING_LIST) {
			e = vcc_expr_edit(STRING_LIST,
			    "\v+\n\v1,\nvrt_magic_string_end\v-", e, NULL);
		}
		vcc_expr_fmt(tl->fb, tl->indent, e);
		VSB_putc(tl->fb, '\n');
	} else if (t1 != tl->t)
		vcc_ErrWhere2(tl, t1, tl->t);
	vcc_delete_expr(e);
}

/*--------------------------------------------------------------------
 */

void
vcc_Expr_Call(struct vcc *tl, const struct symbol *sym)
{

	struct expr *e;
	struct token *t1;

	t1 = tl->t;
	e = NULL;
	vcc_Eval_SymFunc(tl, &e, sym, VOID);
	if (!tl->err) {
		vcc_expr_fmt(tl->fb, tl->indent, e);
		VSB_cat(tl->fb, ";\n");
	} else if (t1 != tl->t) {
		vcc_ErrWhere2(tl, t1, tl->t);
	}
	vcc_delete_expr(e);
}

/*--------------------------------------------------------------------
 */

void
vcc_Expr_Init(struct vcc *tl)
{
	struct symbol *sym;

	sym = VCC_Symbol(tl, NULL, "regsub", NULL, SYM_FUNC, 1);
	AN(sym);
	sym->eval = vcc_Eval_Regsub;
	sym->eval_priv = NULL;

	sym = VCC_Symbol(tl, NULL, "regsuball", NULL, SYM_FUNC, 1);
	AN(sym);
	sym->eval = vcc_Eval_Regsub;
	sym->eval_priv = sym;

	sym = VCC_Symbol(tl, NULL, "true", NULL, SYM_FUNC, 1);
	AN(sym);
	sym->eval = vcc_Eval_BoolConst;
	sym->eval_priv = sym;

	sym = VCC_Symbol(tl, NULL, "false", NULL, SYM_FUNC, 1);
	AN(sym);
	sym->eval = vcc_Eval_BoolConst;
	sym->eval_priv = NULL;
}
