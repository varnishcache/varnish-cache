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
 * This file contains code for two cross-reference or consistency checks.
 *
 * The first check is simply that all functions, acls and backends are
 * both defined and referenced.  Complaints about referenced but undefined
 * or defined but unreferenced objects will be emitted.
 *
 * The second check recursively decends through function calls to make
 * sure that action actions are correct for the methods through which
 * they are called.
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

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
	struct token		*t;
	unsigned		mask;
	const char		*use;
};

struct proc {
	VTAILQ_ENTRY(proc)	list;
	VTAILQ_HEAD(,proccall)	calls;
	VTAILQ_HEAD(,procuse)	uses;
	struct token		*name;
	unsigned		ret_bitmap;
	unsigned		exists;
	unsigned		called;
	unsigned		active;
	struct token		*return_tok[VCL_RET_MAX];
};

/*--------------------------------------------------------------------*/

static const char *
vcc_typename(struct tokenlist *tl, const struct ref *r)
{
	switch (r->type) {
	case R_FUNC: return ("function");
	case R_ACL: return ("acl");
	case R_BACKEND: return ("backend");
	default:
		ErrInternal(tl);
		vsb_printf(tl->sb, "Ref ");
		vcc_ErrToken(tl, r->name);
		vsb_printf(tl->sb, " has unknown type %d\n",
		    r->type);
		return "?";
	}
}

/*--------------------------------------------------------------------
 * Keep track of definitions and references
 */

static struct ref *
vcc_findref(struct tokenlist *tl, struct token *t, enum ref_type type)
{
	struct ref *r;

	VTAILQ_FOREACH(r, &tl->refs, list) {
		if (r->type != type)
			continue;
		if (vcc_Teq(r->name, t))
			return (r);
	}
	r = TlAlloc(tl, sizeof *r);
	assert(r != NULL);
	r->name = t;
	r->type = type;
	VTAILQ_INSERT_TAIL(&tl->refs, r, list);
	return (r);
}

void
vcc_AddRef(struct tokenlist *tl, struct token *t, enum ref_type type)
{

	vcc_findref(tl, t, type)->refcnt++;
}

void
vcc_AddDef(struct tokenlist *tl, struct token *t, enum ref_type type)
{
	struct ref *r;
	const char *tp;

	r = vcc_findref(tl, t, type);
	if (r->defcnt > 0) {
		tp = vcc_typename(tl, r);
		vsb_printf(tl->sb, "Multiple definitions of %s \"%.*s\"\n",
		    tp, PF(t));
		vcc_ErrWhere(tl, r->name);
		vsb_printf(tl->sb, "...and\n");
		vcc_ErrWhere(tl, t);
	}
	r->defcnt++;
	r->name = t;
}

/*--------------------------------------------------------------------*/

int
vcc_CheckReferences(struct tokenlist *tl)
{
	struct ref *r;
	const char *type;
	int nerr = 0;

	VTAILQ_FOREACH(r, &tl->refs, list) {
		if (r->defcnt != 0 && r->refcnt != 0)
			continue;
		nerr++;

		type = vcc_typename(tl, r);

		if (r->defcnt == 0) {
			vsb_printf(tl->sb,
			    "Undefined %s %.*s, first reference:\n",
			    type, PF(r->name));
			vcc_ErrWhere(tl, r->name);
			continue;
		}

		vsb_printf(tl->sb, "Unused %s %.*s, defined:\n",
		    type, PF(r->name));
		vcc_ErrWhere(tl, r->name);
	}
	return (nerr);
}

/*--------------------------------------------------------------------
 * Returns checks
 */

static struct proc *
vcc_findproc(struct tokenlist *tl, struct token *t)
{
	struct proc *p;

	VTAILQ_FOREACH(p, &tl->procs, list)
		if (vcc_Teq(p->name, t))
			return (p);
	p = TlAlloc(tl, sizeof *p);
	assert(p != NULL);
	VTAILQ_INIT(&p->calls);
	VTAILQ_INIT(&p->uses);
	VTAILQ_INSERT_TAIL(&tl->procs, p, list);
	p->name = t;
	return (p);
}

struct proc *
vcc_AddProc(struct tokenlist *tl, struct token *t)
{
	struct proc *p;

	p = vcc_findproc(tl, t);
	p->name = t;	/* make sure the name matches the definition */
	p->exists++;
	return (p);
}

void
vcc_AddUses(struct tokenlist *tl, const struct token *t, unsigned mask,
    const char *use)
{
	struct procuse *pu;

	(void)t;
	if (tl->curproc == NULL)	/* backend */
		return;
	pu = TlAlloc(tl, sizeof *pu);
	assert(pu != NULL);
	pu->t = tl->t;
	pu->mask = mask;
	pu->use = use;
	VTAILQ_INSERT_TAIL(&tl->curproc->uses, pu, list);
}

void
vcc_AddCall(struct tokenlist *tl, struct token *t)
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
vcc_CheckActionRecurse(struct tokenlist *tl, struct proc *p, unsigned bitmap)
{
	unsigned u;
	struct proccall *pc;

	if (!p->exists) {
		vsb_printf(tl->sb, "Function %.*s does not exist\n",
		    PF(p->name));
		return (1);
	}
	if (p->active) {
		vsb_printf(tl->sb, "Function recurses on\n");
		vcc_ErrWhere(tl, p->name);
		return (1);
	}
	u = p->ret_bitmap & ~bitmap;
	if (u) {
/*lint -save -e525 -e539 */
#define VCL_RET_MAC(l, U)						\
		if (u & (1 << (VCL_RET_##U))) {				\
			vsb_printf(tl->sb, "Invalid return \"" #l "\"\n");\
			vcc_ErrWhere(tl, p->return_tok[VCL_RET_##U]);	\
		}
#include "vcl_returns.h"
#undef VCL_RET_MAC
/*lint -restore */
		vsb_printf(tl->sb, "\n...in function \"%.*s\"\n", PF(p->name));
		vcc_ErrWhere(tl, p->name);
		return (1);
	}
	p->active = 1;
	VTAILQ_FOREACH(pc, &p->calls, list) {
		if (vcc_CheckActionRecurse(tl, pc->p, bitmap)) {
			vsb_printf(tl->sb, "\n...called from \"%.*s\"\n",
			    PF(p->name));
			vcc_ErrWhere(tl, pc->t);
			return (1);
		}
	}
	p->active = 0;
	p->called++;
	return (0);
}

int
vcc_CheckAction(struct tokenlist *tl)
{
	struct proc *p;
	struct method *m;
	int i;

	VTAILQ_FOREACH(p, &tl->procs, list) {
		i = IsMethod(p->name);
		if (i < 0)
			continue;
		m = method_tab + i;
		if (vcc_CheckActionRecurse(tl, p, m->ret_bitmap)) {
			vsb_printf(tl->sb,
			    "\n...which is the \"%s\" method\n", m->name);
			vsb_printf(tl->sb, "Legal returns are:");
#define VCL_RET_MAC(l, U)						\
			if (m->ret_bitmap & ((1 << VCL_RET_##U)))	\
				vsb_printf(tl->sb, " \"%s\"", #l);
/*lint -save -e525 -e539 */
#include "vcl_returns.h"
/*lint +e525 */
#undef VCL_RET_MAC
/*lint -restore */
			vsb_printf(tl->sb, "\n");
			return (1);
		}
	}
	VTAILQ_FOREACH(p, &tl->procs, list) {
		if (p->called)
			continue;
		vsb_printf(tl->sb, "Function unused\n");
		vcc_ErrWhere(tl, p->name);
		return (1);
	}
	return (0);
}

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
vcc_CheckUseRecurse(struct tokenlist *tl, const struct proc *p,
    struct method *m)
{
	struct proccall *pc;
	struct procuse *pu;

	pu = vcc_FindIllegalUse(p, m);
	if (pu != NULL) {
		vsb_printf(tl->sb,
		    "'%.*s': %s not possible in method '%.*s'.\n",
		    PF(pu->t), pu->use, PF(p->name));
		vcc_ErrWhere(tl, pu->t);
		vsb_printf(tl->sb, "\n...in function \"%.*s\"\n",
		    PF(p->name));
		vcc_ErrWhere(tl, p->name);
		return (1);
	}
	VTAILQ_FOREACH(pc, &p->calls, list) {
		if (vcc_CheckUseRecurse(tl, pc->p, m)) {
			vsb_printf(tl->sb, "\n...called from \"%.*s\"\n",
			    PF(p->name));
			vcc_ErrWhere(tl, pc->t);
			return (1);
		}
	}
	return (0);
}

int
vcc_CheckUses(struct tokenlist *tl)
{
	struct proc *p;
	struct method *m;
	struct procuse *pu;
	int i;

	VTAILQ_FOREACH(p, &tl->procs, list) {
		i = IsMethod(p->name);
		if (i < 0)
			continue;
		m = method_tab + i;
		pu = vcc_FindIllegalUse(p, m);
		if (pu != NULL) {
			vsb_printf(tl->sb,
			    "'%.*s': %s not possible in method '%.*s'.",
			    PF(pu->t), pu->use, PF(p->name));
			vsb_cat(tl->sb, "\nAt: ");
			vcc_ErrWhere(tl, pu->t);
			return (1);
		}
		if (vcc_CheckUseRecurse(tl, p, m)) {
			vsb_printf(tl->sb,
			    "\n...which is the \"%s\" method\n", m->name);
			return (1);
		}
	}
	return (0);
}

