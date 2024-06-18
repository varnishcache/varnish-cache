/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 * This file parses the real action of the VCL code, the procedure
 * statements which do the actual work.
 */

#include "config.h"

#include <string.h>

#include "vcc_compile.h"
#include "libvcc.h"

/*--------------------------------------------------------------------*/

static void v_matchproto_(sym_act_f)
vcc_act_call(struct vcc *tl, struct token *t, struct symbol *sym)
{
	struct token *t0;
	unsigned u;

	(void)t;
	ExpectErr(tl, ID);
	t0 = tl->t;
	sym = VCC_SymbolGet(tl, SYM_MAIN, SYM_NONE, SYMTAB_PARTIAL_NOERR,
	    XREF_REF);
	if (sym == NULL) {
		sym = VCC_SymbolGet(tl, SYM_MAIN, SYM_SUB, SYMTAB_CREATE,
		    XREF_REF);
		if (sym == NULL)
			return;
		VCC_GlobalSymbol(sym, SUB);
	}

	if (sym->kind == SYM_SUB) {
		vcc_AddCall(tl, t0, sym);

		Fb(tl, 1, "%s(ctx, VSUB_STATIC, NULL);\n", sym->lname);
		SkipToken(tl, ';');
		return;
	}

	tl->t = t0;
	u = tl->unique++;

	Fb(tl, 1, "{\n");
	tl->indent += INDENT;
	Fb(tl, 2, "VCL_SUB call_%u =\n", u);
	tl->indent += INDENT;
	vcc_Expr(tl, SUB);
	Fb(tl, 2, ";\n\n");
	SkipToken(tl, ';');
	tl->indent -= INDENT;
	Fb(tl, 2, "if (call_%u == NULL) {\n", u);
	Fb(tl, 2, "  VRT_fail(ctx, \"Tried to call NULL SUB near"
	   " source %%u line %%u\",\n");
	Fb(tl, 2, "    VGC_ref[%u].source,\n", tl->cnt);
	Fb(tl, 2, "    VGC_ref[%u].line);\n", tl->cnt);
	Fb(tl, 2, "  END_;\n");
	Fb(tl, 2, "}\n");
	Fb(tl, 2, "call_%u->func(ctx, VSUB_STATIC, NULL);\n", u);
	tl->indent -= INDENT;
	Fb(tl, 1, "}\n");
}

/*--------------------------------------------------------------------*/

static const struct assign {
	vcc_type_t		type;
	unsigned		oper;
	vcc_type_t		want;
	const char		*expr;
} assign[] = {
	{ INT,		T_INCR,		INT, "\v + " },
	{ INT,		T_DECR,		INT, "\v - " },
	{ INT,		T_MUL,		INT, "\v * " },
	{ INT,		T_DIV,		INT, "\v / " },
	{ INT,		'=',		INT },
	{ INT,		0,		INT },
	{ TIME,		T_INCR,		DURATION, "\v + " },
	{ TIME,		T_DECR,		DURATION, "\v - " },
	{ TIME,		T_MUL,		REAL, "\v * " },
	{ TIME,		T_DIV,		REAL, "\v / " },
	{ TIME,		'=',		TIME },
	{ TIME,		0,		TIME },
	{ DURATION,	T_INCR,		DURATION, "\v + " },
	{ DURATION,	T_DECR,		DURATION, "\v - " },
	{ DURATION,	T_MUL,		REAL, "\v * " },
	{ DURATION,	T_DIV,		REAL, "\v / " },
	{ DURATION,	'=',		DURATION },
	{ DURATION,	0,		DURATION },
	{ STRING,	T_INCR,		STRANDS, "\v,\n" },
	{ STRING,	'=',		STRANDS, "0,\n" },
	{ HEADER,	T_INCR,		STRANDS, "VRT_GetHdr(ctx, \v),\n" },
	{ HEADER,	'=',		STRANDS, "0,\n" },
	{ BODY,		'=',		BODY, "LBODY_SET_" },
	{ BODY,		T_INCR,		BODY, "LBODY_ADD_" },
	{ VOID,		'=',		VOID }
};

static void
vcc_assign_expr(struct vcc *tl, struct symbol *sym, const struct assign *ap)
{
	const char *e;
	unsigned indent = 1;

	e = ap->expr;
	if (e == NULL)
		return;

	while (*e != '\0') {
		if (*e == '\v')
			Fb(tl, indent, "%s", sym->rname);
		else
			Fb(tl, indent, "%c", *e);
		indent = 0;
		e++;
	}
}

/*--------------------------------------------------------------------*/

static void v_matchproto_(sym_act_f)
vcc_act_set(struct vcc *tl, struct token *t, struct symbol *sym)
{
	const struct assign *ap;
	vcc_type_t type;

	(void)t;
	ExpectErr(tl, ID);
	t = tl->t;
	sym = VCC_SymbolGet(tl, SYM_MAIN, SYM_VAR, SYMTAB_EXISTING, XREF_NONE);
	ERRCHK(tl);
	AN(sym);
	vcc_AddUses(tl, t, tl->t, sym, XREF_WRITE);
	ERRCHK(tl);
	type = sym->type;
	for (ap = assign; ap->type != VOID; ap++) {
		if (ap->type != type)
			continue;
		if (ap->oper != tl->t->tok)
			continue;
		vcc_NextToken(tl);
		type = ap->want;
		break;
	}

	if (ap->type == VOID)
		SkipToken(tl, ap->oper);

	Fb(tl, 1, "%s\n", sym->lname);
	tl->indent += INDENT;
	vcc_assign_expr(tl, sym, ap);
	vcc_Expr(tl, type);
	ERRCHK(tl);
	tl->indent -= INDENT;
	Fb(tl, 1, ");\n");
	SkipToken(tl, ';');
}

/*--------------------------------------------------------------------*/

static void v_matchproto_(sym_act_f)
vcc_act_unset(struct vcc *tl, struct token *t, struct symbol *sym)
{

	/* XXX: Wrong, should use VCC_Expr(HEADER) */
	ExpectErr(tl, ID);
	t = tl->t;
	sym = VCC_SymbolGet(tl, SYM_MAIN, SYM_VAR, SYMTAB_EXISTING, XREF_NONE);
	ERRCHK(tl);
	AN(sym);
	if (sym->u_methods == 0) {
		vcc_ErrWhere2(tl, t, tl->t);
		VSB_cat(tl->sb, "Variable cannot be unset.\n");
		return;
	}
	vcc_AddUses(tl, t, tl->t, sym, XREF_UNSET);
	Fb(tl, 1, "%s;\n", sym->uname);
	SkipToken(tl, ';');
}

/*--------------------------------------------------------------------*/

static void v_matchproto_(sym_act_f)
vcc_act_ban(struct vcc *tl, struct token *t, struct symbol *sym)
{

	(void)t;
	(void)sym;

	SkipToken(tl, '(');

	Fb(tl, 1, "(void) VRT_ban_string(ctx, \n");
	tl->indent += INDENT;
	vcc_Expr(tl, STRING);
	tl->indent -= INDENT;
	ERRCHK(tl);
	Fb(tl, 1, ");\n");

	SkipToken(tl, ')');
	SkipToken(tl, ';');
}

/*--------------------------------------------------------------------*/

static void v_matchproto_(sym_act_f)
vcc_act_hash_data(struct vcc *tl, struct token *t, struct symbol *sym)
{

	(void)t;
	(void)sym;
	SkipToken(tl, '(');

	Fb(tl, 1, "VRT_hashdata(ctx,\n  ");
	vcc_Expr(tl, STRANDS);
	ERRCHK(tl);
	Fb(tl, 1, ");\n");
	SkipToken(tl, ')');
	SkipToken(tl, ';');
}

/*--------------------------------------------------------------------*/

static void
vcc_act_return_pass(struct vcc *tl)
{

	SkipToken(tl, '(');
	Fb(tl, 1, "VRT_hit_for_pass(ctx,\n");
	tl->indent += INDENT;
	vcc_Expr(tl, DURATION);
	ERRCHK(tl);
	SkipToken(tl, ')');
	Fb(tl, 1, ");\n");
	tl->indent -= INDENT;
}
/*--------------------------------------------------------------------*/

static void
vcc_act_return_fail(struct vcc *tl)
{
	SkipToken(tl, '(');
	Fb(tl, 1, "VRT_fail(ctx,\n");
	tl->indent += INDENT;
	vcc_Expr(tl, STRING);
	tl->indent -= INDENT;
	ERRCHK(tl);
	SkipToken(tl, ')');
	Fb(tl, 1, ");\n");
}

/*--------------------------------------------------------------------*/

static void
vcc_act_return_synth(struct vcc *tl)
{

	SkipToken(tl, '(');
	Fb(tl, 1, "VRT_synth(ctx,\n");
	tl->indent += INDENT;
	vcc_Expr(tl, INT);
	ERRCHK(tl);
	Fb(tl, 1, ",\n");
	if (tl->t->tok == ',') {
		vcc_NextToken(tl);
		vcc_Expr(tl, STRING);
		ERRCHK(tl);
	} else {
		Fb(tl, 1, "(const char*)0\n");
	}
	tl->indent -= INDENT;
	SkipToken(tl, ')');
	Fb(tl, 1, ");\n");
}

/*--------------------------------------------------------------------*/

static void
vcc_act_return_vcl(struct vcc *tl)
{
	struct symbol *sym;
	struct inifin *p;
	char buf[1024];

	SkipToken(tl, '(');
	ExpectErr(tl, ID);
	sym = VCC_SymbolGet(tl, SYM_MAIN, SYM_VCL, SYMTAB_EXISTING, XREF_NONE);
	ERRCHK(tl);
	AN(sym);
	if (sym->eval_priv == NULL) {
		VSB_cat(tl->symtab, ",\n    {\n");
		VSB_cat(tl->symtab, "\t\"dir\": \"import\",\n");
		VSB_cat(tl->symtab, "\t\"type\": \"$VCL\",\n");
		VSB_printf(tl->symtab, "\t\"name\": \"%s\"\n", sym->name);
		VSB_cat(tl->symtab, "   }");

		bprintf(buf, "vgc_vcl_%u", tl->unique++);
		sym->eval_priv = strdup(buf);
		AN(sym->eval_priv);

		Fh(tl, 0, "static VCL_VCL %s;", buf);
		Fh(tl, 0, "\t/* VCL %s */\n", sym->name);

		p = New_IniFin(tl);
		AN(p);
		VSB_printf(p->ini, "\t%s = VPI_vcl_get(ctx, \"%s\");",
		    buf, sym->name);
		VSB_printf(p->fin, "\tVPI_vcl_rel(ctx, %s);",
		    buf);
	}
	Fb(tl, 1, "VPI_vcl_select(ctx, %s);\t/* %s */\n",
	    (const char*)sym->eval_priv, sym->name);
	SkipToken(tl, ')');
}

/*--------------------------------------------------------------------*/

static void
vcc_act_return_retry(struct vcc *tl)
{
	unsigned is_task = 0;

	ExpectErr(tl, '(');
	vcc_NextToken(tl);
	if (vcc_IdIs(tl->t, "task"))
		is_task = 1;
	else if (!vcc_IdIs(tl->t, "fetch")) {
		VSB_printf(tl->sb, "Expected \"task\" or \"fetch\" retry.\n");
		vcc_ErrWhere(tl, tl->t);
	}
	ERRCHK(tl);
	Fb(tl, 1, "VPI_retry(ctx, %u);\n", is_task);
	SkipToken(tl, ID);
	SkipToken(tl, ')');
}

/*--------------------------------------------------------------------*/

static void v_matchproto_(sym_act_f)
vcc_act_return(struct vcc *tl, struct token *t, struct symbol *sym)
{
	unsigned hand, mask;
	const char *h;

	(void)t;
	(void)sym;
	AN(tl->curproc);
	if (tl->t->tok == ';' && tl->curproc->method == NULL) {
		SkipToken(tl, ';');
		Fb(tl, 1, "return;\n");
		return;
	}
	SkipToken(tl, '(');
	ExpectErr(tl, ID);

	hand = VCL_RET_MAX;
	h = NULL;
	mask = 0;
#define VCL_RET_MAC(l, U, B)				\
		if (vcc_IdIs(tl->t, #l)) {		\
			hand = VCL_RET_ ## U;		\
			h = #U;				\
			mask = (B);			\
		}
#include "tbl/vcl_returns.h"
	if (h == NULL) {
		VSB_cat(tl->sb, "Expected return action name.\n");
		vcc_ErrWhere(tl, tl->t);
		ERRCHK(tl);
	}
	assert(hand < VCL_RET_MAX);
	AN(mask);

	vcc_ProcAction(tl->curproc, hand, mask, tl->t);
	vcc_NextToken(tl);
	if (tl->t->tok == '(') {
		if (hand == VCL_RET_SYNTH || hand == VCL_RET_ERROR)
			vcc_act_return_synth(tl);
		else if (hand == VCL_RET_VCL)
			vcc_act_return_vcl(tl);
		else if (hand == VCL_RET_PASS)
			vcc_act_return_pass(tl);
		else if (hand == VCL_RET_RETRY)
			vcc_act_return_retry(tl);
		else if (hand == VCL_RET_FAIL)
			vcc_act_return_fail(tl);
		else {
			VSB_cat(tl->sb, "Arguments not allowed.\n");
			vcc_ErrWhere(tl, tl->t);
		}
	} else {
		if (hand == VCL_RET_SYNTH || hand == VCL_RET_VCL) {
			VSB_cat(tl->sb, "Missing argument.\n");
			vcc_ErrWhere(tl, tl->t);
		}
	}
	ERRCHK(tl);
	Fb(tl, 1, "END_;\n");
	if (hand == VCL_RET_FAIL)
		Fb(tl, 1, "VRT_fail(ctx, \"Failed from VCL\");\n");
	else
		Fb(tl, 1, "VRT_handling(ctx, VCL_RET_%s);\n", h);
	SkipToken(tl, ')');
	SkipToken(tl, ';');
}

/*--------------------------------------------------------------------*/

static void v_matchproto_(sym_act_f)
vcc_act_synthetic(struct vcc *tl, struct token *t, struct symbol *sym)
{

	(void)t;
	(void)sym;
	ExpectErr(tl, '(');
	ERRCHK(tl);
	vcc_NextToken(tl);

	Fb(tl, 1, "VRT_synth_strands(ctx, ");
	vcc_Expr(tl, STRANDS);
	ERRCHK(tl);
	Fb(tl, 1, ");\n");

	SkipToken(tl, ')');
	SkipToken(tl, ';');
}

/*--------------------------------------------------------------------*/

// The pp[] trick is to make the length of #name visible to flexelint.
#define ACT(name, func, mask)						\
	do {								\
		const char pp[] = #name;				\
		sym = VCC_MkSym(tl, pp, SYM_MAIN, SYM_ACTION, VCL_LOW,	\
		    VCL_HIGH);						\
		AN(sym);						\
		sym->action = func;					\
		sym->action_mask = (mask);				\
	} while (0)

void
vcc_Action_Init(struct vcc *tl)
{
	struct symbol *sym;

	ACT(ban,	vcc_act_ban,	0);
	ACT(call,	vcc_act_call,	0);
	ACT(hash_data,	vcc_act_hash_data,
		VCL_MET_HASH);
	ACT(if,		vcc_Act_If,	0);
	ACT(new,	vcc_Act_New,
		VCL_MET_INIT);
	ACT(return,	vcc_act_return,	0);
	ACT(set,	vcc_act_set,	0);
	ACT(synthetic,	vcc_act_synthetic,
		VCL_MET_SYNTH | VCL_MET_BACKEND_ERROR);
	ACT(unset,	vcc_act_unset,	0);
}
