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

	(void)t;
	ExpectErr(tl, ID);
	sym = VCC_SymbolGet(tl, SYM_SUB, SYMTAB_CREATE, XREF_REF);
	AN(sym);
	vcc_AddCall(tl, sym);
	VCC_GlobalSymbol(sym, SUB, "VGC_function");
	Fb(tl, 1, "%s(ctx);\n", sym->rname);
	SkipToken(tl, ';');
}

/*--------------------------------------------------------------------*/

static const struct arith {
	vcc_type_t		type;
	unsigned		oper;
	vcc_type_t		want;
} arith[] = {
	{ INT,		T_INCR,		INT },
	{ INT,		T_DECR,		INT },
	{ INT,		T_MUL,		INT },
	{ INT,		T_DIV,		INT },
	{ INT,		'=',		INT },
	{ INT,		0,		INT },
	{ TIME,		T_INCR,		DURATION },
	{ TIME,		T_DECR,		DURATION },
	{ TIME,		T_MUL,		REAL },
	{ TIME,		T_DIV,		REAL },
	{ TIME,		'=',		TIME },
	{ TIME,		0,		TIME },
	{ DURATION,	T_INCR,		DURATION },
	{ DURATION,	T_DECR,		DURATION },
	{ DURATION,	T_MUL,		REAL },
	{ DURATION,	T_DIV,		REAL },
	{ DURATION,	'=',		DURATION },
	{ DURATION,	0,		DURATION },
	{ VOID,		'=',		VOID }
};


/*--------------------------------------------------------------------*/

static void v_matchproto_(sym_act_f)
vcc_act_set(struct vcc *tl, struct token *t, struct symbol *sym)
{
	const struct arith *ap;
	vcc_type_t type;

	(void)t;
	ExpectErr(tl, ID);
	t = tl->t;
	sym = VCC_SymbolGet(tl, SYM_VAR, "Unknown variable", XREF_NONE);
	ERRCHK(tl);
	AN(sym);
	if (sym->w_methods == 0) {
		vcc_ErrWhere2(tl, t, tl->t);
		if (sym->r_methods != 0)
			VSB_printf(tl->sb, "Variable is read only.\n");
		else
			VSB_printf(tl->sb, "Variable cannot be set.\n");
		return;
	}
	vcc_AddUses(tl, t, tl->t, sym->w_methods, "Cannot be set");
	Fb(tl, 1, "%s\n", sym->lname);
	tl->indent += INDENT;
	type = sym->type;
	for (ap = arith; ap->type != VOID; ap++) {
		if (ap->type != type)
			continue;
		if (ap->oper != tl->t->tok)
			continue;
		if (ap->oper != '=')
			Fb(tl, 1, "%s %c ", sym->rname, *tl->t->b);
		vcc_NextToken(tl);
		type = ap->want;
		break;
	}
	if (ap->type == VOID)
		SkipToken(tl, ap->oper);
	if (type == HEADER) {
		vcc_Expr(tl, STRING_LIST);
	} else if (type == STRING) {
		vcc_Expr(tl, STRING_LIST);
	} else if (type == BODY) {
		vcc_Expr(tl, STRING_LIST);
	} else {
		vcc_Expr(tl, type);
	}
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
	sym = VCC_SymbolGet(tl, SYM_VAR, "Unknown variable", XREF_NONE);
	ERRCHK(tl);
	AN(sym);
	if (sym->u_methods == 0) {
		vcc_ErrWhere2(tl, t, tl->t);
		VSB_printf(tl->sb, "Variable cannot be unset.\n");
		return;
	}
	vcc_AddUses(tl, t, tl->t, sym->u_methods, "Cannot be unset");
	Fb(tl, 1, "%s;\n", sym->uname);
	SkipToken(tl, ';');
}

/*--------------------------------------------------------------------*/

static void v_matchproto_(sym_act_f)
vcc_act_ban(struct vcc *tl, struct token *t, struct symbol *sym)
{

	(void)t;
	(void)sym;

	ExpectErr(tl, '(');
	vcc_NextToken(tl);

	Fb(tl, 1, "VRT_ban_string(ctx, \n");
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
	vcc_Expr(tl, STRING_LIST);
	ERRCHK(tl);
	Fb(tl, 1, ");\n");
	SkipToken(tl, ')');
	SkipToken(tl, ';');
}

/*--------------------------------------------------------------------*/

static void
vcc_act_return_pass(struct vcc *tl)
{

	ExpectErr(tl, '(');
	vcc_NextToken(tl);
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
vcc_act_return_synth(struct vcc *tl)
{

	ExpectErr(tl, '(');
	vcc_NextToken(tl);
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

	ExpectErr(tl, '(');
	vcc_NextToken(tl);
	ExpectErr(tl, ID);
	sym = VCC_SymbolGet(tl, SYM_VCL, "Not a VCL label", XREF_NONE);
	ERRCHK(tl);
	AN(sym);
	if (sym->eval_priv == NULL) {
		VSB_printf(tl->fi, "%s VCL %s */\n", VCC_INFO_PREFIX,
		    sym->name);

		bprintf(buf, "vgc_vcl_%u", tl->unique++);
		sym->eval_priv = strdup(buf);
		AN(sym->eval_priv);

		Fh(tl, 0, "static VCL_VCL %s;", buf);
		Fh(tl, 0, "\t/* VCL %s */\n", sym->name);

		p = New_IniFin(tl);
		AN(p);
		VSB_printf(p->ini, "\t%s = VRT_vcl_get(ctx, \"%s\");",
		    buf, sym->name);
		VSB_printf(p->fin, "\tVRT_vcl_rel(ctx, %s);",
		    buf);
	}
	Fb(tl, 1, "VRT_vcl_select(ctx, %s);\t/* %s */\n",
	    (const char*)sym->eval_priv, sym->name);
	SkipToken(tl, ')');
}

/*--------------------------------------------------------------------*/

static void v_matchproto_(sym_act_f)
vcc_act_return(struct vcc *tl, struct token *t, struct symbol *sym)
{
	unsigned hand;
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
#define VCL_RET_MAC(l, U, B)				\
		if (vcc_IdIs(tl->t, #l)) {		\
			hand = VCL_RET_ ## U;		\
			h = #U;				\
		}
#include "tbl/vcl_returns.h"
	if (h == NULL) {
		VSB_printf(tl->sb, "Expected return action name.\n");
		vcc_ErrWhere(tl, tl->t);
		ERRCHK(tl);
	}
	assert(hand < VCL_RET_MAX);

	vcc_ProcAction(tl->curproc, hand, tl->t);
	vcc_NextToken(tl);
	if (tl->t->tok == '(') {
		if (hand == VCL_RET_SYNTH)
			vcc_act_return_synth(tl);
		else if (hand == VCL_RET_VCL)
			vcc_act_return_vcl(tl);
		else if (hand == VCL_RET_PASS)
			vcc_act_return_pass(tl);
		else {
			VSB_printf(tl->sb, "Arguments not allowed.\n");
			vcc_ErrWhere(tl, tl->t);
		}
	} else {
		if (hand == VCL_RET_SYNTH || hand == VCL_RET_VCL) {
			VSB_printf(tl->sb, "Missing argument.\n");
			vcc_ErrWhere(tl, tl->t);
		}
	}
	ERRCHK(tl);
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

	Fb(tl, 1, "VRT_synth_page(ctx, ");
	vcc_Expr(tl, STRING_LIST);
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
		sym = VCC_MkSym(tl, pp, SYM_ACTION, VCL_LOW, VCL_HIGH);	\
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
