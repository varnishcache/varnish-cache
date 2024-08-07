/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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
 */

#include "config.h"

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "vcc_compile.h"
#include "vjsn.h"

struct expr {
	unsigned	magic;
#define EXPR_MAGIC	0x38c794ab
	vcc_type_t	fmt;
	struct vsb	*vsb;
	uint8_t		constant;
#define EXPR_VAR	(1<<0)
#define EXPR_CONST	(1<<1)
#define EXPR_STR_CONST	(1<<2)		// Last string elem is "..."
	struct token	*t1, *t2;
	struct symbol	*instance;
	int		nstr;
};

/*--------------------------------------------------------------------
 * Facility for carrying expressions around and do text-processing on
 * them.
 */

static inline int
vcc_isconst(const struct expr *e)
{
	AN(e->constant);
	return (e->constant & EXPR_CONST);
}

static inline int
vcc_islit(const struct expr *e)
{
	AN(e->constant);
	return (e->constant & EXPR_STR_CONST);
}

static const char *
vcc_utype(vcc_type_t t)
{
	if (t == STRINGS || t->stringform)
		t = STRING;
	return (t->name);
}

static void vcc_expr0(struct vcc *tl, struct expr **e, vcc_type_t fmt);
static void vcc_expr_cor(struct vcc *tl, struct expr **e, vcc_type_t fmt);
static void vcc_expr_typecheck(struct vcc *tl, struct expr **e, vcc_type_t fmt,
    struct token *t1);

static struct expr *
vcc_new_expr(vcc_type_t fmt)
{
	struct expr *e;

	ALLOC_OBJ(e, EXPR_MAGIC);
	AN(e);
	e->vsb = VSB_new_auto();
	e->fmt = fmt;
	e->constant = EXPR_VAR;
	return (e);
}

static struct expr * v_printflike_(2, 3)
vcc_mk_expr(vcc_type_t fmt, const char *str, ...)
{
	va_list ap;
	struct expr *e;

	e = vcc_new_expr(fmt);
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
	CHECK_OBJ(e, EXPR_MAGIC);
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
 *	\vS  insert subexpression 1(STRINGS) as STRING
 *	\vs  insert subexpression 2(STRINGS) as STRING
 *	\vT  insert subexpression 1(STRINGS) as STRANDS
 *	\vt  insert subexpression 2(STRINGS) as STRANDS
 *	\v+  increase indentation
 *	\v-  decrease indentation
 *	anything else is literal
 *
 * When editing, we check if any of the subexpressions contain a newline
 * and issue it as an indented block of so.
 *
 * XXX: check line lengths in edit, should pass indent in for this
 */

static void
vcc_strands_edit(const struct expr *e1, const struct expr *e2)
{

	if (e2->nstr == 1) {
		VSB_printf(e1->vsb, "TOSTRAND(%s)", VSB_data(e2->vsb));
		return;
	}

	VSB_printf(e1->vsb, "TOSTRANDS(%d,\v+\n%s\v-)",
	    e2->nstr, VSB_data(e2->vsb));
}

static struct expr *
vcc_expr_edit(struct vcc *tl, vcc_type_t fmt, const char *p, struct expr *e1,
    struct expr *e2)
{
	struct expr *e, *e3;
	int nl = 1;

	(void) tl;

	AN(e1);
	e = vcc_new_expr(fmt);
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
		case '+': VSB_cat(e->vsb, "\v+"); nl = 0; break;
		case '-': VSB_cat(e->vsb, "\v-"); nl = 0; break;
		case 'S':
		case 's':
			e3 = (*p == 'S' ? e1 : e2);
			AN(e3);
			assert(e1->fmt == STRINGS);
			if (e3->nstr > 1) {
				VSB_cat(e->vsb,
				    "\nVRT_STRANDS_string(ctx,\v+\n");
				vcc_strands_edit(e, e3);
				VSB_cat(e->vsb,
				    "\v-\n)\n");
			} else {
				VSB_cat(e->vsb, VSB_data(e3->vsb));
			}
			break;
		case 'T':
		case 't':
			e3 = (*p == 'T' ? e1 : e2);
			AN(e3);
			vcc_strands_edit(e, e3);
			break;
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

	if (!e1->fmt->noindent) {
		for (i = 0; i < ind; i++)
			VSB_putc(d, ' ');
	}
	p = VSB_data(e1->vsb);
	while (*p != '\0') {
		if (*p == '\n') {
			VSB_putc(d, '\n');
			if (*++p == '\0')
				break;
			for (i = 0; i < ind; i++)
				VSB_putc(d, ' ');
		} else if (*p != '\v') {
			VSB_putc(d, *p++);
		} else {
			switch (*++p) {
			case '+': ind += INDENT; break;
			case '-': ind -= INDENT; break;
			default:  WRONG("Illegal format in VCC expression");
			}
			p++;
		}
	}
}

/*--------------------------------------------------------------------
 */

static void
vcc_expr_tobool(struct vcc *tl, struct expr **e)
{

	if ((*e)->fmt == BOOL)
		return;
	if ((*e)->fmt == BACKEND || (*e)->fmt == INT)
		*e = vcc_expr_edit(tl, BOOL, "(\v1 != 0)", *e, NULL);
	else if ((*e)->fmt == DURATION)
		*e = vcc_expr_edit(tl, BOOL, "(\v1 > 0)", *e, NULL);
	else if ((*e)->fmt == STRINGS)
		*e = vcc_expr_edit(tl, BOOL, "VRT_Strands2Bool(\vT)", *e, NULL);
	/*
	 * We do not provide automatic folding from REAL to BOOL
	 * because comparing to zero is seldom an exact science
	 * and we want to force people to explicitly get it right.
	 */
}

/*--------------------------------------------------------------------
 */

static void
vcc_expr_tostring(struct vcc *tl, struct expr **e)
{
	const char *p;
	uint8_t	constant = EXPR_VAR;

	CHECK_OBJ_NOTNULL(*e, EXPR_MAGIC);
	assert((*e)->fmt != STRINGS);

	p = (*e)->fmt->tostring;
	if (p != NULL) {
		AN(*p);
		*e = vcc_expr_edit(tl, STRINGS, p, *e, NULL);
		(*e)->constant = constant;
		(*e)->nstr = 1;
	} else {
		VSB_printf(tl->sb,
		    "Cannot convert %s to STRING.\n",
		    vcc_utype((*e)->fmt));
		vcc_ErrWhere2(tl, (*e)->t1, tl->t);
	}
}

/*--------------------------------------------------------------------
 */

void v_matchproto_(sym_expr_t)
vcc_Eval_Handle(struct vcc *tl, struct expr **e, struct token *t,
    struct symbol *sym, vcc_type_t type)
{

	(void)t;
	(void)tl;
	AN(sym->rname);
	AZ(type->stringform);

	if (sym->type->tostring == NULL &&
	    sym->type != STRING && type == STRINGS) {
		*e = vcc_mk_expr(STRINGS, "\"%s\"", sym->name);
		(*e)->nstr = 1;
		(*e)->constant |= EXPR_CONST | EXPR_STR_CONST;
	} else {
		*e = vcc_mk_expr(sym->type, "%s", sym->rname);
		(*e)->constant = EXPR_VAR;
		(*e)->nstr = 1;
		if ((*e)->fmt == STRING)
			(*e)->fmt = STRINGS;
	}
}

void v_matchproto_(sym_expr_t)
vcc_Eval_Sub(struct vcc *tl, struct expr **e, struct token *t,
    struct symbol *sym, vcc_type_t type)
{

	(void)t;
	(void)tl;
	AN(sym->rname);
	AZ(type->stringform);

	assert (sym->type == SUB);

	if (type == SUB) {
		*e = vcc_mk_expr(sym->type, "%s", sym->rname);
		(*e)->constant = EXPR_CONST;
		return;
	}

	VSB_printf(tl->sb, "Symbol '%s' can only be used as a %s expression\n",
	    sym->name, sym->type->name);
	vcc_ErrWhere(tl, tl->t);
}

/*--------------------------------------------------------------------
 */

void v_matchproto_(sym_expr_t)
vcc_Eval_Var(struct vcc *tl, struct expr **e, struct token *t,
    struct symbol *sym, vcc_type_t type)
{

	(void)type;
	vcc_AddUses(tl, t, NULL, sym, XREF_READ);
	ERRCHK(tl);
	*e = vcc_mk_expr(sym->type, "%s", sym->rname);
	(*e)->constant = EXPR_VAR;
	(*e)->nstr = 1;
	if ((*e)->fmt == STRING)
		(*e)->fmt = STRINGS;
}

void v_matchproto_(sym_expr_t)
vcc_Eval_ProtectedHeader(struct vcc *tl, struct expr **e, struct token *t,
    struct symbol *sym, vcc_type_t type)
{

	AN(sym);
	AZ(sym->lorev);

	vcc_Header_Fh(tl, sym);
	sym->eval = vcc_Eval_Var;
	vcc_Eval_Var(tl, e, t, sym, type);
}

/*--------------------------------------------------------------------
 */

static struct expr *
vcc_priv_arg(struct vcc *tl, const char *p, struct symbol *sym)
{
	char buf[64];
	struct inifin *ifp;
	const char *f = NULL;

	AN(sym);
	AN(sym->vmod_name);

	if (!strcmp(p, "PRIV_VCL"))
		return (vcc_mk_expr(VOID, "&vmod_priv_%s", sym->vmod_name));

	if (!strcmp(p, "PRIV_CALL")) {
		bprintf(buf, "vmod_priv_%u", tl->unique++);
		ifp = New_IniFin(tl);
		Fh(tl, 0, "static struct vmod_priv %s;\n", buf);
		VSB_printf(ifp->fin, "\tVRT_priv_fini(ctx, &%s);", buf);
		return (vcc_mk_expr(VOID, "&%s", buf));
	}

	if (!strcmp(p, "PRIV_TASK"))
		f = "task";
	else if (!strcmp(p, "PRIV_TOP")) {
		f = "top";
		sym->r_methods &= VCL_MET_TASK_C;
	} else {
		WRONG("Wrong PRIV_ type");
	}
	AN(f);

	return (vcc_mk_expr(VOID, "VRT_priv_%s(ctx, &VGC_vmod_%s)",
	    f, sym->vmod_name));
}

struct func_arg {
	vcc_type_t		type;
	const struct vjsn_val	*enums;
	const char		*cname;
	const char		*name;
	const char		*val;
	struct expr		*result;
	int			avail;
	int			optional;
	VTAILQ_ENTRY(func_arg)	list;
};

static void
vcc_do_enum(struct vcc *tl, struct func_arg *fa, int len, const char *ptr)
{
	const char *r;

	(void)tl;
	r = strchr(fa->cname, '.');
	AN(r);
	fa->result = vcc_mk_expr(VOID, "*%.*s.enum_%.*s",
	    (int)(r - fa->cname), fa->cname, len, ptr);
}

static void
vcc_do_arg(struct vcc *tl, struct func_arg *fa)
{
	struct expr *e2;
	struct vjsn_val *vv;

	if (fa->type == ENUM) {
		ExpectErr(tl, ID);
		ERRCHK(tl);
		VTAILQ_FOREACH(vv, &fa->enums->children, list)
			if (vcc_IdIs(tl->t, vv->value))
				break;
		if (vv == NULL) {
			VSB_cat(tl->sb, "Wrong enum value.");
			VSB_cat(tl->sb, "  Expected one of:\n");
			VTAILQ_FOREACH(vv, &fa->enums->children, list)
				VSB_printf(tl->sb, "\t%s\n", vv->value);
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		vcc_do_enum(tl, fa, PF(tl->t));
		SkipToken(tl, ID);
	} else {
		if (fa->type == SUB)
			tl->subref++;
		vcc_expr0(tl, &e2, fa->type);
		ERRCHK(tl);
		assert(e2->fmt == fa->type);
		fa->result = e2;
	}
	fa->avail = 1;
}

static void
vcc_func(struct vcc *tl, struct expr **e, const void *priv,
    const char *extra, struct symbol *sym)
{
	vcc_type_t rfmt;
	const char *cfunc;
	struct expr *e1;
	struct func_arg *fa, *fa2;
	VTAILQ_HEAD(,func_arg) head;
	struct token *tf, *t1;
	const struct vjsn_val *vv, *vvp;
	const char *sa, *extra_sep;
	char ssa[64];
	int n;

	CAST_OBJ_NOTNULL(vv, priv, VJSN_VAL_MAGIC);
	assert(vjsn_is_array(vv));
	vv = VTAILQ_FIRST(&vv->children);
	rfmt = VCC_Type(VTAILQ_FIRST(&vv->children)->value);
	AN(rfmt);
	vv = VTAILQ_NEXT(vv, list);
	cfunc = vv->value;
	vv = VTAILQ_NEXT(vv, list);
	sa = vv->value;
	if (*sa == '\0') {
		sa = NULL;
	}
	vv = VTAILQ_NEXT(vv, list);
	if (sym->kind == SYM_METHOD) {
		if (*e == NULL) {
			VSB_cat(tl->sb, "Syntax error.");
			tl->err = 1;
			return;
		}
		vcc_NextToken(tl);
		AZ(extra);
		AN((*e)->instance);
		extra = (*e)->instance->rname;
	}
	tf = VTAILQ_PREV(tl->t, tokenhead, list);
	SkipToken(tl, '(');
	if (extra == NULL) {
		extra = "";
		extra_sep = "";
	} else {
		AN(*extra);
		extra_sep = ", ";
	}
	VTAILQ_INIT(&head);
	for (;vv != NULL; vv = VTAILQ_NEXT(vv, list)) {
		assert(vjsn_is_array(vv));
		fa = calloc(1, sizeof *fa);
		AN(fa);
		fa->cname = cfunc;
		VTAILQ_INSERT_TAIL(&head, fa, list);

		vvp = VTAILQ_FIRST(&vv->children);
		if (!memcmp(vvp->value, "PRIV_", 5)) {
			fa->result = vcc_priv_arg(tl, vvp->value, sym);
			vvp = VTAILQ_NEXT(vvp, list);
			if (vvp != NULL)
				fa->name = vvp->value;
			continue;
		}
		fa->type = VCC_Type(vvp->value);
		AN(fa->type);
		vvp = VTAILQ_NEXT(vvp, list);
		if (vvp != NULL) {
			fa->name = vvp->value;
			vvp = VTAILQ_NEXT(vvp, list);
			if (vvp != NULL) {
				fa->val = vvp->value;
				vvp = VTAILQ_NEXT(vvp, list);
				if (vvp != NULL) {
					fa->enums = vvp;
					vvp = VTAILQ_NEXT(vvp, list);
				}
			}
		}
		if (sa != NULL && vvp != NULL && vjsn_is_true(vvp)) {
			fa->optional = 1;
			vvp = VTAILQ_NEXT(vvp, list);
		}
		AZ(vvp);
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
		if (tl->err)
			VSB_printf(tl->sb, "Expected argument: %s %s\n\n",
			    fa->type->name,
			    fa->name ? fa->name : "(unnamed argument)");
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
			AN(fa->name);
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

	if (sa != NULL)
		e1 = vcc_mk_expr(rfmt, "%s(ctx%s%s,\v+\n&(%s)\v+ {\n",
		    cfunc, extra_sep, extra, sa);
	else
		e1 = vcc_mk_expr(rfmt, "%s(ctx%s%s\v+",
		    cfunc, extra_sep, extra);
	n = 0;
	VTAILQ_FOREACH_SAFE(fa, &head, list, fa2) {
		n++;
		if (fa->optional) {
			AN(fa->name);
			bprintf(ssa, "\v1.valid_%s = %d,\n",
			    fa->name, fa->avail);
			e1 = vcc_expr_edit(tl, e1->fmt, ssa, e1, NULL);
		}
		if (fa->result == NULL && fa->type == ENUM && fa->val != NULL)
			vcc_do_enum(tl, fa, strlen(fa->val), fa->val);
		if (fa->result == NULL && fa->val != NULL)
			fa->result = vcc_mk_expr(fa->type, "%s", fa->val);
		if (fa->result != NULL && sa != NULL) {
			if (fa->name)
				bprintf(ssa, "\v1.%s = \v2,\n", fa->name);
			else
				bprintf(ssa, "\v1.arg%d = \v2,\n", n);
			e1 = vcc_expr_edit(tl, e1->fmt, ssa, e1, fa->result);
		} else if (fa->result != NULL) {
			e1 = vcc_expr_edit(tl, e1->fmt, "\v1,\n\v2",
			    e1, fa->result);
		} else if (!fa->optional) {
			if (fa->name)
				VSB_printf(tl->sb, "Argument '%s' missing\n",
				    fa->name);
			else
				VSB_printf(tl->sb, "Argument %d missing\n", n);
			vcc_ErrWhere(tl, tl->t);
		}
		free(fa);
	}
	if (sa != NULL) {
		*e = vcc_expr_edit(tl, e1->fmt, "\v1\v-\n}\v-\n)", e1, NULL);
	} else {
		*e = vcc_expr_edit(tl, e1->fmt, "\v1\v-\n)", e1, NULL);
	}
	SkipToken(tl, ')');
	vcc_AddUses(tl, tf, NULL, sym, XREF_READ);
}


/*--------------------------------------------------------------------
 */

void
vcc_Eval_Func(struct vcc *tl, const struct vjsn_val *spec,
    const char *extra, struct symbol *sym)
{
	struct expr *e = NULL;

	vcc_func(tl, &e, spec, extra, sym);
	if (tl->err)
		VSB_cat(tl->sb, "While compiling function call:\n");
	ERRCHK(tl);
	vcc_expr_fmt(tl->fb, tl->indent, e);
	VSB_cat(tl->fb, ";\n");
	vcc_delete_expr(e);
}

/*--------------------------------------------------------------------
 */

void v_matchproto_(sym_expr_t)
vcc_Eval_SymFunc(struct vcc *tl, struct expr **e, struct token *t,
    struct symbol *sym, vcc_type_t fmt)
{

	(void)t;
	(void)fmt;
	assert(sym->kind == SYM_FUNC || sym->kind == SYM_METHOD);
	AN(sym->eval_priv);

	vcc_func(tl, e, sym->eval_priv, sym->extra, sym);
	ERRCHK(tl);
	if ((*e)->fmt == STRING) {
		(*e)->fmt = STRINGS;
		(*e)->nstr = 1;
	}
}

/*--------------------------------------------------------------------
 */

static void
vcc_number(struct vcc *tl, struct expr **e, vcc_type_t fmt, const char *sign)
{
	VCL_INT vi;
	struct expr *e1;
	struct token *t;

	assert(fmt != VOID);
	if (fmt == BYTES) {
		vcc_ByteVal(tl, &vi);
		ERRCHK(tl);
		e1 = vcc_mk_expr(BYTES, "%ju", (intmax_t)vi);
	} else {
		t = tl->t;
		vcc_NextToken(tl);
		if (tl->t->tok == ID) {
			e1 = vcc_mk_expr(DURATION, "%s%.3f * %g",
			    sign, t->num, vcc_DurationUnit(tl));
			ERRCHK(tl);
		} else if (fmt == REAL || t->tok == FNUM) {
			e1 = vcc_mk_expr(REAL, "%s%.3f", sign, t->num);
		} else {
			e1 = vcc_mk_expr(INT, "%s%.0f", sign, t->num);
		}
	}
	e1->constant = EXPR_CONST;
	*e = e1;
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    Expr5:
 *	'(' ExprCor ')'
 *	symbol
 *	CNUM
 *	FNUM
 *	CSTR
 *	CBLOB
 */

static void
vcc_expr5(struct vcc *tl, struct expr **e, vcc_type_t fmt)
{
	struct expr *e1, *e2;
	const char *ip, *sign;
	struct token *t, *t1;
	struct symbol *sym;

	sign = "";
	*e = NULL;
	if (tl->t->tok == '(') {
		SkipToken(tl, '(');
		vcc_expr_cor(tl, &e2, fmt);
		ERRCHK(tl);
		SkipToken(tl, ')');
		if (e2->fmt == STRINGS)
			*e = e2;
		else
			*e = vcc_expr_edit(tl, e2->fmt, "(\v1)", e2, NULL);
		return;
	}
	switch (tl->t->tok) {
	case ID:
		t = tl->t;
		t1 = vcc_PeekToken(tl);
		AN(t1);
		sym = VCC_SymbolGet(tl, SYM_MAIN, SYM_NONE,
		    SYMTAB_PARTIAL_NOERR, XREF_REF);
		if (sym == NULL && fmt->global_pfx != NULL && t1->tok != '.') {
			sym = VCC_SymbolGet(tl, SYM_MAIN, SYM_NONE,
			    SYMTAB_CREATE, XREF_REF);
			ERRCHK(tl);
			AN(sym);
			VCC_GlobalSymbol(sym, fmt);
		}
		ERRCHK(tl);
		if (sym == NULL)
			AZ(VCC_SymbolGet(tl, SYM_MAIN, SYM_NONE,
			    SYMTAB_PARTIAL, XREF_REF));
		ERRCHK(tl);
		AN(sym);
		if (sym->kind == SYM_INSTANCE) {
			AZ(*e);
			*e = vcc_new_expr(sym->type);
			(*e)->instance = sym;
			return;
		}
		if (sym->kind == SYM_FUNC && sym->type == VOID) {
			VSB_cat(tl->sb, "Function returns VOID:\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		if (sym->eval != NULL) {
			AN(sym->eval);
			AZ(*e);
			sym->eval(tl, e, t, sym, fmt);
			if (tl->err) {
				VSB_cat(tl->sb,
				    "While compiling function call:\n\n");
				vcc_ErrWhere2(tl, t, tl->t);
			}
			ERRCHK(tl);
			/* Unless asked for a HEADER, fold to string here */
			if (*e && fmt != HEADER && (*e)->fmt == HEADER) {
				vcc_expr_tostring(tl, e);
				ERRCHK(tl);
			}
			return;
		}
		VSB_printf(tl->sb,
		    "Symbol '%.*s' type (%s) cannot be used in expression.\n",
		    PF(t), sym->kind->name);
		vcc_ErrWhere(tl, t);
		if (sym->def_b != NULL) {
			VSB_cat(tl->sb, "That symbol was defined here:\n");
			vcc_ErrWhere(tl, sym->def_b);
		}
		return;
	case CSTR:
		assert(fmt != VOID);
		if (fmt == IP) {
			if (*tl->t->dec == '/') {
				/*
				 * On some platforms (e.g. FreeBSD),
				 * getaddrinfo(3) may resolve a path to a
				 * sockaddr_un if it happens to exist and
				 * is a socket. So don't let that happen.
				 */
				VSB_cat(tl->sb,
				    "Cannot convert to an IP address: ");
				vcc_ErrToken(tl, tl->t);
				vcc_ErrWhere(tl, tl->t);
				return;
			}
			Resolve_Sockaddr(tl, tl->t->dec, "80",
			    &ip, NULL, &ip, NULL, NULL, 1,
			    tl->t, "IP constant");
			ERRCHK(tl);
			e1 = vcc_mk_expr(IP, "%s", ip);
			ERRCHK(tl);
		} else if (fmt == REGEX) {
			e1 = vcc_new_expr(REGEX);
			vcc_regexp(tl, e1->vsb);
			AZ(VSB_finish(e1->vsb));
		} else {
			e1 = vcc_new_expr(STRINGS);
			EncToken(e1->vsb, tl->t);
			AZ(VSB_finish(e1->vsb));
			e1->constant |= EXPR_STR_CONST;
			e1->nstr = 1;
		}
		e1->t1 = tl->t;
		e1->constant |= EXPR_CONST;
		vcc_NextToken(tl);
		*e = e1;
		return;
	case '-':
		if (fmt != INT &&
		    fmt != REAL &&
		    fmt != DURATION &&
		    fmt != STRINGS)
			break;
		vcc_NextToken(tl);
		if (tl->t->tok != FNUM && tl->t->tok != CNUM) {
			vcc_expr_cor(tl, &e1, fmt);
			ERRCHK(tl);
			*e = vcc_expr_edit(tl, e1->fmt, "-(\v1)", e1, NULL);
			return;
		}
		sign = "-";
		/* FALLTHROUGH */
	case FNUM:
	case CNUM:
		vcc_number(tl, e, fmt, sign);
		return;
	case CBLOB:
		e1 = vcc_new_expr(BLOB);
		VSB_printf(e1->vsb, "%s", tl->t->dec);
		AZ(VSB_finish(e1->vsb));
		e1->constant |= EXPR_STR_CONST;
		e1->t1 = tl->t;
		vcc_NextToken(tl);
		*e = e1;
		return;
	default:
		break;
	}
	VSB_cat(tl->sb, "Unknown token ");
	vcc_ErrToken(tl, tl->t);
	VSB_printf(tl->sb, " when looking for %s\n\n", vcc_utype(fmt));
	vcc_ErrWhere(tl, tl->t);
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    Expr4:
 *      Expr5 [ '.' (type_attribute | type_method()) ]*
 */

void
vcc_Eval_TypeMethod(struct vcc *tl, struct expr **e, struct token *t,
    struct symbol *sym, vcc_type_t fmt)
{
	const char *impl;

	(void)t;
	impl = VCC_Type_EvalMethod(tl, sym);
	ERRCHK(tl);
	AN(impl);
	*e = vcc_expr_edit(tl, fmt, impl, *e, NULL);
}

static void
vcc_expr4(struct vcc *tl, struct expr **e, vcc_type_t fmt)
{
	struct symbol *sym;

	*e = NULL;
	vcc_expr5(tl, e, fmt);
	ERRCHK(tl);
	AN(*e);
	while (tl->t->tok == '.') {
		vcc_NextToken(tl);
		ExpectErr(tl, ID);

		sym = VCC_TypeSymbol(tl, SYM_METHOD, (*e)->fmt);
		if (sym == NULL) {
			VSB_cat(tl->sb, "Unknown property ");
			vcc_ErrToken(tl, tl->t);
			VSB_printf(tl->sb, " for type %s\n", (*e)->fmt->name);
			vcc_ErrWhere(tl, tl->t);
			return;
		}

		AN(sym->eval);
		sym->eval(tl, e, tl->t, sym, sym->type);
		ERRCHK(tl);
		if ((*e)->fmt == STRING) {
			(*e)->fmt = STRINGS;
			(*e)->nstr = 1;
		}
	}
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    ExprMul:
 *      Expr4 { {'*'|'/'|'%'} Expr4 } *
 */

static void
vcc_expr_mul(struct vcc *tl, struct expr **e, vcc_type_t fmt)
{
	struct expr *e2;
	vcc_type_t f2;
	struct token *tk;
	char buf[24];

	*e = NULL;
	vcc_expr4(tl, e, fmt);
	ERRCHK(tl);
	AN(*e);

	while (tl->t->tok == '*' || tl->t->tok == '/' || tl->t->tok == '%') {
		if (tl->t->tok == '%' && ((*e)->fmt != INT)) {
			VSB_cat(tl->sb, "Operator % only possible on INT.\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		f2 = (*e)->fmt->multype;
		if (f2 == NULL) {
			VSB_printf(tl->sb,
			    "Operator %.*s not possible on type %s.\n",
			    PF(tl->t), vcc_utype((*e)->fmt));
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		tk = tl->t;
		vcc_NextToken(tl);
		vcc_expr4(tl, &e2, f2);
		ERRCHK(tl);
		if (e2->fmt != INT && e2->fmt != f2) {
			VSB_printf(tl->sb, "%s %.*s %s not possible.\n",
			    vcc_utype((*e)->fmt), PF(tk), vcc_utype(e2->fmt));
			vcc_ErrWhere(tl, tk);
			return;
		}
		bprintf(buf, "(\v1%c\v2)", tk->tok);
		*e = vcc_expr_edit(tl, (*e)->fmt, buf, *e, e2);
	}
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    ExprAdd:
 *      ExprMul { {'+'|'-'} ExprMul } *
 */

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

	{ EOI, VOID,		VOID,		VOID }
};

static void
vcc_expr_add(struct vcc *tl, struct expr **e, vcc_type_t fmt)
{
	const struct adds *ap;
	struct expr  *e2;
	struct token *tk;
	int lit, n;

	*e = NULL;
	vcc_expr_mul(tl, e, fmt);
	ERRCHK(tl);

	while (tl->t->tok == '+' || tl->t->tok == '-') {
		tk = tl->t;
		for (ap = vcc_adds; ap->op != EOI; ap++)
			if (tk->tok == ap->op && (*e)->fmt == ap->a)
				break;
		vcc_NextToken(tl);
		if (ap->op == EOI && fmt == STRINGS)
			vcc_expr_mul(tl, &e2, STRINGS);
		else
			vcc_expr_mul(tl, &e2, (*e)->fmt);
		ERRCHK(tl);

		for (ap = vcc_adds; ap->op != EOI; ap++)
			if (tk->tok == ap->op && (*e)->fmt == ap->a &&
			    e2->fmt == ap->b)
				break;

		if (ap->op == '+') {
			*e = vcc_expr_edit(tl, ap->fmt, "(\v1 + \v2)", *e, e2);
		} else if (ap->op == '-') {
			*e = vcc_expr_edit(tl, ap->fmt, "(\v1 - \v2)", *e, e2);
		} else if (tk->tok == '+' &&
		    ((*e)->fmt == STRINGS || fmt == STRINGS)) {
			if ((*e)->fmt != STRINGS)
				vcc_expr_tostring(tl, e);
			if (e2->fmt != STRINGS)
				vcc_expr_tostring(tl, &e2);
			if (vcc_islit(*e) && vcc_isconst(e2)) {
				lit = vcc_islit(e2);
				*e = vcc_expr_edit(tl, STRINGS,
				    "\v1\n\v2", *e, e2);
				(*e)->constant = EXPR_CONST;
				(*e)->nstr = 1;
				if (lit)
					(*e)->constant |= EXPR_STR_CONST;
			} else {
				n = (*e)->nstr + e2->nstr;
				*e = vcc_expr_edit(tl, STRINGS,
				    "\v1,\n\v2", *e, e2);
				(*e)->constant = EXPR_VAR;
				(*e)->nstr = n;
			}
		} else {
			VSB_printf(tl->sb, "%s %.*s %s not possible.\n",
			    vcc_utype((*e)->fmt), PF(tk), vcc_utype(e2->fmt));
			vcc_ErrWhere2(tl, tk, tl->t);
			return;
		}
	}

	/* No concatenation, finalize string. */
	if ((*e)->fmt->stringform)
		vcc_expr_tostring(tl, e);
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    ExprCmp:
 *	ExprAdd
 *      ExprAdd Relation ExprAdd
 *	ExprAdd(STRING) '~' CString
 *	ExprAdd(STRING) '!~' CString
 *	ExprAdd(IP) '==' ExprAdd(IP)
 *	ExprAdd(IP) '!=' ExprAdd(IP)
 *	ExprAdd(IP) '~' ACL
 *	ExprAdd(IP) '!~' ACL
 */

struct cmps;

typedef void cmp_f(struct vcc *, struct expr **, const struct cmps *);

struct cmps {
	vcc_type_t		fmt;
	unsigned		token;
	cmp_f			*func;
	const char		*emit;
};

static void v_matchproto_(cmp_f)
cmp_simple(struct vcc *tl, struct expr **e, const struct cmps *cp)
{
	struct expr *e2;
	struct token *tk;

	tk = tl->t;
	vcc_NextToken(tl);
	vcc_expr_add(tl, &e2, (*e)->fmt);
	ERRCHK(tl);

	if (e2->fmt != (*e)->fmt) {
		VSB_printf(tl->sb,
		    "Comparison of different types: %s '%.*s' %s\n",
		    vcc_utype((*e)->fmt), PF(tk), vcc_utype(e2->fmt));
		vcc_ErrWhere(tl, tk);
	} else
		*e = vcc_expr_edit(tl, BOOL, cp->emit, *e, e2);
}

static void v_matchproto_(cmp_f)
cmp_regexp(struct vcc *tl, struct expr **e, const struct cmps *cp)
{
	struct token *t1;
	struct expr *e2;
	char buf[128];

	*e = vcc_expr_edit(tl, STRING, "\vS", *e, NULL);
	vcc_NextToken(tl);
	t1 = tl->t;
	vcc_expr4(tl, &e2, REGEX);
	ERRCHK(tl);
	vcc_expr_typecheck(tl, &e2, REGEX, t1);
	ERRCHK(tl);
	bprintf(buf, "%sVRT_re_match(ctx, \v1, \v2)", cp->emit);
	*e = vcc_expr_edit(tl, BOOL, buf, *e, e2);
}

static void v_matchproto_(cmp_f)
cmp_acl(struct vcc *tl, struct expr **e, const struct cmps *cp)
{
	struct token *t1;
	struct expr *e2;
	char buf[256];

	vcc_NextToken(tl);
	t1 = tl->t;
	vcc_expr4(tl, &e2, ACL);
	ERRCHK(tl);
	vcc_expr_typecheck(tl, &e2, ACL, t1);
	ERRCHK(tl);
	bprintf(buf, "%sVRT_acl_match(ctx, \v1, \v2)", cp->emit);
	*e = vcc_expr_edit(tl, BOOL, buf, e2, *e);
}

static void v_matchproto_(cmp_f)
cmp_string(struct vcc *tl, struct expr **e, const struct cmps *cp)
{
	struct expr *e2;
	struct token *tk;
	char buf[128];

	tk = tl->t;
	vcc_NextToken(tl);
	vcc_expr_add(tl, &e2, STRINGS);
	ERRCHK(tl);
	if (e2->fmt != STRINGS) {
		VSB_printf(tl->sb,
		    "Comparison of different types: %s '%.*s' %s\n",
		    vcc_utype((*e)->fmt), PF(tk), vcc_utype(e2->fmt));
		vcc_ErrWhere(tl, tk);
	} else if ((*e)->nstr == 1 && e2->nstr == 1) {
		bprintf(buf, "(%s VRT_strcmp(\v1, \v2))", cp->emit);
		*e = vcc_expr_edit(tl, BOOL, buf, *e, e2);
	} else {
		bprintf(buf, "(%s VRT_CompareStrands(\vT, \vt))", cp->emit);
		*e = vcc_expr_edit(tl, BOOL, buf, *e, e2);
	}
}

#define IDENT_REL(typ)							\
	{typ,		T_EQ,		cmp_simple, "(\v1 == \v2)" },	\
	{typ,		T_NEQ,		cmp_simple, "(\v1 != \v2)" }

#define NUM_REL(typ)							\
	IDENT_REL(typ),							\
	{typ,		T_LEQ,		cmp_simple, "(\v1 <= \v2)" },	\
	{typ,		T_GEQ,		cmp_simple, "(\v1 >= \v2)" },	\
	{typ,		'<',		cmp_simple, "(\v1 < \v2)" },	\
	{typ,		'>',		cmp_simple, "(\v1 > \v2)" }

static const struct cmps vcc_cmps[] = {
	NUM_REL(INT),
	NUM_REL(DURATION),
	NUM_REL(BYTES),
	NUM_REL(REAL),
	NUM_REL(TIME),
	IDENT_REL(BACKEND),
	IDENT_REL(ACL),
	IDENT_REL(PROBE),
	IDENT_REL(STEVEDORE),
	IDENT_REL(SUB),
	IDENT_REL(INSTANCE),

	{BOOL,		T_EQ,		cmp_simple, "((!(\v1)) == (!(\v2)))" },
	{BOOL,		T_NEQ,		cmp_simple, "((!(\v1)) != (!(\v2)))" },
	{IP,		T_EQ,		cmp_simple, "!VRT_ipcmp(ctx, \v1, \v2)" },
	{IP,		T_NEQ,		cmp_simple, "VRT_ipcmp(ctx, \v1, \v2)" },

	{IP,		'~',		cmp_acl, "" },
	{IP,		T_NOMATCH,	cmp_acl, "!" },

	{STRINGS,	T_EQ,		cmp_string, "0 =="},
	{STRINGS,	T_NEQ,		cmp_string, "0 !="},
	{STRINGS,	'<',		cmp_string, "0 > "},
	{STRINGS,	'>',		cmp_string, "0 < "},
	{STRINGS,	T_LEQ,		cmp_string, "0 >="},
	{STRINGS,	T_GEQ,		cmp_string, "0 <="},

	{STRINGS,	'~',		cmp_regexp, "" },
	{STRINGS,	T_NOMATCH,	cmp_regexp, "!" },

	{VOID,		0,		NULL, NULL}
};

#undef IDENT_REL
#undef NUM_REL

static void
vcc_expr_cmp(struct vcc *tl, struct expr **e, vcc_type_t fmt)
{
	const struct cmps *cp;
	struct token *tk;

	*e = NULL;
	vcc_expr_add(tl, e, fmt);
	ERRCHK(tl);
	tk = tl->t;

	for (cp = vcc_cmps; cp->fmt != VOID; cp++) {
		if (tl->t->tok != cp->token)
			continue;
		if ((*e)->fmt != cp->fmt)
			continue;
		AN(cp->func);
		cp->func(tl, e, cp);
		return;
	}

	switch (tk->tok) {
	case T_EQ:
	case T_NEQ:
	case '<':
	case T_LEQ:
	case '>':
	case T_GEQ:
	case '~':
	case T_NOMATCH:
		VSB_printf(tl->sb, "Operator %.*s not possible on %s\n",
		    PF(tl->t), vcc_utype((*e)->fmt));
		vcc_ErrWhere(tl, tl->t);
		return;
	default:
		break;
	}
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    ExprNot:
 *      '!' ExprCmp
 */

static void
vcc_expr_not(struct vcc *tl, struct expr **e, vcc_type_t fmt)
{
	struct token *tk;

	*e = NULL;
	tk = tl->t;
	if (tl->t->tok == '!')
		vcc_NextToken(tl);
	vcc_expr_cmp(tl, e, fmt);
	ERRCHK(tl);
	if (tk->tok != '!')
		return;
	vcc_expr_tobool(tl, e);
	ERRCHK(tl);
	if ((*e)->fmt != BOOL) {
		VSB_cat(tl->sb, "'!' must be followed by BOOL, found ");
		VSB_printf(tl->sb, "%s.\n", vcc_utype((*e)->fmt));
		vcc_ErrWhere2(tl, tk, tl->t);
	} else {
		*e = vcc_expr_edit(tl, BOOL, "!(\v1)", *e, NULL);
	}
}

/*--------------------------------------------------------------------
 * CAND and COR are identical save for a few details, but they are
 * stacked so handling them in the same function is not simpler.
 * Instead have them both call this helper function to do everything.
 */

typedef void upfunc(struct vcc *tl, struct expr **e, vcc_type_t fmt);

static void
vcc_expr_bin_bool(struct vcc *tl, struct expr **e, vcc_type_t fmt,
    unsigned ourtok, upfunc *up, const char *tokstr)
{
	struct expr *e2;
	struct token *tk;
	char buf[32];

	*e = NULL;
	tk = tl->t;
	up(tl, e, fmt);
	ERRCHK(tl);
	if (tl->t->tok != ourtok)
		return;
	vcc_expr_tobool(tl, e);
	ERRCHK(tl);
	if ((*e)->fmt != BOOL) {
		VSB_printf(tl->sb,
		    "'%s' must be preceeded by BOOL,"
		    " found %s.\n", tokstr, vcc_utype((*e)->fmt));
		vcc_ErrWhere2(tl, tk, tl->t);
		return;
	}
	*e = vcc_expr_edit(tl, BOOL, "(\v+\n\v1", *e, NULL);
	while (tl->t->tok == ourtok) {
		vcc_NextToken(tl);
		tk = tl->t;
		up(tl, &e2, fmt);
		ERRCHK(tl);
		vcc_expr_tobool(tl, &e2);
		ERRCHK(tl);
		if (e2->fmt != BOOL) {
			VSB_printf(tl->sb,
			    "'%s' must be followed by BOOL,"
			    " found %s.\n", tokstr, vcc_utype(e2->fmt));
			vcc_ErrWhere2(tl, tk, tl->t);
			vcc_delete_expr(e2);
			return;
		}
		bprintf(buf, "\v1\v-\n%s\v+\n\v2", tokstr);
		*e = vcc_expr_edit(tl, BOOL, buf, *e, e2);
	}
	*e = vcc_expr_edit(tl, BOOL, "\v1\v-\n)", *e, NULL);
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    ExprCand:
 *      ExprNot { '&&' ExprNot } *
 */

static void
vcc_expr_cand(struct vcc *tl, struct expr **e, vcc_type_t fmt)
{

	vcc_expr_bin_bool(tl, e, fmt, T_CAND, vcc_expr_not, "&&");
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    ExprCOR:
 *      ExprCand { '||' ExprCand } *
 */

static void
vcc_expr_cor(struct vcc *tl, struct expr **e, vcc_type_t fmt)
{

	vcc_expr_bin_bool(tl, e, fmt, T_COR, vcc_expr_cand, "||");
}

/*--------------------------------------------------------------------
 * This function is the entry-point for getting an expression with
 * a particular type, ready for inclusion in the VGC.
 */

static void
vcc_expr0(struct vcc *tl, struct expr **e, vcc_type_t fmt)
{
	struct token *t1;

	assert(fmt != VOID);
	assert(fmt != STRINGS);
	*e = NULL;
	t1 = tl->t;
	if (fmt->stringform)
		vcc_expr_cor(tl, e, STRINGS);
	else
		vcc_expr_cor(tl, e, fmt);
	ERRCHK(tl);

	if ((*e)->fmt == fmt)
		return;

	if ((*e)->fmt != STRINGS && fmt->stringform)
		vcc_expr_tostring(tl, e);

	if ((*e)->fmt->stringform) {
		VSB_printf(tl->sb, "Cannot convert type %s(%s) to %s(%s)\n",
		    vcc_utype((*e)->fmt), (*e)->fmt->name,
		    vcc_utype(fmt), fmt->name);
		vcc_ErrWhere2(tl, t1, tl->t);
		return;
	}

	if (fmt == BODY && !(*e)->fmt->bodyform)
		vcc_expr_tostring(tl, e);

	if (fmt == BODY && (*e)->fmt->bodyform) {
		if ((*e)->fmt == STRINGS)
			*e = vcc_expr_edit(tl, BODY, "STRING, 0, \vT", *e, NULL);
		else if ((*e)->fmt == BLOB)
			*e = vcc_expr_edit(tl, BODY, "BLOB, 0, \v1", *e, NULL);
		else
			WRONG("Unhandled bodyform");
	}

	if ((*e)->fmt == STRINGS && fmt->stringform) {
		if (fmt == STRING)
			*e = vcc_expr_edit(tl, STRING, "\vS", *e, NULL);
		else if (fmt == STRANDS)
			*e = vcc_expr_edit(tl, STRANDS, "\vT", (*e), NULL);
		else
			WRONG("Unhandled stringform");
	}

	if (fmt == BOOL) {
		vcc_expr_tobool(tl, e);
		ERRCHK(tl);
	}

	vcc_expr_typecheck(tl, e, fmt, t1);
}

static void
vcc_expr_typecheck(struct vcc *tl, struct expr **e, vcc_type_t fmt,
    struct token *t1)
{

	assert(fmt != VOID);
	assert(fmt != STRINGS);

	if (fmt != (*e)->fmt)  {
		VSB_printf(tl->sb, "Expression has type %s, expected %s\n",
		    vcc_utype((*e)->fmt), vcc_utype(fmt));
		vcc_ErrWhere2(tl, t1, tl->t);
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
	struct expr *e = NULL;

	assert(fmt != VOID);
	assert(fmt != STRINGS);
	vcc_expr0(tl, &e, fmt);
	ERRCHK(tl);
	assert(e->fmt == fmt);

	vcc_expr_fmt(tl->fb, tl->indent, e);
	VSB_cat(tl->fb, "\n");
	vcc_delete_expr(e);
}

/*--------------------------------------------------------------------
 */

void v_matchproto_(sym_act_f)
vcc_Act_Call(struct vcc *tl, struct token *t, struct symbol *sym)
{

	struct expr *e;

	e = NULL;
	vcc_func(tl, &e, sym->eval_priv, sym->extra, sym);
	if (!tl->err) {
		vcc_expr_fmt(tl->fb, tl->indent, e);
		SkipToken(tl, ';');
		VSB_cat(tl->fb, ";\n");
	} else if (t != tl->t) {
		VSB_cat(tl->sb, "While compiling function call:\n\n");
		vcc_ErrWhere2(tl, t, tl->t);
	}
	vcc_delete_expr(e);
}

void v_matchproto_(sym_act_f)
vcc_Act_Obj(struct vcc *tl, struct token *t, struct symbol *sym)
{

	struct expr *e = NULL;

	assert(sym->kind == SYM_INSTANCE);
	ExpectErr(tl, '.');
	tl->t = t;
	vcc_expr4(tl, &e, sym->type);
	ERRCHK(tl);
	vcc_expr_fmt(tl->fb, tl->indent, e);
	vcc_delete_expr(e);
	SkipToken(tl, ';');
	VSB_cat(tl->fb, ";\n");
}

/*--------------------------------------------------------------------
 */

static void v_matchproto_(sym_expr_t)
vcc_Eval_Regsub(struct vcc *tl, struct expr **e, struct token *t,
    struct symbol *sym, vcc_type_t fmt)
{
	struct expr *e2, *e3;
	int all = sym->eval_priv == NULL ? 0 : 1;
	char buf[128];

	(void)t;
	(void)fmt;
	SkipToken(tl, '(');
	vcc_expr0(tl, &e2, STRING);
	ERRCHK(tl);
	SkipToken(tl, ',');
	vcc_expr0(tl, &e3, REGEX);
	ERRCHK(tl);

	bprintf(buf, "VRT_regsub(ctx, %d,\v+\n\v1,\n\v2", all);
	*e = vcc_expr_edit(tl, STRING, buf, e2, e3);
	SkipToken(tl, ',');
	vcc_expr0(tl, &e2, STRING);
	ERRCHK(tl);
	*e = vcc_expr_edit(tl, STRINGS, "\v1,\n\v2)\v-", *e, e2);
	(*e)->nstr = 1;
	SkipToken(tl, ')');
}

/*--------------------------------------------------------------------
 */

static void v_matchproto_(sym_expr_t)
vcc_Eval_BoolConst(struct vcc *tl, struct expr **e, struct token *t,
    struct symbol *sym, vcc_type_t fmt)
{

	(void)t;
	(void)tl;
	(void)fmt;
	*e = vcc_mk_expr(BOOL, "(0==%d)", sym->eval_priv == NULL ? 1 : 0);
	(*e)->constant = EXPR_CONST;
}

/*--------------------------------------------------------------------
 */

static void v_matchproto_(sym_expr_t)
vcc_Eval_Default(struct vcc *tl, struct expr **e, struct token *t,
    struct symbol *sym, vcc_type_t fmt)
{
	(void)e;
	(void)fmt;
	(void)sym;
	(void)t;

	if (fmt == PROBE)
		*e = vcc_mk_expr(PROBE, "%s", vcc_default_probe(tl));
	else if (fmt == BACKEND)
		*e = vcc_mk_expr(BACKEND, "*(VCL_conf.default_director)");
	else {
		VSB_cat(tl->sb, "Symbol 'default' is a reserved word.\n");
		vcc_ErrWhere(tl, t);
	}
}

/*--------------------------------------------------------------------
 */

void
vcc_Expr_Init(struct vcc *tl)
{
	struct symbol *sym;

	sym = VCC_MkSym(tl, "regsub", SYM_MAIN, SYM_FUNC, VCL_LOW, VCL_HIGH);
	AN(sym);
	sym->type = STRING;
	sym->eval = vcc_Eval_Regsub;
	sym->eval_priv = NULL;

	sym = VCC_MkSym(tl, "regsuball", SYM_MAIN, SYM_FUNC, VCL_LOW, VCL_HIGH);
	AN(sym);
	sym->type = STRING;
	sym->eval = vcc_Eval_Regsub;
	sym->eval_priv = sym;

	sym = VCC_MkSym(tl, "true", SYM_MAIN, SYM_FUNC, VCL_LOW, VCL_HIGH);
	AN(sym);
	sym->type = BOOL;
	sym->eval = vcc_Eval_BoolConst;
	sym->eval_priv = sym;

	sym = VCC_MkSym(tl, "false", SYM_MAIN, SYM_FUNC, VCL_LOW, VCL_HIGH);
	AN(sym);
	sym->type = BOOL;
	sym->eval = vcc_Eval_BoolConst;
	sym->eval_priv = NULL;

	sym = VCC_MkSym(tl, "default", SYM_MAIN, SYM_FUNC, VCL_LOW, VCL_HIGH);
	AN(sym);
	sym->type = BACKEND;	// ... can also (sometimes) deliver PROBE
	sym->eval = vcc_Eval_Default;
}
