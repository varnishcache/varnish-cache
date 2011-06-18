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

#include <stdio.h>

#include "vsb.h"

#include "libvarnish.h"
#include "vcc_priv.h"
#include "vcc_compile.h"

/*--------------------------------------------------------------------*/

struct proccall {
	VTAILQ_ENTRY(proccall)	list;
	struct proc		*p;
	struct token		*t;
};

struct procuse {
	VTAILQ_ENTRY(procuse)	list;
	const struct token	*t;
	unsigned		mask;
	const char		*use;
};

struct proc {
	VTAILQ_HEAD(,proccall)	calls;
	VTAILQ_HEAD(,procuse)	uses;
	struct token		*name;
	unsigned		ret_bitmap;
	unsigned		exists;
	unsigned		called;
	unsigned		active;
	struct token		*return_tok[VCL_RET_MAX];
};

/*--------------------------------------------------------------------
 * Keep track of definitions and references
 */

void
vcc_AddRef(struct vcc *tl, const struct token *t, enum symkind kind)
{
	struct symbol *sym;

	sym = VCC_GetSymbolTok(tl, t, kind);
	AN(sym);
	sym->nref++;
}

int
vcc_AddDef(struct vcc *tl, const struct token *t, enum symkind kind)
{
	struct symbol *sym;

	sym = VCC_GetSymbolTok(tl, t, kind);
	AN(sym);
	sym->ndef++;
	return (sym->ndef);
}

/*--------------------------------------------------------------------*/

static void
vcc_checkref(struct vcc *tl, const struct symbol *sym)
{

	if (sym->ndef == 0 && sym->nref != 0) {
		VSB_printf(tl->sb, "Undefined %s %.*s, first reference:\n",
		    VCC_SymKind(tl, sym), PF(sym->def_b));
		vcc_ErrWhere(tl, sym->def_b);
	} else if (sym->ndef != 0 && sym->nref == 0) {
		VSB_printf(tl->sb, "Unused %s %.*s, defined:\n",
		    VCC_SymKind(tl, sym), PF(sym->def_b));
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

static struct proc *
vcc_findproc(struct vcc *tl, struct token *t)
{
	struct symbol *sym;
	struct proc *p;


	sym = VCC_GetSymbolTok(tl, t, SYM_SUB);
	AN(sym);
	if (sym->proc != NULL)
		return (sym->proc);

	p = TlAlloc(tl, sizeof *p);
	assert(p != NULL);
	VTAILQ_INIT(&p->calls);
	VTAILQ_INIT(&p->uses);
	p->name = t;
	sym->proc = p;
	return (p);
}

struct proc *
vcc_AddProc(struct vcc *tl, struct token *t)
{
	struct proc *p;

	p = vcc_findproc(tl, t);
	p->name = t;	/* make sure the name matches the definition */
	p->exists++;
	return (p);
}

void
vcc_AddUses(struct vcc *tl, const struct token *t, unsigned mask,
    const char *use)
{
	struct procuse *pu;

	if (tl->curproc == NULL)	/* backend */
		return;
	pu = TlAlloc(tl, sizeof *pu);
	assert(pu != NULL);
	pu->t = t;
	pu->mask = mask;
	pu->use = use;
	VTAILQ_INSERT_TAIL(&tl->curproc->uses, pu, list);
}

void
vcc_AddCall(struct vcc *tl, struct token *t)
{
	struct proccall *pc;
	struct proc *p;

	p = vcc_findproc(tl, t);
	pc = TlAlloc(tl, sizeof *pc);
	assert(pc != NULL);
	pc->p = p;
	pc->t = t;
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

	if (!p->exists) {
		VSB_printf(tl->sb, "Function %.*s does not exist\n",
		    PF(p->name));
		return (1);
	}
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
#include "vcl_returns.h"
#undef VCL_RET_MAC

		VSB_printf(tl->sb, "\n...in subroutine \"%.*s\"\n",
		    PF(p->name));
		vcc_ErrWhere(tl, p->name);
		return (1);
	}
	p->active = 1;
	VTAILQ_FOREACH(pc, &p->calls, list) {
		if (vcc_CheckActionRecurse(tl, pc->p, bitmap)) {
			VSB_printf(tl->sb, "\n...called from \"%.*s\"\n",
			    PF(p->name));
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
	struct method *m;
	int i;

	p = sym->proc;
	AN(p);
	i = IsMethod(p->name);
	if (i < 0)
		return;
	m = method_tab + i;
	if (vcc_CheckActionRecurse(tl, p, m->ret_bitmap)) {
		VSB_printf(tl->sb,
		    "\n...which is the \"%s\" method\n", m->name);
		VSB_printf(tl->sb, "Legal returns are:");
#define VCL_RET_MAC(l, U, B)						\
		if (m->ret_bitmap & ((1 << VCL_RET_##U)))	\
			VSB_printf(tl->sb, " \"%s\"", #l);

#include "vcl_returns.h"
#undef VCL_RET_MAC
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
    struct method *m)
{
	struct proccall *pc;
	struct procuse *pu;

	pu = vcc_FindIllegalUse(p, m);
	if (pu != NULL) {
		VSB_printf(tl->sb,
		    "'%.*s': %s from method '%.*s'.\n",
		    PF(pu->t), pu->use, PF(p->name));
		vcc_ErrWhere(tl, pu->t);
		VSB_printf(tl->sb, "\n...in subroutine \"%.*s\"\n",
		    PF(p->name));
		vcc_ErrWhere(tl, p->name);
		return (1);
	}
	VTAILQ_FOREACH(pc, &p->calls, list) {
		if (vcc_CheckUseRecurse(tl, pc->p, m)) {
			VSB_printf(tl->sb, "\n...called from \"%.*s\"\n",
			    PF(p->name));
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
	struct method *m;
	struct procuse *pu;
	int i;

	p = sym->proc;
	AN(p);

	i = IsMethod(p->name);
	if (i < 0)
		return;
	m = method_tab + i;
	pu = vcc_FindIllegalUse(p, m);
	if (pu != NULL) {
		VSB_printf(tl->sb,
		    "'%.*s': %s in method '%.*s'.",
		    PF(pu->t), pu->use, PF(p->name));
		VSB_cat(tl->sb, "\nAt: ");
		vcc_ErrWhere(tl, pu->t);
		return;
	}
	if (vcc_CheckUseRecurse(tl, p, m)) {
		VSB_printf(tl->sb,
		    "\n...which is the \"%s\" method\n", m->name);
		return;
	}
}

int
vcc_CheckUses(struct vcc *tl)
{

	VCC_WalkSymbols(tl, vcc_checkuses, SYM_SUB);
	return (tl->err);
}
