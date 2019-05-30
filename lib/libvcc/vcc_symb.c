/*-
 * Copyright (c) 2010 Varnish Software AS
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

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "vcc_compile.h"

#include "vct.h"

static const char * const rootname = "";

/*--------------------------------------------------------------------*/
#define VCC_KIND(U,l) const struct kind SYM_##U[1] = {{ KIND_MAGIC, #l}};
#include "tbl/symbol_kind.h"
/*--------------------------------------------------------------------*/

struct symtab {
	unsigned                        magic;
#define SYMTAB_MAGIC                    0x084d9c8a
	unsigned                        nlen;
	char                            *name;
	struct symtab			*parent;
	VTAILQ_ENTRY(symtab)            list;
	VTAILQ_HEAD(,symtab)            children;
	VTAILQ_HEAD(,symbol)            symbols;
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

static char *
vcc_dup_be(const char *b, const char *e)
{
	char *p;

	AN(b);
	if (e == NULL)
		e = strchr(b, '\0');
	AN(e);
	assert(e >= b);

	p = malloc((e - b) + 1);
	AN(p);
	memcpy(p, b, e - b);
	p[e - b] = '\0';
	return (p);
}

static struct symtab *
vcc_symtab_new(const char *b, const char *e)
{
	struct symtab *st;

	ALLOC_OBJ(st, SYMTAB_MAGIC);
	AN(st);
	st->name = vcc_dup_be(b, e);
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
	char *p, *q;

	p = vcc_dup_be(b, e);

	while (1) {
		q = strchr(p, '.');
		if (q == NULL)
			q = strchr(p, '\0');
		else
			assert(q[1] != '.' && q[1] != '\0');
		AN(q);
		l = q - p;
		VTAILQ_FOREACH(st2, &st->children, list) {
			i = strncasecmp(st2->name, p, l);
			if (i < 0)
				continue;
			if (i == 0 && l == st2->nlen)
				break;
			st3 = vcc_symtab_new(p, q);
			st3->parent = st;
			VTAILQ_INSERT_BEFORE(st2, st3, list);
			st2 = st3;
			break;
		}
		if (st2 == NULL) {
			st2 = vcc_symtab_new(p, q);
			st2->parent = st;
			VTAILQ_INSERT_TAIL(&st->children, st2, list);
		}
		if (*q == '\0')
			return (st2);
		st = st2;
		p = q + 1;
	}
}

static struct symbol *
vcc_new_symbol(struct vcc *tl, struct symtab *st)
{
	struct symbol *sym;

	sym = TlAlloc(tl, sizeof *sym);
	INIT_OBJ(sym, SYMBOL_MAGIC);
	AN(sym);
	sym->name = st->name;
	sym->symtab = st;
	sym->kind = SYM_NONE;
	sym->type = VOID;
	sym->lorev = VCL_LOW;
	sym->hirev = VCL_HIGH;
	VTAILQ_INSERT_TAIL(&st->symbols, sym, list);
	return (sym);
}

static struct symbol *
vcc_sym_in_tab(struct vcc *tl, struct symtab *st,
    vcc_kind_t kind, int vlo, int vhi)
{
	struct symtab *pst;
	struct symbol *sym, *psym;

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
		return(sym);
	psym = VTAILQ_FIRST(&pst->symbols);
	if (psym == NULL)
		return(sym);
	if (psym->wildcard == NULL)
		return(sym);

	sym = vcc_new_symbol(tl, st);
	sym->lorev = vlo;
	sym->hirev = vhi;
	sym->kind = kind;
	psym->wildcard(tl, psym, sym);
	if (tl->err)
		return(NULL);
	return (sym);
}

static struct symbol *
VCC_Symbol(struct vcc *tl,
    const char *b, const char *e, vcc_kind_t kind,
    int create, int vlo, int vhi)
{
	struct symtab *st;
	struct symbol *sym;

	assert(vlo <= vhi);
	if (tl->syms == NULL)
		tl->syms = vcc_symtab_new(rootname, rootname);

	st = vcc_symtab_str(tl->syms, b, e);
	AN(st);
	sym = vcc_sym_in_tab(tl, st, kind, vlo, vhi);
	if (sym == NULL && create) {
		sym = vcc_new_symbol(tl, st);
		sym->lorev = vlo;
		sym->hirev = vhi;
		sym->kind = kind;
	}
	return(sym);
}

const char XREF_NONE[] = "xref_none";
const char XREF_DEF[] = "xref_def";
const char XREF_REF[] = "xref_ref";
const char SYMTAB_NOERR[] = "sym_noerror";
const char SYMTAB_CREATE[] = "sym_create";

static void
vcc_symxref(struct symbol *sym, const char *x, const struct token *t)
{
	if (x == XREF_DEF) {
		if (sym->def_b == NULL)
			sym->def_b = t;
		sym->ndef++;
	} else if (x == XREF_REF) {
		if (sym->ref_b == NULL)
			sym->ref_b = t;
		sym->nref++;
	} else {
		assert (x == XREF_NONE);
	}
}

struct symbol *
VCC_SymbolGetTok(struct vcc *tl, vcc_kind_t kind, const char *e, const char *x,
    const struct token *t)
{
	struct symbol *sym;

	AN(e);
	if (tl->syntax >= VCL_41 && e == SYMTAB_CREATE && kind != SYM_SUB &&
	    (t->b[0] == 'v'|| t->b[0] == 'V') &&
	    (t->b[1] == 'c'|| t->b[1] == 'C') &&
	    (t->b[2] == 'l'|| t->b[2] == 'L') &&
	    (t->b[3] == '_')) {
		VSB_printf(tl->sb,
		    "Symbols named 'vcl_*' are reserved.\nAt:");
		vcc_ErrWhere(tl, t);
		return (NULL);
	}

	sym = VCC_Symbol(tl, t->b, t->e, kind,
	    e == SYMTAB_CREATE ? 1 : 0, tl->syntax, tl->syntax);
	if (sym == NULL && e == SYMTAB_NOERR)
		return (sym);
	if (sym == NULL) {
		VSB_printf(tl->sb, "%s: ", e);
		vcc_ErrToken(tl, t);
		sym = VCC_Symbol(tl, t->b, t->e, kind, 0,
			VCL_LOW, VCL_HIGH);
		if (sym != NULL) {
			VSB_printf(tl->sb, " (Only available when");
			if (sym->lorev >= VCL_LOW)
				VSB_printf(tl->sb, " %.1f <=", .1 * sym->lorev);
			VSB_printf(tl->sb, " VCL syntax");
			if (sym->hirev <= VCL_HIGH)
				VSB_printf(tl->sb, " <= %.1f", .1 * sym->hirev);
			VSB_printf(tl->sb, ")");
		}
		VSB_cat(tl->sb, "\nAt: ");
		vcc_ErrWhere(tl, t);
		return (NULL);
	}
	assert (sym->lorev <= tl->syntax && sym->hirev >= tl->syntax);
	if (kind != SYM_NONE && kind != sym->kind) {
		VSB_printf(tl->sb, "Symbol ");
		vcc_ErrToken(tl, t);
		VSB_printf(tl->sb, " has wrong type (%s): ", sym->kind->name);
		VSB_cat(tl->sb, "\nAt: ");
		vcc_ErrWhere(tl, t);
		if (sym->def_b != NULL) {
			VSB_printf(tl->sb, "Symbol was defined here: ");
			vcc_ErrWhere(tl, sym->def_b);
		} else if (sym->ref_b != NULL) {
			VSB_printf(tl->sb, "Symbol was declared here: ");
			vcc_ErrWhere(tl, sym->ref_b);
		} else {
			VSB_printf(tl->sb, "Symbol was builtin\n");
		}
		return (NULL);
	}
	vcc_symxref(sym, x, t);
	return (sym);
}

struct symbol *
VCC_SymbolGet(struct vcc *tl, vcc_kind_t kind, const char *e, const char *x)
{
	struct symtab *st;
	struct symbol *sym;
	struct token *tn, *tn1;

	if (tl->syntax >= VCL_41 && e == SYMTAB_CREATE && kind != SYM_SUB &&
	    (tl->t->b[0] == 'v'|| tl->t->b[0] == 'V') &&
	    (tl->t->b[1] == 'c'|| tl->t->b[1] == 'C') &&
	    (tl->t->b[2] == 'l'|| tl->t->b[2] == 'L') &&
	    (tl->t->b[3] == '_')) {
		VSB_printf(tl->sb,
		    "Symbols named 'vcl_*' are reserved.\nAt:");
		vcc_ErrWhere(tl, tl->t);
		return (NULL);
	}

	st = tl->syms;
	tn = tl->t;
	while (1) {
		st = vcc_symtab_str(st, tn->b, tn->e);
		tn1 = VTAILQ_NEXT(tn, list);
		if (tn1->tok != '.')
			break;
		tn1 = VTAILQ_NEXT(tn1, list);
		if (tn1->tok != ID)
			break;
		tn = tn1;
	}
	sym = vcc_sym_in_tab(tl, st, kind, tl->syntax, tl->syntax);
	if (sym == NULL && e == SYMTAB_CREATE) {
		sym = vcc_new_symbol(tl, st);
		sym->lorev = tl->syntax;
		sym->hirev = tl->syntax;
		sym->kind = kind;
	}
	if (sym == NULL && e == SYMTAB_NOERR)
		return (sym);
	if (sym == NULL) {
		VSB_printf(tl->sb, "%s: '", e);
		tn = VTAILQ_NEXT(tn, list);
		for (tn1 = tl->t; tn1 != tn; tn1 = VTAILQ_NEXT(tn1, list))
			VSB_printf(tl->sb, "%.*s", PF(tn1));
		VSB_printf(tl->sb, "'");
		sym = vcc_sym_in_tab(tl, st, kind, VCL_LOW, VCL_HIGH);
		if (sym != NULL) {
			VSB_printf(tl->sb, " (Only available when");
			if (sym->lorev >= VCL_LOW)
				VSB_printf(tl->sb, " %.1f <=", .1 * sym->lorev);
			VSB_printf(tl->sb, " VCL syntax");
			if (sym->hirev <= VCL_HIGH)
				VSB_printf(tl->sb, " <= %.1f", .1 * sym->hirev);
			VSB_printf(tl->sb, ")");
		}
		VSB_cat(tl->sb, "\nAt: ");
		vcc_ErrWhere2(tl, tl->t, tn);
		return (NULL);
	}
	if (kind != SYM_NONE && kind != sym->kind) {
		VSB_printf(tl->sb, "Symbol '");
		tn = VTAILQ_NEXT(tn, list);
		for (tn1 = tl->t; tn1 != tn; tn1 = VTAILQ_NEXT(tn1, list))
			VSB_printf(tl->sb, "%.*s", PF(tn1));
		VSB_printf(tl->sb, "' has wrong type (%s): ", sym->kind->name);
		VSB_cat(tl->sb, "\nAt: ");
		vcc_ErrWhere2(tl, tl->t, tn);
		if (sym->def_b != NULL) {
			VSB_printf(tl->sb, "Symbol was defined here: ");
			vcc_ErrWhere(tl, sym->def_b);
		} else if (sym->ref_b != NULL) {
			VSB_printf(tl->sb, "Symbol was declared here: ");
			vcc_ErrWhere(tl, sym->ref_b);
		} else {
			VSB_printf(tl->sb, "Symbol was builtin\n");
		}
		return (NULL);
	}
	vcc_symxref(sym, x, tl->t);
	tl->t = VTAILQ_NEXT(tn, list);
	return (sym);
}

struct symbol *
VCC_MkSym(struct vcc *tl, const char *b, vcc_kind_t kind, int vlo, int vhi)
{
	struct symbol *sym;

	AN(tl);
	AN(b);
	CHECK_OBJ_NOTNULL(kind, KIND_MAGIC);

	sym = VCC_Symbol(tl, b, NULL, kind, 1, vlo, vhi);
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
	}
}

void
VCC_WalkSymbols(struct vcc *tl, symwalk_f *func, vcc_kind_t kind)
{

	vcc_walksymbols(tl, tl->syms, func, kind);
}

void
VCC_GlobalSymbol(struct symbol *sym, vcc_type_t type, const char *pfx)
{
	struct vsb *vsb;

	CHECK_OBJ_NOTNULL(sym, SYMBOL_MAGIC);
	AN(pfx);

	vsb = VSB_new_auto();
	AN(vsb);
	VSB_printf(vsb, "%s_", pfx);
	VCC_PrintCName(vsb, sym->name, NULL);
	AZ(VSB_finish(vsb));
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
VCC_HandleSymbol(struct vcc *tl, vcc_type_t fmt, const char *pfx)
{
	struct symbol *sym;
	vcc_kind_t kind;
	struct token *t;
	const char *p;

	kind = VCC_HandleKind(fmt);
	assert(kind != SYM_NONE);

	t = tl->t;
	sym = VCC_SymbolGet(tl, SYM_NONE, SYMTAB_NOERR, XREF_NONE);
	if (sym != NULL && sym->def_b != NULL && kind == sym->kind) {
		p = sym->kind->name;
		VSB_printf(tl->sb, "%c%s '%.*s' redefined.\n",
		    toupper(*p), p + 1, PF(t));
		vcc_ErrWhere(tl, t);
		VSB_printf(tl->sb, "First definition:\n");
		AN(sym->def_b);
		vcc_ErrWhere(tl, sym->def_b);
		return (sym);
	} else if (sym != NULL && sym->def_b != NULL) {
		VSB_printf(tl->sb, "Name '%.*s' already defined.\n", PF(t));
		vcc_ErrWhere(tl, t);
		VSB_printf(tl->sb, "First definition:\n");
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
		sym = VCC_SymbolGet(tl, kind, SYMTAB_CREATE, XREF_NONE);
	if (sym == NULL)
		return (NULL);
	AN(sym);
	AZ(sym->ndef);
	VCC_GlobalSymbol(sym, fmt, pfx);
	sym->ndef = 1;
	if (sym->def_b == NULL)
		sym->def_b = t;
	return (sym);
}
