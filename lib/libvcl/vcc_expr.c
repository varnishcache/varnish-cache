/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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

#include "svnid.h"
SVNID("$Id$")

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
		return("Unknown Type");
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

/*--------------------------------------------------------------------
 *
 */

struct expr {
	unsigned	magic;
#define EXPR_MAGIC	0x38c794ab
	enum var_type	fmt;
	struct vsb	*vsb;
	/* XXX: first and last token */
};

static void vcc_expr0(struct vcc *tl, struct expr **e, enum var_type fmt);

static struct expr *
vcc_new_expr(void)
{
	struct expr *e;

	/* XXX: use TlAlloc() ? */
	ALLOC_OBJ(e, EXPR_MAGIC);
	AN(e);
	e->vsb = vsb_newauto();
	e->fmt = VOID;
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
vcc_expr_edit(enum var_type fmt, const char *p, struct expr *e1, struct expr *e2)
{
	struct expr *e;
	char *q;

	q = strchr(vsb_data(e1->vsb), '\n');
	if (q == NULL && e2 != NULL)
		q = strchr(vsb_data(e2->vsb), '\n');
	e = vcc_new_expr();
	while (*p != '\0') {
		if (*p != '\v') {
			vsb_putc(e->vsb, *p);
			p++;
			continue;
		} 
		p++;
		switch(*p) {
		case '+': vsb_cat(e->vsb, "\v+"); break;
		case '-': vsb_cat(e->vsb, "\v-"); break;
		case '1': 
		case '2': 
			if (q != NULL) 
				vsb_cat(e->vsb, "\v+\n");
			if (*p == '1')
				vsb_cat(e->vsb, vsb_data(e1->vsb));
			else
				vsb_cat(e->vsb, vsb_data(e2->vsb));
			if (q != NULL) 
				vsb_cat(e->vsb, "\v-\n");
			break;
		default:
			assert(__LINE__ == 0);
		}
		p++;
	}
	vsb_finish(e->vsb);
	AZ(vsb_overflowed(e->vsb));
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
	for (p = vsb_data(e1->vsb); *p != '\0'; p++) {
		if (*p == '\n') {
			vsb_putc(d, '\n');
			if (p[1] != '\0') {
				for (i = 0; i < ind; i++)
					vsb_cat(d, " ");
			}
			continue;
		}
		if (*p != '\v') {
			vsb_putc(d, *p);
			continue;
		}
		p++;
		switch(*p) {
		case '+': ind += 2; break;
		case '-': ind -= 2; break;
		default:
			assert(__LINE__ == 0);
		}
	}
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
	const struct var *vp;
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
	e1 = vcc_new_expr();
	switch(tl->t->tok) {
	case ID:
		sym = VCC_FindSymbol(tl, tl->t);
		if (sym == NULL) {
			vsb_printf(tl->sb, "Symbol not found: ");
			vcc_ErrToken(tl, tl->t);
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		AN(sym);

		vcc_AddUses(tl, tl->t, sym->r_methods, "Not available");
		AN(sym->var);
		vp = vcc_FindVar(tl, tl->t, 0, "cannot be read");
		ERRCHK(tl);
		assert(vp != NULL);
		vsb_printf(e1->vsb, "%s", vp->rname);
		e1->fmt = vp->fmt;
		vcc_NextToken(tl);
		break;
	case CSTR:
		assert(fmt != VOID);
		EncToken(e1->vsb, tl->t);
		e1->fmt = STRING;
		vcc_NextToken(tl);
		break;
	case CNUM:
		assert(fmt != VOID);
		if (fmt == DURATION) {
			vcc_RTimeVal(tl, &d);
			ERRCHK(tl);
			vsb_printf(e1->vsb, "%g", d);
			e1->fmt = DURATION;
		} else {
			vsb_printf(e1->vsb, "%.*s", PF(tl->t));
			vcc_NextToken(tl);
			e1->fmt = INT;
		}
		break;
	default:
		e1->fmt = fmt;
		vsb_printf(e1->vsb, "<E4 %.*s %u>", PF(tl->t), tl->t->tok);
		vcc_NextToken(tl);
		break;
	}

	vsb_finish(e1->vsb);
	AZ(vsb_overflowed(e1->vsb));
	*e = e1;
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

	*e = NULL;
	vcc_expr4(tl, e, fmt);
	ERRCHK(tl);
	f3 = f2 = (*e)->fmt;

	switch(f2) {
	case INT:	f2 = INT; break;
	case DURATION:	f2 = INT; break; 	/* XXX: should be Double */
	default:
		return;
	}
	while (tl->t->tok == '+' || tl->t->tok == '-') {
		vcc_NextToken(tl);
		vcc_expr4(tl, &e2, f2);
		ERRCHK(tl);
		if (tl->t->tok == '+')
			*e = vcc_expr_edit(f3, "(\v1+\v2)", *e, e2);
		else
			*e = vcc_expr_edit(f3, "(\v1-\v2)", *e, e2);
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

	*e = NULL;
	vcc_expr_mul(tl, e, fmt);
	ERRCHK(tl);
	f2 = (*e)->fmt;

	switch(f2) {
	case INT:	break;
	case TIME:	f2 = DURATION; break;
	case DURATION:	f2 = DURATION; break;
	default:
		return;
	}

	while (tl->t->tok == '+' || tl->t->tok == '-') {
		vcc_NextToken(tl);
		vcc_expr_mul(tl, &e2, f2);
		ERRCHK(tl);
		if (tl->t->tok == '+')
			*e = vcc_expr_edit(f2, "(\v1+\v2)", *e, e2);
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

static const struct cmps {
	enum var_type 		fmt;
	unsigned		token;
	const char		*emit;
} vcc_cmps[] = {
	{INT,		T_EQ,	"(\v1 == \v2)" },
	{INT,		T_NEQ,	"(\v1 != \v2)" },
	{INT,		T_LEQ,	"(\v1 <= \v2)" },
	{INT,		T_GEQ,	"(\v1 >= \v2)" },
	{INT,		'<',	"(\v1 < \v2)" },
	{INT,		'>',	"(\v1 > \v2)" },

	{DURATION,	T_EQ,	"(\v1 == \v2)" },
	{DURATION,	T_NEQ,	"(\v1 != \v2)" },
	{DURATION,	T_LEQ,	"(\v1 <= \v2)" },
	{DURATION,	T_GEQ,	"(\v1 >= \v2)" },
	{DURATION,	'<',	"(\v1 < \v2)" },
	{DURATION,	'>',	"(\v1 > \v2)" },

	{STRING,	T_EQ,	"!VRT_strcmp(\v1, \v2)" },
	{STRING,	T_NEQ,	"VRT_strcmp(\v1, \v2)" },

	{VOID, 0, NULL}
};

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
	
	if (fmt == BOOL && tl->t->tok == '!') {
		vcc_NextToken(tl);
		vcc_expr_add(tl, &e2, fmt);
		ERRCHK(tl);
		*e = vcc_expr_edit(BOOL, "!(\v1)", e2, NULL);
		return;
	}

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
		if (e2->fmt != (*e)->fmt) { /* XXX */
			vsb_printf(tl->sb, "Comparison of different types\n");
			vsb_printf(tl->sb, "Left side has type %s\n",
			    vcc_Type((*e)->fmt));
			vsb_printf(tl->sb, "Right side has type %s\n",
			    vcc_Type(e2->fmt));
			vcc_ErrToken(tl, tk);
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
		vcc_AddRef(tl, tl->t, R_ACL);
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
		vcc_AddRef(tl, tl->t, R_BACKEND);
		bprintf(buf, "(\v1 %.*s VGCDIR(_%.*s))", PF(tk), PF(tl->t));
		vcc_NextToken(tl);
		*e = vcc_expr_edit(BOOL, buf, *e, NULL);
		return;
	}
	if (fmt == BOOL) {
		switch((*e)->fmt) {
		case STRING:
			*e = vcc_expr_edit(BOOL, "(\v1 != 0)", *e, NULL);
			return;
		default:
			break;
		}
	}
	if (fmt == VOID || fmt != (*e)->fmt) {
		vsb_printf(tl->sb, "WANT: %s has %s next %.*s (%s)\n",
		    vcc_Type(fmt), vcc_Type((*e)->fmt),
		    PF(tl->t), vsb_data((*e)->vsb));
		tl->err = 1;
	}
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    ExprCand:
 *      ExprAdd { '&&' ExprAdd } *
 */

static void
vcc_expr_cand(struct vcc *tl, struct expr **e, enum var_type fmt)
{
	struct expr *e2;

	*e = NULL;
	vcc_expr_cmp(tl, e, fmt);
	ERRCHK(tl);
	if ((*e)->fmt != BOOL) 
		return;
	while (tl->t->tok == T_CAND) {
		vcc_NextToken(tl);
		vcc_expr_cmp(tl, &e2, fmt);
		ERRCHK(tl);
		*e = vcc_expr_edit(BOOL, "(\v1&&\v2)", *e, e2);
	}
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

	*e = NULL;
	vcc_expr_cand(tl, e, fmt);
	ERRCHK(tl);
	if ((*e)->fmt != BOOL)
		return;
	while (tl->t->tok == T_COR) {
		vcc_NextToken(tl);
		vcc_expr_cand(tl, &e2, fmt);
		ERRCHK(tl);
		*e = vcc_expr_edit(BOOL, "(\v1||\v2)", *e, e2);
	}
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
	if (!tl->err && fmt != e->fmt)  {
		vsb_printf(tl->sb, "Expression has type %s, expected %s\n",
		    vcc_Type(e->fmt), vcc_Type(fmt));
		tl->err = 1;
	}
	if (!tl->err) {
		vcc_expr_fmt(tl->fb, tl->indent, e);
		vsb_putc(tl->fb, '\n');
	} else {
		vsb_printf(tl->sb, "Expression starts here:\n");
		vcc_ErrWhere(tl, t1);
		if (t1 != tl->t) {
			vsb_printf(tl->sb, "Expression ends here:\n");
			vcc_ErrWhere(tl, tl->t);
		}
	}
	vcc_delete_expr(e);
}
