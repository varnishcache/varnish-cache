/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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

#include <stdio.h>
#include <string.h>

#include "vcc_compile.h"

/*--------------------------------------------------------------------*/

static void vcc_Compound(struct vcc *tl);

/*--------------------------------------------------------------------*/

#define L(tl, foo)	do {	\
	tl->indent += INDENT;	\
	foo;			\
	tl->indent -= INDENT;	\
} while (0)

#define C(tl, sep)	do {					\
	Fb(tl, 1, "VRT_count(ctx, %u)%s\n", ++tl->cnt, sep);	\
	tl->t->cnt = tl->cnt;					\
} while (0)

/*--------------------------------------------------------------------
 * SYNTAX:
 *    Conditional:
 *	'(' Cond_0 ')'
 */

static void
vcc_Conditional(struct vcc *tl)
{

	SkipToken(tl, '(');
	Fb(tl, 0, "(\n");
	L(tl, vcc_Expr(tl, BOOL));
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

	SkipToken(tl, ID);
	Fb(tl, 1, "if ");
	vcc_Conditional(tl);
	ERRCHK(tl);
	L(tl, vcc_Compound(tl));
	ERRCHK(tl);
	while (tl->t->tok == ID) {
		if (vcc_IdIs(tl->t, "else")) {
			vcc_NextToken(tl);
			if (tl->t->tok == '{') {
				Fb(tl, 1, "else\n");
				L(tl, vcc_Compound(tl));
				ERRCHK(tl);
				return;
			}
			if (tl->t->tok != ID || !vcc_IdIs(tl->t, "if")) {
				VSB_printf(tl->sb,
				    "'else' must be followed by 'if' or '{'\n");
				vcc_ErrWhere(tl, tl->t);
				return;
			}
			Fb(tl, 1, "else if ");
			vcc_NextToken(tl);
			vcc_Conditional(tl);
			ERRCHK(tl);
			L(tl, vcc_Compound(tl));
			ERRCHK(tl);
		} else if (vcc_IdIs(tl->t, "elseif") ||
		     vcc_IdIs(tl->t, "elsif") ||
		     vcc_IdIs(tl->t, "elif")) {
			Fb(tl, 1, "else if ");
			vcc_NextToken(tl);
			vcc_Conditional(tl);
			ERRCHK(tl);
			L(tl, vcc_Compound(tl));
			ERRCHK(tl);
		} else {
			break;
		}
	}
	C(tl, ";");
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
		case '}':
			vcc_NextToken(tl);
			tl->indent -= INDENT;
			Fb(tl, 1, "}\n");
			return;
		case CSRC:
			if (tl->allow_inline_c) {
				Fb(tl, 1, "%.*s\n",
				    (int) (tl->t->e - (tl->t->b + 2)),
				    tl->t->b + 1);
				vcc_NextToken(tl);
			} else {
				VSB_printf(tl->sb,
				    "Inline-C not allowed\n");
				vcc_ErrWhere(tl, tl->t);
			}
			break;
		case EOI:
			VSB_printf(tl->sb,
			    "End of input while in compound statement\n");
			tl->err = 1;
			return;
		case ID:
			if (vcc_IdIs(tl->t, "if")) {
				vcc_IfStmt(tl);
				break;
			} else {
				i = vcc_ParseAction(tl);
				ERRCHK(tl);
				if (i) {
					SkipToken(tl, ';');
					break;
				}
			}
			/* FALLTHROUGH */
		default:
			/* We deliberately do not mention inline C */
			VSB_printf(tl->sb,
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
vcc_ParseFunction(struct vcc *tl)
{
	int m, i;

	vcc_NextToken(tl);
	ExpectErr(tl, ID);
	if (!vcc_isCid(tl->t)) {
		VSB_printf(tl->sb,
		    "Names of VCL sub's cannot contain '-'\n");
		vcc_ErrWhere(tl, tl->t);
		return;
	}

	m = IsMethod(tl->t);
	if (m == -2) {
		VSB_printf(tl->sb,
		    "VCL sub's named 'vcl*' are reserved names.\n");
		vcc_ErrWhere(tl, tl->t);
		VSB_printf(tl->sb, "Valid vcl_* methods are:\n");
		for (i = 0; method_tab[i].name != NULL; i++)
			VSB_printf(tl->sb, "\t%s\n", method_tab[i].name);
		return;
	} else if (m != -1) {
		assert(m < VCL_MET_MAX);
		tl->fb = tl->fm[m];
		if (tl->mprocs[m] == NULL) {
			(void)vcc_AddDef(tl, tl->t, SYM_SUB);
			vcc_AddRef(tl, tl->t, SYM_SUB);
			tl->mprocs[m] = vcc_AddProc(tl, tl->t);
		}
		tl->curproc = tl->mprocs[m];
		Fb(tl, 1, "  /* ... from ");
		vcc_Coord(tl, tl->fb, NULL);
		Fb(tl, 0, " */\n");
	} else {
		tl->fb = tl->fc;
		i = vcc_AddDef(tl, tl->t, SYM_SUB);
		if (i > 1) {
			VSB_printf(tl->sb,
			    "Function %.*s redefined\n", PF(tl->t));
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		tl->curproc = vcc_AddProc(tl, tl->t);
		Fh(tl, 0, "int VGC_function_%.*s "
		    "(const struct vrt_ctx *ctx);\n", PF(tl->t));
		Fc(tl, 1, "\nint __match_proto__(vcl_func_t)\n");
		Fc(tl, 1, "VGC_function_%.*s(const struct vrt_ctx *ctx)\n",
		    PF(tl->t));
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
 */

static void
vcc_ParseDirector(struct vcc *tl)
{
	VSB_printf(tl->sb, "\ndirectors are now in directors VMOD.\n");
	vcc_ErrWhere(tl, tl->t);
}

/*--------------------------------------------------------------------
 */

static void
vcc_ParseVcl(struct vcc *tl)
{
	double ver;
	struct token *tok;

	assert(vcc_IdIs(tl->t, "vcl"));
	vcc_NextToken(tl);
	tok = tl->t;
	ver = vcc_DoubleVal(tl);
	ERRCHK(tl);
	if (ver != 4.0) {
		VSB_printf(tl->sb, "VCL version %.1f not supported.\n", ver);
		vcc_ErrWhere(tl, tok);
		ERRCHK(tl);
	}
	ExpectErr(tl, ';');
	vcc_NextToken(tl);
}

/*--------------------------------------------------------------------
 * Top level of parser, recognize:
 *	Inline C-code
 *	ACL definitions
 *	Function definitions
 *	Backend definitions
 *	VMOD import directives
 *	VCL version declarations
 *	End of input
 */

typedef void parse_f(struct vcc *tl);

static struct toplev {
	const char	*name;
	parse_f		*func;
} toplev[] = {
	{ "acl",		vcc_ParseAcl },
	{ "sub",		vcc_ParseFunction },
	{ "backend",		vcc_ParseBackend },
	{ "director",		vcc_ParseDirector },
	{ "probe",		vcc_ParseProbe },
	{ "import",		vcc_ParseImport },
	{ "vcl",		vcc_ParseVcl },
	{ NULL, NULL }
};

void
vcc_Parse(struct vcc *tl)
{
	struct toplev *tp;

	if (!vcc_IdIs(tl->t, "vcl")) {
		VSB_printf(tl->sb,
		    "VCL version declaration missing\n"
		    "Update your VCL to Version 4 syntax, and add\n"
		    "\tvcl 4.0;\n"
		    "on the first line the VCL files.\n"
		);
		vcc_ErrWhere(tl, tl->t);
		ERRCHK(tl);
	}
	vcc_ParseVcl(tl);
	ERRCHK(tl);
	while (tl->t->tok != EOI) {
		ERRCHK(tl);
		switch (tl->t->tok) {
		case CSRC:
			if (tl->allow_inline_c) {
				Fc(tl, 0, "%.*s\n",
				    (int) (tl->t->e - (tl->t->b + 4)),
				    tl->t->b + 2);
				vcc_NextToken(tl);
			} else {
				VSB_printf(tl->sb,
				    "Inline-C not allowed\n");
				vcc_ErrWhere(tl, tl->t);
			}
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
			VSB_printf(tl->sb, "Expected one of\n\t");
			for (tp = toplev; tp->name != NULL; tp++) {
				if (tp[1].name == NULL)
					VSB_printf(tl->sb, " or ");
				VSB_printf(tl->sb, "'%s'", tp->name);
				if (tp[1].name != NULL)
					VSB_printf(tl->sb, ", ");
			}
			VSB_printf(tl->sb, "\nFound: ");
			vcc_ErrToken(tl, tl->t);
			VSB_printf(tl->sb, " at\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}
	}
}
