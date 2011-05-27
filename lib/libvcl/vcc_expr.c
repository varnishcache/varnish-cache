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
 *
 * XXX: add VRT_count()'s
 */

#include "config.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "vsb.h"

#include "vcc_priv.h"
#include "vcc_compile.h"
#include "libvarnish.h"

static const char *
vcc_Type(enum var_type fmt)
{
	switch(fmt) {
#define VCC_TYPE(a)	case a: return(#a);
#include "vcc_types.h"
#undef VCC_TYPE
	default:
		assert("Unknwon Type");
		return(NULL);
	}
}

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
	else {
		vsb_printf(tl->sb, "Unknown time unit ");
		vcc_ErrToken(tl, tl->t);
		vsb_printf(tl->sb, ".  Legal are 's', 'm', 'h' and 'd'\n");
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
vcc_RTimeVal(struct vcc *tl, double *d)
{
	double v, sc;
	int sign = 1;

	if (tl->t->tok == '-') {
		sign *= -1;
		vcc_NextToken(tl);
	}
	v = vcc_DoubleVal(tl);
	ERRCHK(tl);
	ExpectErr(tl, ID);
	sc = vcc_TimeUnit(tl);
	*d = sign * v * sc;
}

/*--------------------------------------------------------------------*/

void
vcc_TimeVal(struct vcc *tl, double *d)
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
		vsb_printf(tl->sb, "Expected BYTES unit (B, KB, MB...) got ");
		vcc_ErrToken(tl, tl->t);
		vsb_printf(tl->sb, "\n");
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
		vsb_printf(tl->sb, "Unknown BYTES unit ");
		vcc_ErrToken(tl, tl->t);
		vsb_printf(tl->sb,
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
	enum var_type	fmt;
	struct vsb	*vsb;
	uint8_t		constant;
	struct token	*t1, *t2;
};

static void vcc_expr0(struct vcc *tl, struct expr **e, enum var_type fmt);

static struct expr *
vcc_new_expr(void)
{
	struct expr *e;

	/* XXX: use TlAlloc() ? */
	ALLOC_OBJ(e, EXPR_MAGIC);
	AN(e);
	e->vsb = vsb_new_auto();
	e->fmt = VOID;
	return (e);
}

static struct expr *
vcc_mk_expr(enum var_type fmt, const char *str, ...)
{
	va_list ap;
	struct expr *e;

	e = vcc_new_expr();
	e->fmt = fmt;
	va_start(ap, str);
	vsb_vprintf(e->vsb, str, ap);
	va_end(ap);
	AZ(vsb_finish(e->vsb));
	return (e);
}

static void
vcc_delete_expr(struct expr *e)
{
	if (e == NULL)
		return;
	CHECK_OBJ_NOTNULL(e, EXPR_MAGIC);
	vsb_delete(e->vsb);
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
 *	\v-  increase indentation
 *	anything else is literal
 *
 * When editing, we check if any of the subexpressions contain a newline
 * and issue it as an indented block of so.
 *
 * XXX: check line lengths in edit, should pass indent in for this
 */

static struct expr *
vcc_expr_edit(enum var_type fmt, const char *p, struct expr *e1,
    struct expr *e2)
{
	struct expr *e;
	int nl = 1;

	e = vcc_new_expr();
	while (*p != '\0') {
		if (*p == '\n') {
			if (!nl)
				vsb_putc(e->vsb, *p);
			nl = 1;
			p++;
			continue;
		}
		nl = 0;
		if (*p != '\v') {
			vsb_putc(e->vsb, *p);
			p++;
			continue;
		}
		assert(*p == '\v');
		p++;
		switch(*p) {
		case '+': vsb_cat(e->vsb, "\v+"); break;
		case '-': vsb_cat(e->vsb, "\v-"); break;
		case '1':
		case '2':
			if (*p == '1')
				vsb_cat(e->vsb, vsb_data(e1->vsb));
			else {
				AN(e2);
				vsb_cat(e->vsb, vsb_data(e2->vsb));
			}
			break;
		default:
			assert(__LINE__ == 0);
		}
		p++;
	}
	AZ(vsb_finish(e->vsb));
	if (e1 != NULL)
		e->t1 = e1->t1;
	else if (e2 != NULL)
		e->t1 = e2->t1;
	if (e2 != NULL)
		e->t2 = e2->t1;
	else if (e1 != NULL)
		e->t1 = e1->t1;
	if ((e1 == NULL || e1->constant) && (e2 == NULL || e2->constant))
		e->constant = 1;
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
		vsb_cat(d, " ");
	p = vsb_data(e1->vsb);
	while (*p != '\0') {
		if (*p == '\n') {
			vsb_putc(d, '\n');
			if (p[1] != '\0') {
				for (i = 0; i < ind; i++)
					vsb_cat(d, " ");
			}
			p++;
			continue;
		}
		if (*p != '\v') {
			vsb_putc(d, *p);
			p++;
			continue;
		}
		p++;
		switch(*p) {
		case '+': ind += 2; break;
		case '-': ind -= 2; break;
		default:
			assert(__LINE__ == 0);
		}
		p++;
	}
}

/*--------------------------------------------------------------------
 */

static enum var_type
vcc_arg_type(const char **p)
{

#define VCC_TYPE(a) if (!strcmp(#a, *p)) { *p += strlen(#a) + 1; return (a);}
#include "vcc_types.h"
#undef VCC_TYPE
	return (VOID);
}

/*--------------------------------------------------------------------
 */

static void
vcc_expr_tostring(struct expr **e, enum var_type fmt)
{
	const char *p;

	CHECK_OBJ_NOTNULL(*e, EXPR_MAGIC);
	AN(fmt == STRING || fmt == STRING_LIST);

	p = NULL;
	switch((*e)->fmt) {
	case BACKEND:	p = "VRT_backend_string(sp, \v1)"; break;
	case BOOL:	p = "VRT_bool_string(sp, \v1)"; break;
	case DURATION:	p = "VRT_double_string(sp, \v1)"; break;
			 /* XXX: should DURATION insist on "s" suffix ? */
	case INT:	p = "VRT_int_string(sp, \v1)"; break;
	case IP:	p = "VRT_IP_string(sp, \v1)"; break;
	case BYTES:	p = "VRT_double_string(sp, \v1)"; break; /* XXX */
	case REAL:	p = "VRT_double_string(sp, \v1)"; break;
	case TIME:	p = "VRT_time_string(sp, \v1)"; break;
	default:	break;
	}
	if (p != NULL)
		*e = vcc_expr_edit(fmt, p, *e, NULL);
}

/*--------------------------------------------------------------------
 */

static void
vcc_Eval_Regsub(struct vcc *tl, struct expr **e, const struct symbol *sym)
{
	struct expr *e2;
	int all = sym->eval_priv == NULL ? 0 : 1;
	char *p;
	char buf[128];

	vcc_delete_expr(*e);
	SkipToken(tl, ID);
	SkipToken(tl, '(');

	vcc_expr0(tl, &e2, STRING);
	if (e2->fmt != STRING) 
		vcc_expr_tostring(&e2, STRING);

	SkipToken(tl, ',');
	ExpectErr(tl, CSTR);
	p = vcc_regexp(tl);
	vcc_NextToken(tl);

	bprintf(buf, "VRT_regsub(sp, %d,\n\v1,\n%s\n", all, p);
	*e = vcc_expr_edit(STRING, buf, e2, *e);

	SkipToken(tl, ',');
	vcc_expr0(tl, &e2, STRING);
	if (e2->fmt != STRING) 
		vcc_expr_tostring(&e2, STRING);
	*e = vcc_expr_edit(STRING, "\v1, \v2)", *e, e2);
	SkipToken(tl, ')');
}

/*--------------------------------------------------------------------
 */

static void
vcc_Eval_BoolConst(struct vcc *tl, struct expr **e, const struct symbol *sym)
{

	vcc_NextToken(tl);
	*e = vcc_mk_expr(BOOL, "(0==%d)", sym->eval_priv == NULL ? 1 : 0);
	(*e)->constant = 1;
}

/*--------------------------------------------------------------------
 */

void
vcc_Eval_Backend(struct vcc *tl, struct expr **e, const struct symbol *sym)
{

	assert(sym->kind == SYM_BACKEND);

	vcc_ExpectCid(tl);
	vcc_AddRef(tl, tl->t, SYM_BACKEND);
	*e = vcc_mk_expr(BACKEND, "VGCDIR(_%.*s)", PF(tl->t));
	vcc_NextToken(tl);
}

/*--------------------------------------------------------------------
 */
void
vcc_Eval_Var(struct vcc *tl, struct expr **e, const struct symbol *sym)
{
	const struct var *vp;

	assert(sym->kind == SYM_VAR);
	vcc_AddUses(tl, tl->t, sym->r_methods, "Not available");
	vp = vcc_FindVar(tl, tl->t, 0, "cannot be read");
	ERRCHK(tl);
	assert(vp != NULL);
	*e = vcc_mk_expr(vp->fmt, "%s", vp->rname);
	vcc_NextToken(tl);
}

/*--------------------------------------------------------------------
 */

void
vcc_Eval_Func(struct vcc *tl, struct expr **e, const struct symbol *sym)
{
	const char *p, *r;
	struct expr *e1, *e2;
	enum var_type fmt;
	char buf[32];

	assert(sym->kind == SYM_FUNC || sym->kind == SYM_PROC);
	AN(sym->cfunc);
	AN(sym->args);
	SkipToken(tl, ID);
	SkipToken(tl, '(');
	p = sym->args;
	e2 = vcc_mk_expr(vcc_arg_type(&p), "%s(sp\v+", sym->cfunc);
	while (*p != '\0') {
		e1 = NULL;
		fmt = vcc_arg_type(&p);
		if (fmt == VOID && !strcmp(p, "PRIV_VCL")) {
			r = strchr(sym->name, '.');
			AN(r);
			e1 = vcc_mk_expr(VOID, "&vmod_priv_%.*s",
			    r - sym->name, sym->name);
			p += strlen(p) + 1;
		} else if (fmt == VOID && !strcmp(p, "PRIV_CALL")) {
			bprintf(buf, "vmod_priv_%u", tl->nvmodpriv++);
			Fh(tl, 0, "struct vmod_priv %s;\n", buf);
			e1 = vcc_mk_expr(VOID, "&%s", buf);
			p += strlen(p) + 1;
		} else if (fmt == ENUM) {
			ExpectErr(tl, ID);
			ERRCHK(tl);
			r = p;
			do {
				if (vcc_IdIs(tl->t, p))
					break;
				p += strlen(p) + 1;
			} while (*p != '\0');
			if (*p == '\0') {
				vsb_printf(tl->sb, "Wrong enum value.");
				vsb_printf(tl->sb, "  Expected one of:\n");
				do {
					vsb_printf(tl->sb, "\t%s\n", r);
					r += strlen(r) + 1;
				} while (*r != '\0');
				vcc_ErrWhere(tl, tl->t);
				return;
			}
			e1 = vcc_mk_expr(VOID, "\"%.*s\"", PF(tl->t));
			while (*p != '\0')
				p += strlen(p) + 1;
			p++;
			SkipToken(tl, ID);
			if (*p != '\0')
				SkipToken(tl, ',');
		} else if (fmt == HEADER) {
			const struct var *v;
			sym = VCC_FindSymbol(tl, tl->t, SYM_NONE);
			ERRCHK(tl);
			SkipToken(tl, ID);
			if (sym == NULL) {
				vsb_printf(tl->sb, "Symbol not found.\n");
				vcc_ErrWhere(tl, tl->t);
				return;
			}
			vcc_AddUses(tl, tl->t, sym->r_methods, "Not available");
			if (sym->kind != SYM_VAR) {
				vsb_printf(tl->sb, "Wrong kind of symbol.\n");
				vcc_ErrWhere(tl, tl->t);
				return;
			}
			AN(sym->var);
			v = sym->var;
			if (v->http == NULL) {
				vsb_printf(tl->sb,
				    "Variable not an HTTP header.\n");
				vcc_ErrWhere(tl, tl->t);
				return;
			}
			e1 = vcc_mk_expr(VOID, "%s, \"%s\"", v->http, v->hdr);
			if (*p != '\0')
				SkipToken(tl, ',');
		} else {
			vcc_expr0(tl, &e1, fmt);
			ERRCHK(tl);
			if (e1->fmt != fmt) {
				vsb_printf(tl->sb, "Wrong argument type.");
				vsb_printf(tl->sb, "  Expected %s.",
					vcc_Type(fmt));
				vsb_printf(tl->sb, "  Got %s.\n",
					vcc_Type(e1->fmt));
				vcc_ErrWhere2(tl, e1->t1, tl->t);
				return;
			}
			assert(e1->fmt == fmt);
			if (e1->fmt == STRING_LIST) {
				e1 = vcc_expr_edit(STRING_LIST,
				    "\v+\n\v1,\nvrt_magic_string_end\v-",
				    e1, NULL);
			}
			if (*p != '\0')
				SkipToken(tl, ',');
		}
		e2 = vcc_expr_edit(e2->fmt, "\v1,\n\v2", e2, e1);
	}
	SkipToken(tl, ')');
	e2 = vcc_expr_edit(e2->fmt, "\v1\n)\v-", e2, NULL);
	*e = e2;
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    Expr4:
 *	'(' Expr0 ')'
 *	CNUM
 *	CSTR
 */

static void
vcc_expr4(struct vcc *tl, struct expr **e, enum var_type fmt)
{
	struct expr *e1, *e2;
	const struct symbol *sym;
	double d;

	*e = NULL;
	if (tl->t->tok == '(') {
		SkipToken(tl, '(');
		vcc_expr0(tl, &e2, fmt);
		ERRCHK(tl);
		SkipToken(tl, ')');
		*e = vcc_expr_edit(e2->fmt, "(\v1)", e2, NULL);
		return;
	}
	switch(tl->t->tok) {
	case ID:
		/*
		 * XXX: what if var and func/proc had same name ?
		 * XXX: look for SYM_VAR first for consistency ?
		 */
		sym = VCC_FindSymbol(tl, tl->t, SYM_NONE);
		if (sym == NULL || sym->eval == NULL) {
			vsb_printf(tl->sb, "Symbol not found: ");
			vcc_ErrToken(tl, tl->t);
			vsb_printf(tl->sb, " (expected type %s):\n",
			    vcc_Type(fmt));
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		AN(sym);
		switch(sym->kind) {
		case SYM_VAR:
		case SYM_FUNC:
		case SYM_BACKEND:
			AN(sym->eval);
			AZ(*e);
			sym->eval(tl, e, sym);
			return;
		default:
			break;
		}
		vsb_printf(tl->sb,
		    "Symbol type (%s) can not be used in expression.\n",
		    VCC_SymKind(tl, sym));
		vcc_ErrWhere(tl, tl->t);
		return;
	case CSTR:
		assert(fmt != VOID);
		e1 = vcc_new_expr();
		EncToken(e1->vsb, tl->t);
		e1->fmt = STRING;
		e1->t1 = tl->t;
		e1->constant = 1;
		vcc_NextToken(tl);
		AZ(vsb_finish(e1->vsb));
		*e = e1;
		break;
	case CNUM:
		/*
		 * XXX: %g may not have enough decimals by default
		 * XXX: but %a is ugly, isn't it ?
		 */
		assert(fmt != VOID);
		if (fmt == DURATION) {
			vcc_RTimeVal(tl, &d);
			ERRCHK(tl);
			e1 = vcc_mk_expr(DURATION, "%g", d);
		} else if (fmt == BYTES) {
			vcc_ByteVal(tl, &d);
			ERRCHK(tl);
			e1 = vcc_mk_expr(BYTES, "%.1f", d);
			ERRCHK(tl);
		} else if (fmt == REAL) {
			e1 = vcc_mk_expr(REAL, "%g", vcc_DoubleVal(tl));
			ERRCHK(tl);
		} else {
			e1 = vcc_mk_expr(INT, "%.*s", PF(tl->t));
			vcc_NextToken(tl);
		}
		e1->constant = 1;
		*e = e1;
		break;
	default:
		vsb_printf(tl->sb, "Unknown token ");
		vcc_ErrToken(tl, tl->t);
		vsb_printf(tl->sb, " when looking for %s\n\n", vcc_Type(fmt));
		vcc_ErrWhere(tl, tl->t);
		break;
	}
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    Expr3:
 *      Expr4 { {'*'|'/'} Expr4 } *
 */

static void
vcc_expr_mul(struct vcc *tl, struct expr **e, enum var_type fmt)
{
	struct expr *e2;
	enum var_type f2, f3;
	struct token *tk;

	*e = NULL;
	vcc_expr4(tl, e, fmt);
	ERRCHK(tl);
	f3 = f2 = (*e)->fmt;

	switch(f2) {
	case INT:	f2 = INT; break;
	case DURATION:	f2 = REAL; break;
	case BYTES:	f2 = REAL; break;
	default:
		if (tl->t->tok != '*' && tl->t->tok != '/')
			return;
		vsb_printf(tl->sb, "Operator %.*s not possible on type %s.\n",
		    PF(tl->t), vcc_Type(f2));
		vcc_ErrWhere(tl, tl->t);
		return;
	}

	while (tl->t->tok == '*' || tl->t->tok == '/') {
		tk = tl->t;
		vcc_NextToken(tl);
		vcc_expr4(tl, &e2, f2);
		ERRCHK(tl);
		assert(e2->fmt == f2);
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
 */

static void
vcc_expr_add(struct vcc *tl, struct expr **e, enum var_type fmt)
{
	struct expr  *e2;
	enum var_type f2;
	struct token *tk;

	*e = NULL;
	vcc_expr_mul(tl, e, fmt);
	ERRCHK(tl);
	f2 = (*e)->fmt;

	if ((f2 == STRING_LIST || f2 == STRING) && tl->t->tok == '+') {
		while (tl->t->tok == '+') {
			vcc_NextToken(tl);
			vcc_expr_mul(tl, &e2, STRING);
			ERRCHK(tl);
			if (e2->fmt != STRING && e2->fmt != STRING_LIST)
				vcc_expr_tostring(&e2, f2);
			ERRCHK(tl);
			assert(e2->fmt == STRING || e2->fmt == STRING_LIST);
			if ((*e)->constant &&  e2->constant) {
				assert((*e)->fmt == STRING);
				assert(e2->fmt == STRING);
				*e = vcc_expr_edit(STRING, "\v1\n\v2", *e, e2);
			} else {
				*e = vcc_expr_edit(STRING_LIST,
				    "\v1,\n\v2", *e, e2);
			}
		}
	}
	if (fmt != STRING_LIST && (*e)->fmt == STRING_LIST)
		*e = vcc_expr_edit(STRING,
		    "\v+VRT_WrkString(sp,\n\v1,\nvrt_magic_string_end)",
		    *e, NULL);
	if (fmt == STRING_LIST && (*e)->fmt == STRING)
		(*e)->fmt = STRING_LIST;

	switch(f2) {
	case INT:	break;
	case TIME:	break;
	case DURATION:	break;
	case BYTES:	break;
	default:
		if (tl->t->tok != '+' && tl->t->tok != '-')
			return;
		vsb_printf(tl->sb, "Operator %.*s not possible on type %s.\n",
		    PF(tl->t), vcc_Type(f2));
		vcc_ErrWhere(tl, tl->t);
		return;
	}

	while (tl->t->tok == '+' || tl->t->tok == '-') {
		if (f2 == TIME)
			f2 = DURATION;
		tk = tl->t;
		vcc_NextToken(tl);
		vcc_expr_mul(tl, &e2, f2);
		ERRCHK(tl);
		if (tk->tok == '-' && (*e)->fmt == TIME && e2->fmt == TIME) {
			/* OK */
		} else if (tk->tok == '-' &&
		    (*e)->fmt == BYTES && e2->fmt == BYTES) {
			/* OK */
		} else if (e2->fmt != f2) {
			vsb_printf(tl->sb, "%s %.*s %s not possible.\n",
			    vcc_Type((*e)->fmt), PF(tk), vcc_Type(e2->fmt));
			vcc_ErrWhere2(tl, tk, tl->t);
			return;
		}
		if (tk->tok == '+')
			*e = vcc_expr_edit(f2, "(\v1+\v2)", *e, e2);
		else if (f2 == TIME && e2->fmt == TIME)
			*e = vcc_expr_edit(DURATION, "(\v1-\v2)", *e, e2);
		else
			*e = vcc_expr_edit(f2, "(\v1-\v2)", *e, e2);
	}
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
	enum var_type		fmt;
	unsigned		token;
	const char		*emit;
} vcc_cmps[] = {
	NUM_REL(INT),
	NUM_REL(DURATION),
	NUM_REL(BYTES),

	{STRING,	T_EQ,	"!VRT_strcmp(\v1, \v2)" },
	{STRING,	T_NEQ,	"VRT_strcmp(\v1, \v2)" },

	{VOID, 0, NULL}
};

#undef NUM_REL

static void
vcc_expr_cmp(struct vcc *tl, struct expr **e, enum var_type fmt)
{
	struct expr *e2;
	const struct cmps *cp;
	char buf[256];
	char *re;
	const char *not;
	struct token *tk;

	*e = NULL;

	vcc_expr_add(tl, e, fmt);
	ERRCHK(tl);

	if ((*e)->fmt == BOOL)
		return;

	tk = tl->t;
	for (cp = vcc_cmps; cp->fmt != VOID; cp++)
		if ((*e)->fmt == cp->fmt && tl->t->tok == cp->token)
			break;
	if (cp->fmt != VOID) {
		vcc_NextToken(tl);
		vcc_expr_add(tl, &e2, (*e)->fmt);
		ERRCHK(tl);
		if (e2->fmt != (*e)->fmt) { /* XXX */
			vsb_printf(tl->sb, "Comparison of different types: ");
			vsb_printf(tl->sb, "%s ", vcc_Type((*e)->fmt));
			vcc_ErrToken(tl, tk);
			vsb_printf(tl->sb, " %s\n", vcc_Type(e2->fmt));
			vcc_ErrWhere(tl, tk);
			return;
		}
		*e = vcc_expr_edit(BOOL, cp->emit, *e, e2);
		return;
	}
	if ((*e)->fmt == STRING &&
	    (tl->t->tok == '~' || tl->t->tok == T_NOMATCH)) {
	        not = tl->t->tok == '~' ? "" : "!";
		vcc_NextToken(tl);
		ExpectErr(tl, CSTR);
		re = vcc_regexp(tl);
		ERRCHK(tl);
		vcc_NextToken(tl);
		bprintf(buf, "%sVRT_re_match(\v1, %s)", not, re);
		*e = vcc_expr_edit(BOOL, buf, *e, NULL);
		return;
	}
	if ((*e)->fmt == IP &&
	    (tl->t->tok == '~' || tl->t->tok == T_NOMATCH)) {
	        not = tl->t->tok == '~' ? "" : "!";
		vcc_NextToken(tl);
		ExpectErr(tl, ID);
		vcc_AddRef(tl, tl->t, SYM_ACL);
		bprintf(buf, "%smatch_acl_named_%.*s(sp, \v1)", not, PF(tl->t));
		vcc_NextToken(tl);
		*e = vcc_expr_edit(BOOL, buf, *e, NULL);
		return;
	}
	if ((*e)->fmt == IP && (tl->t->tok == T_EQ || tl->t->tok == T_NEQ)) {
		vcc_Acl_Hack(tl, buf);
		*e = vcc_expr_edit(BOOL, buf, *e, NULL);
		return;
	}
	if ((*e)->fmt == BACKEND &&
	    (tl->t->tok == T_EQ || tl->t->tok == T_NEQ)) {
		vcc_NextToken(tl);
		ExpectErr(tl, ID);
		vcc_AddRef(tl, tl->t, SYM_BACKEND);
		bprintf(buf, "(\v1 %.*s VGCDIR(_%.*s))", PF(tk), PF(tl->t));
		vcc_NextToken(tl);
		*e = vcc_expr_edit(BOOL, buf, *e, NULL);
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
		vsb_printf(tl->sb, "Operator %.*s not possible on %s\n",
		    PF(tl->t), vcc_Type((*e)->fmt));
		vcc_ErrWhere(tl, tl->t);
		return;
	default:
		break;
	}
	if (fmt == BOOL && (*e)->fmt == STRING) {
		*e = vcc_expr_edit(BOOL, "(\v1 != 0)", *e, NULL);
		return;
	}
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    ExprNot:
 *      '!' ExprCmp
 */

static void
vcc_expr_not(struct vcc *tl, struct expr **e, enum var_type fmt)
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
	vsb_printf(tl->sb, "'!' must be followed by BOOL, found ");
	vsb_printf(tl->sb, "%s.\n", vcc_Type(e2->fmt));
	vcc_ErrWhere2(tl, tk, tl->t);
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    ExprCand:
 *      ExprNot { '&&' ExprNot } *
 */

static void
vcc_expr_cand(struct vcc *tl, struct expr **e, enum var_type fmt)
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
			vsb_printf(tl->sb,
			    "'&&' must be followed by BOOL, found ");
			vsb_printf(tl->sb, "%s.\n", vcc_Type(e2->fmt));
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
vcc_expr0(struct vcc *tl, struct expr **e, enum var_type fmt)
{
	struct expr *e2;
	struct token *tk;

	*e = NULL;
	vcc_expr_cand(tl, e, fmt);
	ERRCHK(tl);
	if ((*e)->fmt != BOOL || tl->t->tok != T_COR)
		return;
	*e = vcc_expr_edit(BOOL, "(\v+\n\v1", *e, NULL);
	while (tl->t->tok == T_COR) {
		vcc_NextToken(tl);
		tk = tl->t;
		vcc_expr_cand(tl, &e2, fmt);
		ERRCHK(tl);
		if (e2->fmt != BOOL) {
			vsb_printf(tl->sb,
			    "'||' must be followed by BOOL, found ");
			vsb_printf(tl->sb, "%s.\n", vcc_Type(e2->fmt));
			vcc_ErrWhere2(tl, tk, tl->t);
			return;
		}
		*e = vcc_expr_edit(BOOL, "\v1\v-\n||\v+\n\v2", *e, e2);
	}
	*e = vcc_expr_edit(BOOL, "\v1\v-\n)", *e, NULL);
}

/*--------------------------------------------------------------------
 * This function parses and emits the C-code to evaluate an expression
 *
 * We know up front what kind of type we want the expression to be,
 * and this function is the backstop if that doesn't succeed.
 */

void
vcc_Expr(struct vcc *tl, enum var_type fmt)
{
	struct expr *e;
	struct token *t1;

	assert(fmt != VOID);

	t1 = tl->t;
	vcc_expr0(tl, &e, fmt);
	ERRCHK(tl);
	if (fmt == STRING || fmt == STRING_LIST)
		vcc_expr_tostring(&e, fmt);
	if (!tl->err && fmt != e->fmt)  {
		vsb_printf(tl->sb, "Expression has type %s, expected %s\n",
		    vcc_Type(e->fmt), vcc_Type(fmt));
		tl->err = 1;
	}
	if (!tl->err) {
		if (e->fmt == STRING_LIST) {
			e = vcc_expr_edit(STRING_LIST,
			    "\v+\n\v1,\nvrt_magic_string_end\v-", e, NULL);
		}
		vcc_expr_fmt(tl->fb, tl->indent, e);
		vsb_putc(tl->fb, '\n');
	} else {
		if (t1 != tl->t)
			vcc_ErrWhere2(tl, t1, tl->t);
	}
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
	vcc_Eval_Func(tl, &e, sym);
	if (!tl->err) {
		vcc_expr_fmt(tl->fb, tl->indent, e);
		vsb_cat(tl->fb, ";\n");
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

	sym = VCC_AddSymbolStr(tl, "regsub", SYM_FUNC);
	AN(sym);
	sym->eval = vcc_Eval_Regsub;
	sym->eval_priv = NULL;

	sym = VCC_AddSymbolStr(tl, "regsuball", SYM_FUNC);
	AN(sym);
	sym->eval = vcc_Eval_Regsub;
	sym->eval_priv = sym;

	sym = VCC_AddSymbolStr(tl, "true", SYM_FUNC);
	AN(sym);
	sym->eval = vcc_Eval_BoolConst;
	sym->eval_priv = sym;

	sym = VCC_AddSymbolStr(tl, "false", SYM_FUNC);
	AN(sym);
	sym->eval = vcc_Eval_BoolConst;
	sym->eval_priv = NULL;
}
