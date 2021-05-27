/*-
 * Copyright (c) 2010 Varnish Software AS
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
 */
/*lint -save -esym(759, SYMTAB_NOERR) -esym(765, SYMTAB_NOERR)*/

#include "config.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "vcc_compile.h"

#include "vct.h"

/*--------------------------------------------------------------------*/

#define VCC_KIND(U,l) const struct kind SYM_##U[1] = {{ KIND_MAGIC, #l}};
#include "tbl/symbol_kind.h"

/*--------------------------------------------------------------------*/

struct vcc_namespace {
	unsigned		magic;
#define VCC_NAMESPACE_MAGIC	0x27b842f4
	const char		*name;
	enum vcc_namespace_e	id;
};

#define VCC_NAMESPACE(U, l)				\
	static const struct vcc_namespace sym_##l = {	\
		VCC_NAMESPACE_MAGIC,			\
		#l,					\
		VCC_NAMESPACE_##U			\
	};						\
	vcc_ns_t SYM_##U = &sym_##l;
#include "vcc_namespace.h"

/*--------------------------------------------------------------------*/

struct symtab {
	unsigned			magic;
#define SYMTAB_MAGIC			0x084d9c8a
	unsigned			nlen;
	const char			*name;
	const struct symtab		*parent;
	VTAILQ_ENTRY(symtab)		list;
	VTAILQ_HEAD(,symtab)		children;
	VTAILQ_HEAD(,symbol)		symbols;
};

static vcc_kind_t
VCC_HandleKind(vcc_type_t fmt)
{
	if (fmt == ACL)		return (SYM_ACL);
	if (fmt == BACKEND)	return (SYM_BACKEND);
	if (fmt == PROBE)	return (SYM_PROBE);
	if (fmt == STEVEDORE)	return (SYM_STEVEDORE);
	if (fmt == SUB)		return (SYM_SUB);
	if (fmt == INSTANCE)	return (SYM_INSTANCE);
	AZ(fmt->global_pfx);
	return (SYM_NONE);
}

void
VCC_PrintCName(struct vsb *vsb, const char *b, const char *e)
{

	AN(vsb);
	AN(b);

	if (e == NULL)
		e = strchr(b, '\0');
	assert(b < e);

	for (; b < e; b++)
		if (vct_isalnum(*b))
			VSB_putc(vsb, *b);
		else
			VSB_printf(vsb, "_%02x_", *b);
}

static void
vcc_symtabname(struct vsb *vsb, const struct symtab *st)
{
	if (st->parent != NULL && st->parent->parent != NULL) {
		vcc_symtabname(vsb, st->parent);
		VSB_putc(vsb, '.');
	}
	VSB_cat(vsb, st->name);
}

void
VCC_SymName(struct vsb *vsb, const struct symbol *sym)
{
	AN(vsb);
	CHECK_OBJ_NOTNULL(sym, SYMBOL_MAGIC);
	CHECK_OBJ_NOTNULL(sym->symtab, SYMTAB_MAGIC);
	vcc_symtabname(vsb, sym->symtab);
}

static struct symtab *
vcc_symtab_new(const char *name)
{
	struct symtab *st;

	ALLOC_OBJ(st, SYMTAB_MAGIC);
	AN(st);
	st->name = name;
	st->nlen = strlen(st->name);
	VTAILQ_INIT(&st->children);
	VTAILQ_INIT(&st->symbols);
	return (st);
}

static struct symtab *
vcc_symtab_str(struct symtab *st, const char *b, const char *e)
{
	struct symtab *st2, *st3;
	size_t l;
	int i;
	const char *q;

	if (e == NULL)
		e = strchr(b, '\0');

	while (b < e) {
		for (q = b; q < e && *q != '.'; q++)
			continue;
		AN(q);
		l = q - b;
		VTAILQ_FOREACH(st2, &st->children, list) {
			i = strncasecmp(st2->name, b, l);
			if (i < 0)
				continue;
			if (i == 0 && l == st2->nlen)
				break;
			st3 = vcc_symtab_new(vcc_Dup_be(b, q));
			st3->parent = st;
			VTAILQ_INSERT_BEFORE(st2, st3, list);
			st2 = st3;
			break;
		}
		if (st2 == NULL) {
			st2 = vcc_symtab_new(vcc_Dup_be(b, q));
			st2->parent = st;
			VTAILQ_INSERT_TAIL(&st->children, st2, list);
		}
		st = st2;
		b = q + 1;
	}
	return (st);
}

static struct symbol *
vcc_new_symbol(struct vcc *tl, struct symtab *st,
    vcc_kind_t kind, int vlo, int vhi)
{
	struct symbol *sym;

	assert(vlo <= vhi);
	sym = TlAlloc(tl, sizeof *sym);
	INIT_OBJ(sym, SYMBOL_MAGIC);
	AN(sym);
	sym->name = st->name;
	sym->symtab = st;
	sym->kind = kind;
	sym->type = VOID;
	sym->lorev = vlo;
	sym->hirev = vhi;
	VTAILQ_INSERT_TAIL(&st->symbols, sym, list);
	return (sym);
}

static struct symbol *
vcc_sym_in_tab(struct vcc *tl, struct symtab *st,
    vcc_kind_t kind, int vlo, int vhi)
{
	const struct symtab *pst;
	struct symbol *sym, *psym;

	assert(vlo <= vhi);
	VTAILQ_FOREACH(sym, &st->symbols, list) {
		if (sym->lorev > vhi || sym->hirev < vlo)
			continue;
		if ((kind == SYM_NONE && kind == sym->kind))
			continue;
		if (tl->syntax < VCL_41 && strcmp(sym->name, "default") &&
		     (kind != SYM_NONE && kind != sym->kind))
			continue;
		return (sym);
	}
	pst = st->parent;
	if (pst == NULL)
		return (sym);
	psym = VTAILQ_FIRST(&pst->symbols);
	if (psym == NULL)
		return (sym);
	if (psym->wildcard == NULL)
		return (sym);

	sym = vcc_new_symbol(tl, st, kind, vlo, vhi);
	psym->wildcard(tl, psym, sym);
	if (tl->err)
		return (NULL);
	return (sym);
}


const struct symxref XREF_NONE[1] = {{"xref_none"}};
const struct symxref XREF_DEF[1] = {{"xref_def"}};
const struct symxref XREF_REF[1] = {{"xref_ref"}};

const struct symmode SYMTAB_NOERR[1] = {{
	.name = "sym_noerror",
	.noerr = 1
}};

const struct symmode SYMTAB_CREATE[1] = {{
	.name = "sym_create"
}};

const struct symmode SYMTAB_EXISTING[1] = {{
	.name = "Symbol not found"
}};

const struct symmode SYMTAB_PARTIAL[1] = {{
	.name = "Symbol not found",
	.partial = 1
}};

const struct symmode SYMTAB_PARTIAL_NOERR[1] = {{
	.name = "Symbol not found",
	.partial = 1,
	.noerr = 1
}};

struct symbol *
VCC_SymbolGet(struct vcc *tl, vcc_ns_t ns, vcc_kind_t kind,
    const struct symmode *e, const struct symxref *x)
{
	struct symtab *st, *st2 = NULL;
	struct symbol *sym = NULL, *sym2 = NULL;
	struct token *t0, *tn, *tn1, *tn2 = NULL;

	AN(tl);
	CHECK_OBJ_NOTNULL(ns, VCC_NAMESPACE_MAGIC);
	AN(ns->name);
	CHECK_OBJ_NOTNULL(kind, KIND_MAGIC);
	AN(e);
	AN(x);
	AN(x->name);
	if (tl->syntax >= VCL_41 && e == SYMTAB_CREATE && kind != SYM_SUB &&
	    vcc_Has_vcl_prefix(tl->t->b)) {
		VSB_cat(tl->sb, "Symbols named 'vcl_*' are reserved.\nAt:");
		vcc_ErrWhere(tl, tl->t);
		return (NULL);
	}

	st = tl->syms[ns->id];
	t0 = tl->t;
	tn = tl->t;
	while (1) {
		st = vcc_symtab_str(st, tn->b, tn->e);
		sym2 = vcc_sym_in_tab(tl, st, kind, tl->esyntax, tl->syntax);
		if (sym2 != NULL) {
			sym = sym2;
			st2 = st;
			tn2 = tn;
		}
		tn1 = vcc_NextTokenFrom(tl, tn);
		if (tn1 == NULL || tn1->tok != '.')
			break;
		tn1 = vcc_NextTokenFrom(tl, tn1);
		if (tn1 == NULL || tn1->tok != ID)
			break;
		tn = tn1;
	}
	if (sym != NULL && sym->kind == SYM_VMOD && e->partial)
		e = SYMTAB_EXISTING;
	if (sym != NULL && e->partial) {
		st = st2;
		tn = tn2;
	} else if (st != st2) {
		sym = NULL;
	}
	if (tl->err || (sym == NULL && e->noerr))
		return (sym);
	AN(st);
	AN(tn);
	if (sym == NULL && e == SYMTAB_CREATE)
		sym = vcc_new_symbol(tl, st, kind, tl->esyntax, tl->syntax);
	tl->t = vcc_NextTokenFrom(tl, tn);
	if (tl->err)
		return (NULL);
	if (sym == NULL) {
		VSB_printf(tl->sb, "%s: '", e->name);
		vcc_PrintTokens(tl, t0, tl->t);
		VSB_cat(tl->sb, "'");
		sym = vcc_sym_in_tab(tl, st, kind, VCL_LOW, VCL_HIGH);
		if (sym != NULL && sym->kind != SYM_OBJECT &&
		    sym->kind != SYM_INSTANCE) { /* XXX: too specific */
			VSB_cat(tl->sb, " (Only available when");
			if (sym->lorev >= VCL_LOW)
				VSB_printf(tl->sb, " %.1f <=", .1 * sym->lorev);
			VSB_cat(tl->sb, " VCL syntax");
			if (sym->hirev <= VCL_HIGH)
				VSB_printf(tl->sb, " <= %.1f", .1 * sym->hirev);
			VSB_cat(tl->sb, ")");
		}
		VSB_cat(tl->sb, "\nAt: ");
		vcc_ErrWhere2(tl, t0, tl->t);
		return (NULL);
	}
	if (kind != SYM_NONE && kind != sym->kind) {
		VSB_cat(tl->sb, "Symbol '");
		vcc_PrintTokens(tl, t0, tl->t);
		VSB_printf(tl->sb, "' has wrong type (%s), expected %s:",
		    sym->kind->name, kind->name);
		VSB_cat(tl->sb, "\nAt: ");
		vcc_ErrWhere2(tl, t0, tl->t);
		if (sym->def_b != NULL) {
			VSB_cat(tl->sb, "Symbol was defined here: ");
			vcc_ErrWhere(tl, sym->def_b);
		} else if (sym->ref_b != NULL) {
			VSB_cat(tl->sb, "Symbol was declared here: ");
			vcc_ErrWhere(tl, sym->ref_b);
		} else {
			VSB_cat(tl->sb, "Symbol was builtin\n");
		}
		return (NULL);
	}
	if (x == XREF_DEF) {
		if (sym->def_b == NULL)
			sym->def_b = t0;
		sym->ndef++;
	} else if (x == XREF_REF) {
		if (sym->ref_b == NULL)
			sym->ref_b = t0;
		sym->nref++;
	} else {
		assert (x == XREF_NONE);
	}
	return (sym);
}

static struct symbol *
vcc_TypeSymbol(struct vcc *tl, vcc_ns_t ns, vcc_kind_t kind, vcc_type_t type)
{
	struct token t[1], *t0;
	struct symbol *sym;
	struct vsb *buf;

	buf = VSB_new_auto();
	AN(buf);
	VSB_printf(buf, "%s.%.*s", type->name, PF(tl->t));
	AZ(VSB_finish(buf));

	/* NB: we create a fake token but errors are handled by the caller. */
	memcpy(t, tl->t, sizeof *t);
	t->b = VSB_data(buf);
	t->e = t->b + VSB_len(buf);

	t0 = tl->t;
	tl->t = t;
	sym = VCC_SymbolGet(tl, ns, kind, SYMTAB_NOERR, XREF_NONE);
	tl->t = t0;
	VSB_destroy(&buf);

	return (sym);
}

struct symbol *
VCC_TypeSymbol(struct vcc *tl, vcc_kind_t kind, vcc_type_t type)
{

	if (strchr(type->name, '.') == NULL)
		return (vcc_TypeSymbol(tl, SYM_TYPE, kind, type));

	/* NB: type imported from a VMOD */
	return (vcc_TypeSymbol(tl, SYM_MAIN, kind, type));
}

struct symbol *
VCC_MkSym(struct vcc *tl, const char *b, vcc_ns_t ns, vcc_kind_t kind,
    int vlo, int vhi)
{
	struct symtab *st;
	struct symbol *sym;

	AN(tl);
	AN(b);
	CHECK_OBJ_NOTNULL(ns, VCC_NAMESPACE_MAGIC);
	CHECK_OBJ_NOTNULL(kind, KIND_MAGIC);
	assert(vlo <= vhi);

	if (tl->syms[ns->id] == NULL)
		tl->syms[ns->id] = vcc_symtab_new("");
	st = vcc_symtab_str(tl->syms[ns->id], b, NULL);
	AN(st);
	sym = vcc_sym_in_tab(tl, st, kind, vlo, vhi);
	AZ(sym);
	sym = vcc_new_symbol(tl, st, kind, vlo, vhi);
	AN(sym);
	sym->noref = 1;
	return (sym);
}

static void
vcc_walksymbols(struct vcc *tl, const struct symtab *root,
    symwalk_f *func, vcc_kind_t kind)
{
	struct symbol *sym;
	struct symtab *st1, *st2 = NULL;

	VTAILQ_FOREACH(sym, &root->symbols, list) {
		if (kind == SYM_NONE || kind == sym->kind)
			func(tl, sym);
		ERRCHK(tl);
	}
	VTAILQ_FOREACH(st1, &root->children, list) {
		if (st2 != NULL)
			assert(strcasecmp(st1->name, st2->name) >= 0);
		st2 = st1;
		vcc_walksymbols(tl, st1, func, kind);
		ERRCHK(tl);
	}
}

void
VCC_WalkSymbols(struct vcc *tl, symwalk_f *func, vcc_ns_t ns, vcc_kind_t kind)
{

	CHECK_OBJ_NOTNULL(ns, VCC_NAMESPACE_MAGIC);
	vcc_walksymbols(tl, tl->syms[ns->id], func, kind);
}

void
VCC_GlobalSymbol(struct symbol *sym, vcc_type_t type)
{
	struct vsb *vsb;

	CHECK_OBJ_NOTNULL(sym, SYMBOL_MAGIC);
	AN(type);
	AN(type->global_pfx);

	vsb = VSB_new_auto();
	AN(vsb);
	VSB_printf(vsb, "%s_", type->global_pfx);
	VCC_PrintCName(vsb, sym->name, NULL);
	AZ(VSB_finish(vsb));
	sym->lname = strdup(VSB_data(vsb));
	AN(sym->lname);
	if (type == SUB) {
		VSB_destroy(&vsb);
		vsb = VSB_new_auto();
		AN(vsb);
		VSB_printf(vsb, "sub_%s", sym->lname);
		AZ(VSB_finish(vsb));
	}
	sym->rname = strdup(VSB_data(vsb));
	AN(sym->rname);
	VSB_destroy(&vsb);

	sym->type = type;
	sym->kind = VCC_HandleKind(sym->type);
	if (sym->kind != SYM_NONE) {
		AZ(VCT_invalid_name(sym->rname, NULL));
		sym->eval = vcc_Eval_Handle;
	} else {
		WRONG("Wrong kind of global symbol");
	}

#define VCL_MET_MAC(l,u,t,b)   sym->r_methods |= VCL_MET_##u;
#include "tbl/vcl_returns.h"
}

struct symbol *
VCC_HandleSymbol(struct vcc *tl, vcc_type_t fmt)
{
	struct symbol *sym;
	vcc_kind_t kind;
	struct token *t;
	const char *p;

	kind = VCC_HandleKind(fmt);
	assert(kind != SYM_NONE);

	t = tl->t;
	sym = VCC_SymbolGet(tl, SYM_MAIN, SYM_NONE, SYMTAB_NOERR, XREF_NONE);
	if (sym != NULL && sym->def_b != NULL && kind == sym->kind) {
		p = sym->kind->name;
		VSB_printf(tl->sb, "%c%s '%.*s' redefined.\n",
		    toupper(*p), p + 1, PF(t));
		vcc_ErrWhere(tl, t);
		VSB_cat(tl->sb, "First definition:\n");
		AN(sym->def_b);
		vcc_ErrWhere(tl, sym->def_b);
		return (sym);
	} else if (sym != NULL && sym->def_b != NULL) {
		VSB_printf(tl->sb, "Name '%.*s' already defined.\n", PF(t));
		vcc_ErrWhere(tl, t);
		VSB_cat(tl->sb, "First definition:\n");
		AN(sym->def_b);
		vcc_ErrWhere(tl, sym->def_b);
		return (sym);
	} else if (sym != NULL && sym->kind != kind) {
		VSB_printf(tl->sb,
		    "Name %.*s must have type '%s'.\n",
		    PF(t), sym->kind->name);
		vcc_ErrWhere(tl, t);
		return (sym);
	}
	if (sym == NULL)
		sym = VCC_SymbolGet(tl, SYM_MAIN, kind, SYMTAB_CREATE,
		    XREF_NONE);
	if (sym == NULL)
		return (NULL);
	AN(sym);
	AZ(sym->ndef);
	VCC_GlobalSymbol(sym, fmt);
	sym->ndef = 1;
	if (sym->def_b == NULL)
		sym->def_b = t;
	return (sym);
}
/*lint -restore */
