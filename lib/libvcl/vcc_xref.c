/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006 Linpro AS
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
 * $Id$
 *
 * This fine contains code for two cross-reference or consistency checks.
 *
 * The first check is simply that all functions, acls and backends are
 * both defined and referenced.  Complaints about referenced but undefined
 * or defined but unreferenced objects will be emitted.
 *
 * The second check recursively decends through function calls to make
 * sure that action actions are correct for the methods through which
 * they are called.
 */

#include <assert.h>
#include <stdio.h>

#include "vsb.h"

#include "vcc_priv.h"
#include "vcc_compile.h"

/*--------------------------------------------------------------------*/

struct proccall {
	TAILQ_ENTRY(proccall)	list;
	struct proc		*p;
	struct token		*t;
};

struct proc {
	TAILQ_ENTRY(proc)	list;
	TAILQ_HEAD(,proccall)	calls;
	struct token		*name;
	unsigned		returns;
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
		return "???";
	}
}

/*--------------------------------------------------------------------
 * Keep track of definitions and references
 */

static struct ref *
vcc_findref(struct tokenlist *tl, struct token *t, enum ref_type type)
{
	struct ref *r;

	TAILQ_FOREACH(r, &tl->refs, list) {
		if (r->type != type)
			continue;
		if (vcc_Teq(r->name, t))
			return (r);
	}
	r = TlAlloc(tl, sizeof *r);
	assert(r != NULL);
	r->name = t;
	r->type = type;
	TAILQ_INSERT_TAIL(&tl->refs, r, list);
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

	TAILQ_FOREACH(r, &tl->refs, list) {
		if (r->defcnt != 0 && r->refcnt != 0)
			continue;
		nerr++;

		type = vcc_typename(tl, r);
		if (r->defcnt == 0 && r->name->tok == METHOD) {
			vsb_printf(tl->sb,
			    "No definition for method %.*s\n", PF(r->name));
			continue;
		}

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

	TAILQ_FOREACH(p, &tl->procs, list)
		if (vcc_Teq(p->name, t))
			return (p);
	p = TlAlloc(tl, sizeof *p);
	assert(p != NULL);
	TAILQ_INIT(&p->calls);
	TAILQ_INSERT_TAIL(&tl->procs, p, list);
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
vcc_AddCall(struct tokenlist *tl, struct token *t)
{
	struct proccall *pc;
	struct proc *p;

	p = vcc_findproc(tl, t);
	pc = TlAlloc(tl, sizeof *pc);
	assert(pc != NULL);
	pc->p = p;
	pc->t = t;
	TAILQ_INSERT_TAIL(&tl->curproc->calls, pc, list);
}

void
vcc_ProcAction(struct proc *p, unsigned returns, struct token *t)
{

	p->returns |= (1 << returns);
	/* Record the first instance of this return */
	if (p->return_tok[returns] == NULL)
		p->return_tok[returns] = t;
}

static int
vcc_CheckActionRecurse(struct tokenlist *tl, struct proc *p, unsigned returns)
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
	u = p->returns & ~returns;
	if (u) {
#define VCL_RET_MAC(a, b, c, d) \
		if (u & VCL_RET_##b) { \
			vsb_printf(tl->sb, "Illegal return \"%s\"\n", #a); \
			vcc_ErrWhere(tl, p->return_tok[d]); \
		}
#include "vcl_returns.h"
#undef VCL_RET_MAC
		vsb_printf(tl->sb, "\n...in function \"%.*s\"\n", PF(p->name));
		vcc_ErrWhere(tl, p->name);
		return (1);
	}
	p->active = 1;
	TAILQ_FOREACH(pc, &p->calls, list) {
		if (vcc_CheckActionRecurse(tl, pc->p, returns)) {
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

	TAILQ_FOREACH(p, &tl->procs, list) {
		i = IsMethod(p->name);
		if (i < 0)
			continue;
		m = method_tab + i;
		if (vcc_CheckActionRecurse(tl, p, m->returns)) {
			vsb_printf(tl->sb,
			    "\n...which is the \"%s\" method\n", m->name);
			vsb_printf(tl->sb, "Legal returns are:");
#define VCL_RET_MAC(a, b, c, d) \
			if (m->returns & c) \
				vsb_printf(tl->sb, " \"%s\"", #a);
#define VCL_RET_MAC_E(a, b, c, d) VCL_RET_MAC(a, b, c, d)
#include "vcl_returns.h"
#undef VCL_RET_MAC
#undef VCL_RET_MAC_E
			vsb_printf(tl->sb, "\n");
			return (1);
		}
	}
	TAILQ_FOREACH(p, &tl->procs, list) {
		if (p->called)
			continue;
		vsb_printf(tl->sb, "Function unused\n");
		vcc_ErrWhere(tl, p->name);
		return (1);
	}
	return (0);
}

