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
 * This file contains code for two cross-reference or consistency checks.
 *
 * The first check is simply that all subroutine, acls and backends are
 * both defined and referenced.  Complaints about referenced but undefined
 * or defined but unreferenced objects will be emitted.
 *
 * The second check recursively descends through subroutine calls to make
 * sure that action actions are correct for the methods through which
 * they are called.
 */

#include "config.h"

#include <string.h>
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
	const struct symbol	*sym;
	const struct xrefuse	*use;
	unsigned		mask;
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
		if (!tl->err_unref)
			vcc_Warn(tl);
	}
}

int
vcc_CheckReferences(struct vcc *tl)
{

	VCC_WalkSymbols(tl, vcc_checkref, SYM_MAIN, SYM_NONE);
	return (tl->err);
}

/*--------------------------------------------------------------------
 * Returns checks
 */

const struct xrefuse XREF_READ[1] = {{"xref_read", "Not available"}};
const struct xrefuse XREF_WRITE[1] = {{"xref_write", "Cannot be set"}};
const struct xrefuse XREF_UNSET[1] = {{"xref_unset", "Cannot be unset"}};
const struct xrefuse XREF_ACTION[1] = {{"xref_action", "Not a valid action"}};

void
vcc_AddUses(struct vcc *tl, const struct token *t1, const struct token *t2,
    const struct symbol *sym, const struct xrefuse *use)
{
	struct procuse *pu;

	AN(tl->curproc);
	pu = TlAlloc(tl, sizeof *pu);
	AN(pu);
	AN(sym);
	AN(use);
	AN(use->name);
	pu->t1 = t1;
	pu->t2 = t2;
	if (pu->t2 == NULL) {
		pu->t2 = vcc_PeekTokenFrom(tl, t1);
		AN(pu->t2);
	}
	pu->sym = sym;
	pu->use = use;
	pu->fm = tl->curproc;

	if (pu->use == XREF_READ)
		pu->mask = sym->r_methods;
	else if (pu->use == XREF_WRITE)
		pu->mask = sym->w_methods;
	else if (pu->use == XREF_UNSET)
		pu->mask = sym->u_methods;
	else if (pu->use == XREF_ACTION)
		pu->mask = sym->action_mask;
	else
		WRONG("wrong xref use");

	VTAILQ_INSERT_TAIL(&tl->curproc->uses, pu, list);
}

void
vcc_AddCall(struct vcc *tl, struct token *t, struct symbol *sym)
{
	struct proccall *pc;

	AN(sym);
	pc = TlAlloc(tl, sizeof *pc);
	AN(pc);
	pc->sym = sym;
	pc->t = t;
	pc->fm = tl->curproc;
	VTAILQ_INSERT_TAIL(&tl->curproc->calls, pc, list);
}

void
vcc_ProcAction(struct proc *p, unsigned returns, unsigned mask, struct token *t)
{

	assert(returns < VCL_RET_MAX);
	p->ret_bitmap |= (1U << returns);
	p->okmask &= mask;
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
		VSB_cat(tl->sb, "Subroutine recurses on\n");
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

	// more references than calls -> sub is referenced for dynamic calls
	u = (p->sym->nref > p->called);

	p->active = 1;
	VTAILQ_FOREACH(pc, &p->calls, list) {
		if (pc->sym->proc == NULL) {
			VSB_printf(tl->sb, "Subroutine %s does not exist\n",
			    pc->sym->name);
			vcc_ErrWhere(tl, pc->t);
			return (1);
		}
		pc->sym->proc->calledfrom |= p->calledfrom;
		pc->sym->proc->called++;
		pc->sym->nref += u;
		if (vcc_CheckActionRecurse(tl, pc->sym->proc, bitmap)) {
			VSB_printf(tl->sb, "\n...called from \"%.*s\"\n",
			    PF(p->name));
			vcc_ErrWhere(tl, pc->t);
			return (1);
		}
		p->okmask &= pc->sym->proc->okmask;
	}
	p->active = 0;
	return (0);
}

/*--------------------------------------------------------------------*/

static void
vcc_checkaction(struct vcc *tl, const struct symbol *sym)
{
	struct proc *p;
	unsigned bitmap;

	p = sym->proc;
	AN(p);
	AN(p->name);

	if (p->method == NULL) {
		bitmap = ~0U;
	} else {
		bitmap = p->method->ret_bitmap;
		p->calledfrom = p->method->bitval;
	}

	if (! vcc_CheckActionRecurse(tl, p, bitmap))
		return;

	tl->err = 1;
	if (p->method == NULL)
		return;

	VSB_printf(tl->sb,
		   "\n...which is the \"%s\" subroutine\n", p->method->name);
	VSB_cat(tl->sb, "Legal returns are:");
#define VCL_RET_MAC(l, U, B)						\
	if (p->method->ret_bitmap & ((1 << VCL_RET_##U)))		\
		VSB_printf(tl->sb, " \"%s\"", #l);

#include "tbl/vcl_returns.h"
	VSB_cat(tl->sb, "\n");
}

int
vcc_CheckAction(struct vcc *tl)
{

	VCC_WalkSymbols(tl, vcc_checkaction, SYM_MAIN, SYM_SUB);
	return (tl->err);
}

/*--------------------------------------------------------------------*/

static struct procuse *
vcc_illegal_write(struct vcc *tl, struct procuse *pu, const struct method *m)
{

	if (pu->mask || pu->use != XREF_WRITE)
		return (NULL);

	if (pu->sym->r_methods == 0) {
		vcc_ErrWhere2(tl, pu->t1, pu->t2);
		VSB_cat(tl->sb, "Variable cannot be set.\n");
		return (NULL);
	}

	if (!(pu->sym->r_methods & m->bitval)) {
		pu->use = XREF_READ; /* NB: change the error message. */
		return (pu);
	}

	vcc_ErrWhere2(tl, pu->t1, pu->t2);
	VSB_cat(tl->sb, "Variable is read only.\n");
	return (NULL);
}

static struct procuse *
vcc_FindIllegalUse(struct vcc *tl, struct proc *p, const struct method *m)
{
	struct procuse *pu, *pw, *r = NULL;

	VTAILQ_FOREACH(pu, &p->uses, list) {
		p->okmask &= pu->mask;
		if (m == NULL)
			continue;
		pw = vcc_illegal_write(tl, pu, m);
		if (r != NULL)
			continue;
		if (tl->err)
			r = pw;
		else if (!(pu->mask & m->bitval))
			r = pu;
	}
	return (r);
}

static int
vcc_CheckUseRecurse(struct vcc *tl, struct proc *p,
    const struct method *m)
{
	struct proccall *pc;
	struct procuse *pu;

	pu = vcc_FindIllegalUse(tl, p, m);
	if (pu != NULL) {
		vcc_ErrWhere2(tl, pu->t1, pu->t2);
		VSB_printf(tl->sb, "%s from subroutine '%s'.\n",
		    pu->use->err, m->name);
		VSB_printf(tl->sb, "\n...in subroutine \"%.*s\"\n",
		    PF(pu->fm->name));
		vcc_ErrWhere(tl, p->name);
		return (1);
	}
	if (tl->err)
		return (1);
	VTAILQ_FOREACH(pc, &p->calls, list) {
		if (vcc_CheckUseRecurse(tl, pc->sym->proc, m)) {
			VSB_printf(tl->sb, "\n...called from \"%.*s\"\n",
			    PF(p->name));
			vcc_ErrWhere(tl, pc->t);
			return (1);
		}
		p->okmask &= pc->sym->proc->okmask;
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
	pu = vcc_FindIllegalUse(tl, p, p->method);
	if (pu != NULL) {
		vcc_ErrWhere2(tl, pu->t1, pu->t2);
		VSB_printf(tl->sb, "%s in subroutine '%.*s'.",
		    pu->use->err, PF(p->name));
		VSB_cat(tl->sb, "\nAt: ");
		return;
	}
	ERRCHK(tl);
	if (vcc_CheckUseRecurse(tl, p, p->method)) {
		VSB_printf(tl->sb,
		    "\n...which is the \"%s\" subroutine\n", p->method->name);
		return;
	}
}

/*
 * Used from a second symbol walk because vcc_checkuses is more precise for
 * subroutines called from methods. We catch here subs used for dynamic calls
 * and with vcc_err_unref = off
 */
static void
vcc_checkpossible(struct vcc *tl, const struct symbol *sym)
{
	struct proc *p;

	p = sym->proc;
	AN(p);

	if (p->okmask != 0)
		return;

	VSB_cat(tl->sb, "Impossible Subroutine");
	vcc_ErrWhere(tl, p->name);
}

int
vcc_CheckUses(struct vcc *tl)
{

	VCC_WalkSymbols(tl, vcc_checkuses, SYM_MAIN, SYM_SUB);
	if (tl->err)
		return (tl->err);
	VCC_WalkSymbols(tl, vcc_checkpossible, SYM_MAIN, SYM_SUB);
	return (tl->err);
}

/*---------------------------------------------------------------------*/

static void v_matchproto_(symwalk_f)
vcc_instance_info(struct vcc *tl, const struct symbol *sym)
{

	CHECK_OBJ_NOTNULL(sym, SYMBOL_MAGIC);
	AN(sym->rname);
	Fc(tl, 0, "\t{ .p = (uintptr_t *)&%s, .name = \"", sym->rname);
	VCC_SymName(tl->fc, sym);
	Fc(tl, 0, "\" },\n");
}

void
VCC_InstanceInfo(struct vcc *tl)
{
	Fc(tl, 0, "\nstatic const struct vpi_ii VGC_instance_info[] = {\n");
	VCC_WalkSymbols(tl, vcc_instance_info, SYM_MAIN, SYM_INSTANCE);
	Fc(tl, 0, "\t{ .p = NULL, .name = \"\" }\n");
	Fc(tl, 0, "};\n");
}

/*---------------------------------------------------------------------*/

static int sym_type_len;

static void v_matchproto_(symwalk_f)
vcc_xreftable_len(struct vcc *tl, const struct symbol *sym)
{
	int len;

	(void)tl;
	CHECK_OBJ_NOTNULL(sym, SYMBOL_MAGIC);
	CHECK_OBJ_NOTNULL(sym->type, TYPE_MAGIC);
	len = strlen(sym->type->name);
	if (sym_type_len < len)
		sym_type_len = len;
}

static void v_matchproto_(symwalk_f)
vcc_xreftable(struct vcc *tl, const struct symbol *sym)
{

	CHECK_OBJ_NOTNULL(sym, SYMBOL_MAGIC);
	CHECK_OBJ_NOTNULL(sym->kind, KIND_MAGIC);
	CHECK_OBJ_NOTNULL(sym->type, TYPE_MAGIC);
	Fc(tl, 0, " * %-8s ", sym->kind->name);
	Fc(tl, 0, " %-*s ", sym_type_len, sym->type->name);
	Fc(tl, 0, " %2u %2u ", sym->lorev, sym->hirev);
	VCC_SymName(tl->fc, sym);
	if (sym->wildcard != NULL)
		Fc(tl, 0, "*");
	Fc(tl, 0, "\n");
}

void
VCC_XrefTable(struct vcc *tl)
{

#define VCC_NAMESPACE(U, l)						\
	Fc(tl, 0, "\n/*\n * Symbol Table " #U "\n *\n");		\
	sym_type_len = 0;						\
	VCC_WalkSymbols(tl, vcc_xreftable_len, SYM_##U, SYM_NONE);	\
	VCC_WalkSymbols(tl, vcc_xreftable, SYM_##U, SYM_NONE);		\
	Fc(tl, 0, "*/\n\n");
#include "vcc_namespace.h"
}
