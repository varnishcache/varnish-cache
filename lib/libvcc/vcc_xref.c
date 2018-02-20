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
 * This file contains code for two cross-reference or consistency checks.
 *
 * The first check is simply that all subroutine, acls and backends are
 * both defined and referenced.  Complaints about referenced but undefined
 * or defined but unreferenced objects will be emitted.
 *
 * The second check recursively decends through subroutine calls to make
 * sure that action actions are correct for the methods through which
 * they are called.
 */

#include "config.h"

#include "vcc_compile.h"

/*--------------------------------------------------------------------*/

struct proccall {
	VTAILQ_ENTRY(proccall)	list;
	struct symbol		*sym;
	struct token		*t;
	struct proc		*fm;
};

struct procuse {
	VTAILQ_ENTRY(procuse)	list;
	const struct token	*t1;
	const struct token	*t2;
	unsigned		mask;
	const char		*use;
	struct proc		*fm;
};

/*--------------------------------------------------------------------*/

static void
vcc_checkref(struct vcc *tl, const struct symbol *sym)
{

	if (sym->noref)
		return;
	if (sym->ndef == 0 && sym->nref != 0) {
		AN(sym->ref_b);
		VSB_printf(tl->sb, "Undefined %s %.*s, first reference:\n",
		    sym->kind->name, PF(sym->ref_b));
		vcc_ErrWhere(tl, sym->ref_b);
	} else if (sym->ndef != 0 && sym->nref == 0) {
		AN(sym->def_b);
		VSB_printf(tl->sb, "Unused %s %.*s, defined:\n",
		    sym->kind->name, PF(sym->def_b));
		vcc_ErrWhere(tl, sym->def_b);
		if (!tl->err_unref) {
			VSB_printf(tl->sb, "(That was just a warning)\n");
			tl->err = 0;
		}
	}
}

int
vcc_CheckReferences(struct vcc *tl)
{

	VCC_WalkSymbols(tl, vcc_checkref, SYM_NONE);
	return (tl->err);
}

/*--------------------------------------------------------------------
 * Returns checks
 */

void
vcc_AddUses(struct vcc *tl, const struct token *t1, const struct token *t2,
    unsigned mask, const char *use)
{
	struct procuse *pu;

	if (tl->curproc == NULL)	/* backend */
		return;
	pu = TlAlloc(tl, sizeof *pu);
	assert(pu != NULL);
	pu->t1 = t1;
	pu->t2 = t2;
	if (pu->t2 == NULL)
		pu->t2 = VTAILQ_NEXT(t1, list);
	pu->mask = mask;
	pu->use = use;
	pu->fm = tl->curproc;
	VTAILQ_INSERT_TAIL(&tl->curproc->uses, pu, list);
}

void
vcc_AddCall(struct vcc *tl, struct symbol *sym)
{
	struct proccall *pc;

	AN(sym);
	pc = TlAlloc(tl, sizeof *pc);
	assert(pc != NULL);
	pc->sym = sym;
	pc->t = tl->t;
	pc->fm = tl->curproc;
	VTAILQ_INSERT_TAIL(&tl->curproc->calls, pc, list);
}

void
vcc_ProcAction(struct proc *p, unsigned returns, struct token *t)
{

	assert(returns < VCL_RET_MAX);
	p->ret_bitmap |= (1U << returns);
	/* Record the first instance of this return */
	if (p->return_tok[returns] == NULL)
		p->return_tok[returns] = t;
}

static int
vcc_CheckActionRecurse(struct vcc *tl, struct proc *p, unsigned bitmap)
{
	unsigned u;
	struct proccall *pc;

	AN(p);
	if (p->active) {
		VSB_printf(tl->sb, "Function recurses on\n");
		vcc_ErrWhere(tl, p->name);
		return (1);
	}
	u = p->ret_bitmap & ~bitmap;
	if (u) {

#define VCL_RET_MAC(l, U, B)						\
		if (u & (1 << (VCL_RET_##U))) {				\
			VSB_printf(tl->sb, "Invalid return \"" #l "\"\n");\
			vcc_ErrWhere(tl, p->return_tok[VCL_RET_##U]);	\
		}
#include "tbl/vcl_returns.h"

		VSB_printf(tl->sb, "\n...in subroutine \"%.*s\"\n",
		    PF(p->name));
		vcc_ErrWhere(tl, p->name);
		return (1);
	}
	p->active = 1;
	VTAILQ_FOREACH(pc, &p->calls, list) {
		if (pc->sym->proc == NULL) {
			VSB_printf(tl->sb, "Function %s does not exist\n",
			    pc->sym->name);
			vcc_ErrWhere(tl, pc->t);
			return (1);
		}
		if (vcc_CheckActionRecurse(tl, pc->sym->proc, bitmap)) {
			VSB_printf(tl->sb, "\n...called from \"%s\"\n",
			    pc->sym->name);
			vcc_ErrWhere(tl, pc->t);
			return (1);
		}
	}
	p->active = 0;
	p->called++;
	return (0);
}

/*--------------------------------------------------------------------*/

static void
vcc_checkaction1(struct vcc *tl, const struct symbol *sym)
{
	struct proc *p;

	p = sym->proc;
	AN(p);
	AN(p->name);
	if (p->method == NULL)
		return;
	if (vcc_CheckActionRecurse(tl, p, p->method->ret_bitmap)) {
		VSB_printf(tl->sb,
		    "\n...which is the \"%s\" method\n", p->method->name);
		VSB_printf(tl->sb, "Legal returns are:");
#define VCL_RET_MAC(l, U, B)						\
		if (p->method->ret_bitmap & ((1 << VCL_RET_##U)))	\
			VSB_printf(tl->sb, " \"%s\"", #l);

#include "tbl/vcl_returns.h"
		VSB_printf(tl->sb, "\n");
		tl->err = 1;
	}

}

static void
vcc_checkaction2(struct vcc *tl, const struct symbol *sym)
{
	struct proc *p;

	p = sym->proc;
	AN(p);

	if (p->called)
		return;
	VSB_printf(tl->sb, "Function unused\n");
	vcc_ErrWhere(tl, p->name);
	if (!tl->err_unref) {
		VSB_printf(tl->sb, "(That was just a warning)\n");
		tl->err = 0;
	}
}

int
vcc_CheckAction(struct vcc *tl)
{

	VCC_WalkSymbols(tl, vcc_checkaction1, SYM_SUB);
	if (tl->err)
		return (tl->err);
	VCC_WalkSymbols(tl, vcc_checkaction2, SYM_SUB);
	return (tl->err);
}

/*--------------------------------------------------------------------*/

static struct procuse *
vcc_FindIllegalUse(const struct proc *p, const struct method *m)
{
	struct procuse *pu;

	VTAILQ_FOREACH(pu, &p->uses, list)
		if (!(pu->mask & m->bitval))
			return (pu);
	return (NULL);
}

static int
vcc_CheckUseRecurse(struct vcc *tl, const struct proc *p,
    const struct method *m)
{
	struct proccall *pc;
	struct procuse *pu;

	pu = vcc_FindIllegalUse(p, m);
	if (pu != NULL) {
		vcc_ErrWhere2(tl, pu->t1, pu->t2);
		VSB_printf(tl->sb, "%s from method '%s'.\n",
		    pu->use, m->name);
		VSB_printf(tl->sb, "\n...in subroutine \"%.*s\"\n",
		    PF(pu->fm->name));
		vcc_ErrWhere(tl, p->name);
		return (1);
	}
	VTAILQ_FOREACH(pc, &p->calls, list) {
		if (vcc_CheckUseRecurse(tl, pc->sym->proc, m)) {
			VSB_printf(tl->sb, "\n...called from \"%.*s\"\n",
			    PF(pc->fm->name));
			vcc_ErrWhere(tl, pc->t);
			return (1);
		}
	}
	return (0);
}

static void
vcc_checkuses(struct vcc *tl, const struct symbol *sym)
{
	struct proc *p;
	struct procuse *pu;

	p = sym->proc;
	AN(p);
	if (p->method == NULL)
		return;
	pu = vcc_FindIllegalUse(p, p->method);
	if (pu != NULL) {
		vcc_ErrWhere2(tl, pu->t1, pu->t2);
		VSB_printf(tl->sb, "%s in method '%.*s'.",
		    pu->use, PF(p->name));
		VSB_cat(tl->sb, "\nAt: ");
		return;
	}
	if (vcc_CheckUseRecurse(tl, p, p->method)) {
		VSB_printf(tl->sb,
		    "\n...which is the \"%s\" method\n", p->method->name);
		return;
	}
}

int
vcc_CheckUses(struct vcc *tl)
{

	VCC_WalkSymbols(tl, vcc_checkuses, SYM_SUB);
	return (tl->err);
}

/*---------------------------------------------------------------------*/

static void
vcc_pnam(struct vcc *tl, const struct symbol *sym)
{

	if (sym->parent != tl->symbols) {
		vcc_pnam(tl, sym->parent);
		Fc(tl, 0, ".");
	}
	Fc(tl, 0, "%s", sym->name);
}

static void v_matchproto_(symwalk_f)
vcc_xreftable(struct vcc *tl, const struct symbol *sym)
{

	CHECK_OBJ_NOTNULL(sym, SYMBOL_MAGIC);
	CHECK_OBJ_NOTNULL(sym->kind, KIND_MAGIC);
	CHECK_OBJ_NOTNULL(sym->type, TYPE_MAGIC);
	Fc(tl, 0, " * %-8s ", sym->kind->name);
	Fc(tl, 0, " %-9s ", sym->type->name);
	Fc(tl, 0, " %2u %2u ", sym->lorev, sym->hirev);
	vcc_pnam(tl, sym);
	if (sym->wildcard != NULL)
		Fc(tl, 0, "*");
	Fc(tl, 0, "\n");
}

void
VCC_XrefTable(struct vcc *tl)
{

	Fc(tl, 0, "\n/*\n * Symbol Table\n *\n");
	VCC_WalkSymbols(tl, vcc_xreftable, SYM_NONE);
	Fc(tl, 0, "*/\n\n");
}
