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

#include "config.h"

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

void v_matchproto_(sym_act_f)
vcc_Act_If(struct vcc *tl, struct token *t, struct symbol *sym)
{

	(void)t;
	(void)sym;
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
	struct symbol *sym;
	struct token *t;

	SkipToken(tl, '{');
	Fb(tl, 1, "{\n");
	tl->indent += INDENT;
	C(tl, ";");
	while (1) {
		ERRCHK(tl);
		t = tl->t;
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
			sym = VCC_SymbolGet(tl, SYM_NONE, SYMTAB_NOERR,
			    XREF_NONE);
			if (sym != NULL && sym->action != NULL) {
				if (sym->action_mask != 0)
					vcc_AddUses(tl, t, NULL,
					    sym->action_mask,
					    "Not a valid action");
				sym->action(tl, t, sym);
				break;
			}
			/* FALLTHROUGH */
		default:
			/* We deliberately do not mention inline C */
			VSB_printf(tl->sb,
			    "Expected an action, 'if', '{' or '}'\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		Fb(tl, 1, "if (*ctx->handling) return;\n");
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
	struct symbol *sym;
	struct token *t;
	struct proc *p;

	vcc_NextToken(tl);
	vcc_ExpectVid(tl, "function");
	ERRCHK(tl);

	t = tl->t;
	sym = VCC_SymbolGet(tl, SYM_SUB, SYMTAB_CREATE, XREF_DEF);
	ERRCHK(tl);
	AN(sym);
	p = sym->proc;
	if (p == NULL) {
		if ((t->b[0] == 'v'|| t->b[0] == 'V') &&
		    (t->b[1] == 'c'|| t->b[1] == 'C') &&
		    (t->b[2] == 'l'|| t->b[2] == 'L')) {
			VSB_printf(tl->sb,
			    "The names 'vcl*' are reserved for subroutines.\n");
			vcc_ErrWhere(tl, t);
			VSB_printf(tl->sb, "Valid vcl_* subroutines are:\n");
			VTAILQ_FOREACH(p, &tl->procs, list) {
				if (p->method != NULL)
					VSB_printf(tl->sb, "\t%s\n",
					    p->method->name);
			}
			return;
		}
		VCC_GlobalSymbol(sym, SUB, "VGC_function");
		p = vcc_NewProc(tl, sym);
		p->name = t;
		VSB_printf(p->cname, "%s", sym->rname);
	} else if (p->method == NULL) {
		VSB_printf(tl->sb, "Function '%s' redefined\n", sym->name);
		vcc_ErrWhere(tl, t);
		VSB_printf(tl->sb, "Previously defined here:\n");
		vcc_ErrWhere(tl, p->name);
		return;
	} else {
		/* Add to VCL sub */
		AN(p->method);
		if (p->name == NULL)
			p->name = t;
	}
	CHECK_OBJ_NOTNULL(p, PROC_MAGIC);
	tl->fb = p->body;
	Fb(tl, 1, "  /* ... from ");
	vcc_Coord(tl, p->body, NULL);
	Fb(tl, 0, " */\n");
	tl->curproc = p;
	tl->indent += INDENT;
	Fb(tl, 1, "{\n");
	L(tl, vcc_Compound(tl));
	Fb(tl, 1, "}\n");
	tl->indent -= INDENT;
	tl->fb = NULL;
	tl->curproc = NULL;
}

/*--------------------------------------------------------------------
 */

static void
vcc_ParseVcl(struct vcc *tl)
{
	struct token *tok0, *tok1, *tok2;
	int syntax;

	assert(vcc_IdIs(tl->t, "vcl"));
	tok0 = tl->t;
	vcc_NextToken(tl);

	tok1 = tl->t;
	Expect(tl, CNUM);
	syntax = (*tl->t->b - '0') * 10;
	vcc_NextToken(tl);
	Expect(tl, '.');
	vcc_NextToken(tl);

	Expect(tl, CNUM);
	tok2 = tl->t;
	syntax += (*tl->t->b - '0');
	vcc_NextToken(tl);

	if (tok1->e - tok1->b != 1 || tok2->e - tok2->b != 1) {
		VSB_printf(tl->sb,
		    "Don't play silly buggers with VCL version numbers\n");
		vcc_ErrWhere2(tl, tok0, tl->t);
		ERRCHK(tl);
	}

	if (syntax < VCL_LOW || syntax > VCL_HIGH) {
		VSB_printf(tl->sb, "VCL version %.1f not supported.\n",
		    .1 * syntax);
		vcc_ErrWhere2(tl, tok0, tl->t);
		ERRCHK(tl);
	}

	if (tl->t->tok != ';') {
		/* Special handling, because next token might be 'vcl'
		 * in the built-in VCL, and that would give a very
		 * confusing error message
		 */
		VSB_printf(tl->sb,
		    "Expected 'vcl N.N;' found no semi-colon\n");
		vcc_ErrWhere2(tl, tok0, tl->t);
		ERRCHK(tl);
	}
	vcc_NextToken(tl);
	if (tl->syntax == 0)
		tl->syntax = syntax;
	if (syntax > tl->syntax) {
		VSB_printf(tl->sb,
		    "VCL version %.1f higher than"
		    " the top level version %.1f\n",
		    .1 * syntax, .1 * tl->syntax);
		vcc_ErrWhere2(tl, tok0, tl->t);
		ERRCHK(tl);
	}
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
	unsigned	vcllo;
	unsigned	vclhi;
} toplev[] = {
	{ "acl",		vcc_ParseAcl,		VCL_41,	VCL_HIGH },
	{ "sub",		vcc_ParseFunction,	VCL_41,	VCL_HIGH },
	{ "backend",		vcc_ParseBackend,	VCL_41,	VCL_HIGH },
	{ "probe",		vcc_ParseProbe,		VCL_41,	VCL_HIGH },
	{ "import",		vcc_ParseImport,	VCL_41,	VCL_HIGH },
	{ "vcl",		vcc_ParseVcl,		VCL_41,	VCL_HIGH },
	{ "default",		NULL,			VCL_41, VCL_HIGH },
	{ NULL, NULL }
};

void
vcc_Parse(struct vcc *tl)
{
	struct toplev *tp;

	AZ(tl->indent);
	if (tl->t->tok != ID || !vcc_IdIs(tl->t, "vcl")) {
		VSB_printf(tl->sb,
		    "VCL version declaration missing\n"
		    "Update your VCL to Version 4 syntax, and add\n"
		    "\tvcl 4.1;\n"
		    "on the first line of the VCL files.\n"
		);
		vcc_ErrWhere(tl, tl->t);
		ERRCHK(tl);
	}
	vcc_ParseVcl(tl);
	ERRCHK(tl);
	AN(tl->syntax);
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
				if (tp->func == NULL)
					continue;
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
	AZ(tl->indent);
}

void
vcc_Parse_Init(struct vcc *tl)
{
	struct toplev *tp;

	for (tp = toplev; tp->name != NULL; tp++)
		AN(VCC_MkSym(tl, tp->name, SYM_NONE, tp->vcllo, tp->vclhi));
}
