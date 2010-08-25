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
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <stdio.h>
#include <string.h>

#include "vsb.h"

#include "vcc_priv.h"
#include "vcc_compile.h"
#include "libvarnish.h"

/*--------------------------------------------------------------------*/

static void vcc_Compound(struct vcc *tl);
static void vcc_Conditional(struct vcc *tl);

/*--------------------------------------------------------------------*/

#define L(tl, foo)	do {	\
	tl->indent += INDENT;	\
	foo;			\
	tl->indent -= INDENT;	\
} while (0)

#define C(tl, sep)	do {					\
	Fb(tl, 1, "VRT_count(sp, %u)%s\n", ++tl->cnt, sep);	\
	tl->t->cnt = tl->cnt;					\
} while (0)

/*--------------------------------------------------------------------*/

static void
vcc_inval_test(struct vcc *tl, const char *type, const char *valid)
{
	vsb_printf(tl->sb, "Invalid test ");
	vcc_ErrToken(tl, tl->t);
	vsb_printf(tl->sb, " on expression of type %s.\n", type);
	vsb_printf(tl->sb, "  only %s are legal\n", valid);
	vcc_ErrWhere(tl, tl->t);
}

/*--------------------------------------------------------------------*/

static void
vcc_Cond_String(struct vcc *tl, const char *a1)
{
	char *p;

	switch (tl->t->tok) {
	case '~':
	case T_NOMATCH:
		Fb(tl, 1, "%sVRT_re_match(",
		     tl->t->tok == '~' ? "" : "!");
		vcc_NextToken(tl);
		ExpectErr(tl, CSTR);
		p = vcc_regexp(tl);
		ERRCHK(tl);
		vcc_NextToken(tl);
		Fb(tl, 1, "%s, %s)\n", a1, p);
		break;
	case T_LEQ:
	case T_GEQ:
	case '>':
	case '<':
		vcc_inval_test(tl, "STRING", "'==', '!=', '~' and '!~'");
		break;
	case T_EQ:
	case T_NEQ:
		Fb(tl, 1, "%sVRT_strcmp(%s, ",
		    tl->t->tok == T_EQ ? "!" : "", a1);
		vcc_NextToken(tl);
		if (!vcc_StringVal(tl)) {
			vcc_ExpectedStringval(tl);
			break;
		}
		Fb(tl, 0, ")\n");
		break;
	default:
		Fb(tl, 1, "%s != (void*)0\n", a1);
		break;
	}
}

static void
vcc_Cond_Bool(const struct vcc *tl, const char *a1)
{

	Fb(tl, 1, "%s\n", a1);
}

static void
vcc_Cond_Backend(struct vcc *tl, const char *a1)
{

	Fb(tl, 1, "%s\n", a1);
	if (tl->t->tok == T_EQ || tl->t->tok == T_NEQ) {
		Fb(tl, 1, "  %.*s\n", PF(tl->t));
	} else {
		vcc_inval_test(tl, "BACKEND", "'==' and '!='");
		return;
	}
	vcc_NextToken(tl);
	vcc_ExpectCid(tl);
	ERRCHK(tl);
	vcc_AddRef(tl, tl->t, R_BACKEND);
	Fb(tl, 1, "VGCDIR(_%.*s)\n", PF(tl->t));
	vcc_NextToken(tl);
}

static void
vcc_Cond_Num(struct vcc *tl, enum var_type fmt, const char *fmtn,
    const char *a1)
{

	Fb(tl, 1, "%s ", a1);
	switch (tl->t->tok) {
	case T_EQ:
	case T_NEQ:
	case T_LEQ:
	case T_GEQ:
	case '>':
	case '<':
		Fb(tl, 0, "%.*s\n", PF(tl->t));
		vcc_NextToken(tl);
		vcc_Expr(tl, fmt);
		break;
	default:
		vcc_inval_test(tl, fmtn,
		    "'==', '!=', '<', '>', '<=' and '>='");
		break;
	}
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    Cond_3:
 *	Typed_Expr Relation Compat_Typed_Expr
 *    Typed_Expr:
 *	VarName
 *	FuncCall
 *    Relation:
 *	Subset('==', '!=', '<', '<=', '>', '>=', '~', '!~')
 *    Compat_Typed_Expr
 *	Typed_Expr
 *	Typed_Const
 *
 * Since we cannot tell if "10 s" is a TIME or DURATION type, or for that
 * matter if "127.0.0.1" is a STRING or IP type, we demand that the expression
 * before the relational operator provides us with a type which can be used to
 * guide parsing of other expression.
 */

static void
vcc_Cond_3(struct vcc *tl)
{
	const struct var *vp;
	const struct symbol *sym;
	const char *left;

	sym = VCC_FindSymbol(tl, tl->t);
	if (sym == NULL) {
		vsb_printf(tl->sb,
		    "Syntax error in condition.\n"
		    "Expected '(', '!' or variable name.\n"
		    "Found ");
		vcc_ErrToken(tl, tl->t);
		vsb_printf(tl->sb, "\n");
		vcc_ErrWhere(tl, tl->t);
		return;
	}
	vcc_AddUses(tl, tl->t, sym->r_methods, "Not available");
	AN(sym->var);
	vp = vcc_FindVar(tl, tl->t, 0, "cannot be read");
	ERRCHK(tl);
	assert(vp != NULL);
	left = vp->rname;
	vcc_NextToken(tl);

	switch (vp->fmt) {
	case BACKEND:	L(tl, vcc_Cond_Backend(tl, left)); break;
	case BOOL:	L(tl, vcc_Cond_Bool(tl, left)); break;
	case DURATION:	L(tl, vcc_Cond_Num(tl, DURATION, "DURATION", left)); break;
	case INT:	L(tl, vcc_Cond_Num(tl, INT, "INT", left)); break;
	case IP:	L(tl, vcc_Cond_Ip(tl, left)); break;
	case STRING:	L(tl, vcc_Cond_String(tl, left)); break;
	case TIME:	L(tl, vcc_Cond_Num(tl, TIME, "TIME", left)); break;
	default:
		vsb_printf(tl->sb,
		    "Variable '%s'"
		    " has no conditions that can be checked\n",
		    vp->name);
		vcc_ErrWhere(tl, tl->t);
		return;
	}
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    Cond_2:
 *	'!'? '(' Conditional ')'
 *	'!'? Cond_3
 */

static void
vcc_Cond_2(struct vcc *tl)
{

	C(tl, ",");
	if (tl->t->tok == '!') {
		Fb(tl, 1, "!");
		vcc_NextToken(tl);
	}
	if (tl->t->tok == '(') {
		vcc_Conditional(tl);
		return;
	}
	if (tl->t->tok == ID) {
		Fb(tl, 1, "(\n");
		vcc_Cond_3(tl);
		Fb(tl, 1, ")\n");
		return;
	}
	vsb_printf(tl->sb,
	    "Syntax error in condition.\n"
	    "Expected '(', '!' or variable name.\n"
	    "Found ");
	vcc_ErrToken(tl, tl->t);
	vsb_printf(tl->sb, "\n");
	vcc_ErrWhere(tl, tl->t);
	return;
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    Cond_1:
 *	Cond_2 { '&&' Cond_2 }*
 */

static void
vcc_Cond_1(struct vcc *tl)
{

	Fb(tl, 1, "(\n");
	L(tl, vcc_Cond_2(tl));
	while (tl->t->tok == T_CAND) {
		vcc_NextToken(tl);
		Fb(tl, 1, ") && (\n");
		L(tl, vcc_Cond_2(tl));
		ERRCHK(tl);
	}
	Fb(tl, 1, ")\n");
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    Cond_0:
 *	Cond_1 { '||' Cond_1 }*
 */

static void
vcc_Cond_0(struct vcc *tl)
{

	Fb(tl, 1, "(\n");
	L(tl, vcc_Cond_1(tl));
	while (tl->t->tok == T_COR) {
		vcc_NextToken(tl);
		Fb(tl, 1, ") || (\n");
		L(tl, vcc_Cond_1(tl));
		ERRCHK(tl);
	}
	Fb(tl, 1, ")\n");
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    Conditional:
 *	'(' Cond_0 ')'
 */

static void
vcc_Conditional(struct vcc *tl)
{

	SkipToken(tl, '(');
	Fb(tl, 1, "(\n");
	L(tl, vcc_Cond_0(tl));
	ERRCHK(tl);
	Fb(tl, 1, ")\n");
	SkipToken(tl, ')');
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    IfStmt:
 *	'if' Conditional  Compound Branch1* Branch2
 *    Branch1:
 *      'elseif' Conditional Compound
 *    Branch2:
 *      'else' Compound
 *	null
 */

static void
vcc_IfStmt(struct vcc *tl)
{

	SkipToken(tl, T_IF);
	Fb(tl, 1, "if \n");
	L(tl, vcc_Conditional(tl));
	ERRCHK(tl);
	L(tl, vcc_Compound(tl));
	ERRCHK(tl);
	while (1) {
		switch (tl->t->tok) {
		case T_ELSE:
			vcc_NextToken(tl);
			if (tl->t->tok != T_IF) {
				Fb(tl, 1, "else \n");
				L(tl, vcc_Compound(tl));
				ERRCHK(tl);
				return;
			}
			/* FALLTHROUGH */
		case T_ELSEIF:
		case T_ELSIF:
			Fb(tl, 1, "else if \n");
			vcc_NextToken(tl);
			L(tl, vcc_Conditional(tl));
			ERRCHK(tl);
			L(tl, vcc_Compound(tl));
			ERRCHK(tl);
			break;
		default:
			C(tl, ";");
			return;
		}
	}
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    Compound:
 *	'{' Stmt* '}'
 *
 *    Stmt:
 *	Compound
 *	IfStmt
 *	CSRC
 *	Id(Action) (XXX)
 */

static void
vcc_Compound(struct vcc *tl)
{
	int i;

	SkipToken(tl, '{');
	Fb(tl, 1, "{\n");
	tl->indent += INDENT;
	C(tl, ";");
	while (1) {
		ERRCHK(tl);
		switch (tl->t->tok) {
		case '{':
			vcc_Compound(tl);
			break;
		case T_IF:
			vcc_IfStmt(tl);
			break;
		case '}':
			vcc_NextToken(tl);
			tl->indent -= INDENT;
			Fb(tl, 1, "}\n");
			return;
		case CSRC:
			Fb(tl, 1, "%.*s\n",
			    tl->t->e - (tl->t->b + 2),
			    tl->t->b + 1);
			vcc_NextToken(tl);
			break;
		case EOI:
			vsb_printf(tl->sb,
			    "End of input while in compound statement\n");
			tl->err = 1;
			return;
		case ID:
			i = vcc_ParseAction(tl);
			ERRCHK(tl);
			if (i) {
				SkipToken(tl, ';');
				break;
			}
			/* FALLTHROUGH */
		default:
			/* We deliberately do not mention inline C */
			vsb_printf(tl->sb,
			    "Expected an action, 'if', '{' or '}'\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}
	}
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    Function:
 *	'sub' ID(name) Compound
 */

static void
vcc_Function(struct vcc *tl)
{
	int m;

	vcc_NextToken(tl);
	ExpectErr(tl, ID);

	m = IsMethod(tl->t);
	if (m != -1) {
		assert(m < VCL_MET_MAX);
		tl->fb = tl->fm[m];
		if (tl->mprocs[m] == NULL) {
			tl->mprocs[m] = vcc_AddProc(tl, tl->t);
			vcc_AddDef(tl, tl->t, R_SUB);
			vcc_AddRef(tl, tl->t, R_SUB);
		}
		tl->curproc = tl->mprocs[m];
		Fb(tl, 1, "  /* ... from ");
		vcc_Coord(tl, tl->fb, NULL);
		Fb(tl, 0, " */\n");
	} else {
		tl->fb = tl->fc;
		tl->curproc = vcc_AddProc(tl, tl->t);
		vcc_AddDef(tl, tl->t, R_SUB);
		Fh(tl, 0, "static int VGC_function_%.*s (struct sess *sp);\n",
		    PF(tl->t));
		Fc(tl, 1, "\nstatic int\n");
		Fc(tl, 1, "VGC_function_%.*s (struct sess *sp)\n", PF(tl->t));
	}
	vcc_NextToken(tl);
	tl->indent += INDENT;
	Fb(tl, 1, "{\n");
	L(tl, vcc_Compound(tl));
	if (m == -1) {
		/*
		 * non-method subroutines must have an explicit non-action
		 * return in case they just fall through the bottom.
		 */
		Fb(tl, 1, "  return(0);\n");
	}
	Fb(tl, 1, "}\n");
	tl->indent -= INDENT;
	tl->fb = NULL;
	tl->curproc = NULL;
}

/*--------------------------------------------------------------------
 * Top level of parser, recognize:
 *	Inline C-code
 *	ACL definitions
 *	Function definitions
 *	Backend & Director definitions
 *	End of input
 */

typedef void parse_f(struct vcc *tl);

static struct toplev {
	const char	*name;
	parse_f		*func;
} toplev[] = {
	{ "acl",		vcc_Acl },
	{ "sub",		vcc_Function },
	{ "backend",		vcc_ParseDirector },
	{ "director",		vcc_ParseDirector },
	{ "probe",		vcc_ParseProbe },
	{ NULL, NULL }
};

void
vcc_Parse(struct vcc *tl)
{
	struct toplev *tp;

	while (tl->t->tok != EOI) {
		ERRCHK(tl);
		switch (tl->t->tok) {
		case CSRC:
			Fc(tl, 0, "%.*s\n",
			    tl->t->e - (tl->t->b + 4), tl->t->b + 2);
			vcc_NextToken(tl);
			break;
		case EOI:
			break;
		case ID:
			for (tp = toplev; tp->name != NULL; tp++) {
				if (!vcc_IdIs(tl->t, tp->name))
					continue;
				tp->func(tl);
				break;
			}
			if (tp->name != NULL)
				break;
			/* FALLTHROUGH */
		default:
			/* We deliberately do not mention inline-C */
			vsb_printf(tl->sb, "Expected one of\n\t");
			for (tp = toplev; tp->name != NULL; tp++) {
				if (tp[1].name == NULL)
					vsb_printf(tl->sb, " or ");
				vsb_printf(tl->sb, "'%s'", tp->name);
				if (tp[1].name != NULL)
					vsb_printf(tl->sb, ", ");
			}
			vsb_printf(tl->sb, "\nFound: ");
			vcc_ErrToken(tl, tl->t);
			vsb_printf(tl->sb, " at\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}
	}
}
