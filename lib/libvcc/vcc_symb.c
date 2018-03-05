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

/*--------------------------------------------------------------------*/
#define VCC_KIND(U,l) const struct kind SYM_##U[1] = {{ KIND_MAGIC, #l}};
#include "tbl/symbol_kind.h"
/*--------------------------------------------------------------------*/

static vcc_kind_t
VCC_HandleKind(vcc_type_t fmt)
{
	if (fmt == ACL)		return(SYM_ACL);
	if (fmt == BACKEND)	return(SYM_BACKEND);
	if (fmt == PROBE)	return(SYM_PROBE);
	if (fmt == STEVEDORE)	return(SYM_STEVEDORE);
	if (fmt == SUB)		return(SYM_SUB);
	if (fmt == INSTANCE)	return(SYM_INSTANCE);
	return(SYM_NONE);
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

static struct symbol *
vcc_new_symbol(struct vcc *tl, const char *b, const char *e)
{
	struct symbol *sym;

	AN(b);
	if (e == NULL)
		e = strchr(b, '\0');
	AN(e);
	assert(e > b);
	sym = TlAlloc(tl, sizeof *sym);
	INIT_OBJ(sym, SYMBOL_MAGIC);
	AN(sym);
	sym->name = TlAlloc(tl, (e - b) + 1L);
	AN(sym->name);
	memcpy(sym->name, b, (e - b));
	sym->name[e - b] = '\0';
	sym->nlen = e - b;
	VTAILQ_INIT(&sym->children);
	sym->kind = SYM_NONE;
	sym->type = VOID;
	sym->lorev = VCL_LOW;
	sym->hirev = VCL_HIGH;
	return (sym);
}

static struct symbol *
VCC_Symbol(struct vcc *tl, struct symbol *parent,
    const char *b, const char *e, vcc_kind_t kind,
    int create, unsigned vlo, unsigned vhi)
{
	const char *q;
	struct symbol *sym, *sym2 = NULL;
	size_t l;
	int i;

	assert(vlo <= vhi);
	if (tl->symbols == NULL)
		tl->symbols = vcc_new_symbol(tl, "<root>", NULL);
	if (parent == NULL)
		parent = tl->symbols;

	AN(b);
	assert(e == NULL || b < e);
	if (e == NULL)
		e = strchr(b, '\0');
	assert(e > b);
	if (e[-1] == '.')
		e--;
	assert(e > b);

	q = strchr(b, '.');
	if (q == NULL || q > e)
		q = e;
	l = q - b;
	assert(l > 0);

	VTAILQ_FOREACH(sym, &parent->children, list) {
		i = strncasecmp(sym->name, b, l);
		if (i < 0)
			continue;
		if (i > 0 || l < sym->nlen) {
			sym2 = sym;
			sym = NULL;
			break;
		}
		if (l > sym->nlen)
			continue;
		if (sym->lorev > vhi || sym->hirev < vlo)
			continue;
		if (q < e)
			break;
		if ((kind == SYM_NONE && kind == sym->kind))
			continue;
		if (tl->syntax < VCL_41 &&
		     (kind != SYM_NONE && kind != sym->kind))
			continue;
		break;
	}
	if (sym == NULL && create == 0 && parent->wildcard != NULL) {
		AN(parent->wildcard);
		sym2 = vcc_new_symbol(tl, b, e);
		sym2->parent = parent;
		parent->wildcard(tl, parent, sym2);
		if (tl->err)
			return (NULL);
		VTAILQ_FOREACH(sym, &parent->children, list) {
			i = strncasecmp(sym->name, b, l);
			if (i > 0 || (i == 0 && l < sym->nlen))
				break;
		}
		sym2->lorev = vlo;
		sym2->hirev = vhi;
		if (sym == NULL)
			VTAILQ_INSERT_TAIL(&parent->children, sym2, list);
		else
			VTAILQ_INSERT_BEFORE(sym, sym2, list);
		return (VCC_Symbol(tl, parent, b, e, kind, -1, vlo, vhi));
	}
	if (sym == NULL && create < 1)
		return (sym);
	if (sym == NULL) {
		sym = vcc_new_symbol(tl, b, q);
		sym->parent = parent;
		sym->lorev = vlo;
		sym->hirev = vhi;
		if (sym2 != NULL)
			VTAILQ_INSERT_BEFORE(sym2, sym, list);
		else
			VTAILQ_INSERT_TAIL(&parent->children, sym, list);
		if (q == e)
			sym->kind = kind;
	}
	if (q == e)
		return (sym);
	assert(*q == '.');
	return (VCC_Symbol(tl, sym, ++q, e, kind, create, vlo, vhi));
}

const char XREF_NONE[] = "xref_none";
const char XREF_DEF[] = "xref_def";
const char XREF_REF[] = "xref_ref";
const char SYMTAB_NOERR[] = "sym_noerror";
const char SYMTAB_CREATE[] = "sym_create";

struct symbol *
VCC_SymbolGet(struct vcc *tl, vcc_kind_t kind, const char *e, const char *x)
{
	struct symbol *sym;

	AN(e);
	if (tl->syntax >= VCL_41 && e == SYMTAB_CREATE && kind != SYM_SUB &&
	    (tl->t->b[0] == 'v'|| tl->t->b[0] == 'V') &&
	    (tl->t->b[1] == 'c'|| tl->t->b[1] == 'C') &&
	    (tl->t->b[2] == 'l'|| tl->t->b[2] == 'L')) {
		VSB_printf(tl->sb,
		    "Symbols named 'vcl_*' are reserved.\nAt:");
		vcc_ErrWhere(tl, tl->t);
		return (NULL);
	}

	sym = VCC_Symbol(tl, NULL, tl->t->b, tl->t->e, kind,
	    e == SYMTAB_CREATE ? 1 : 0, tl->syntax, tl->syntax);
	if (sym == NULL && e == SYMTAB_NOERR)
		return (sym);
	if (sym == NULL) {
		VSB_printf(tl->sb, "%s: ", e);
		vcc_ErrToken(tl, tl->t);
		sym = VCC_Symbol(tl, NULL, tl->t->b, tl->t->e, kind, 0,
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
		vcc_ErrWhere(tl, tl->t);
		return (NULL);
	}
	assert (sym->lorev <= tl->syntax && sym->hirev >= tl->syntax);
	if (kind != SYM_NONE && kind != sym->kind) {
		VSB_printf(tl->sb, "Symbol ");
		vcc_ErrToken(tl, tl->t);
		if (sym->kind == SYM_NONE)
			VSB_printf(tl->sb, " is a reserved word.");
		else
			VSB_printf(tl->sb, " has wrong type (%s): ",
				sym->kind->name);
		VSB_cat(tl->sb, "\nAt: ");
		vcc_ErrWhere(tl, tl->t);
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
	if (x == XREF_DEF) {
		if (sym->def_b == NULL)
			sym->def_b = tl->t;
		sym->ndef++;
	} else if (x == XREF_REF) {
		if (sym->ref_b == NULL)
			sym->ref_b = tl->t;
		sym->nref++;
	} else {
		assert (x == XREF_NONE);
	}
	vcc_NextToken(tl);
	return (sym);
}

struct symbol *
VCC_MkSym(struct vcc *tl, const char *b, vcc_kind_t kind,
    unsigned vlo, unsigned vhi)
{
	struct symbol *sym;

	sym = VCC_Symbol(tl, NULL, b, NULL, kind, 1, vlo, vhi);
	sym->noref = 1;
	return (sym);
}


static void
vcc_walksymbols(struct vcc *tl, const struct symbol *root,
    symwalk_f *func, vcc_kind_t kind)
{
	struct symbol *sym, *sym2 = NULL;

	VTAILQ_FOREACH(sym, &root->children, list) {
		if (sym2 != NULL)
			assert(strcasecmp(sym->name, sym2->name) >= 0);
		sym2 = sym;
		if (kind == SYM_NONE || kind == sym->kind)
			func(tl, sym);
		ERRCHK(tl);
		vcc_walksymbols(tl, sym, func, kind);
	}
}

void
VCC_WalkSymbols(struct vcc *tl, symwalk_f *func, vcc_kind_t kind)
{

	vcc_walksymbols(tl, tl->symbols, func, kind);
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
