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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vcc_compile.h"

/*--------------------------------------------------------------------*/

enum symkind
VCC_HandleKind(vcc_type_t fmt)
{
	if (fmt == ACL)		return(SYM_ACL);
	if (fmt == BACKEND)	return(SYM_BACKEND);
	if (fmt == PROBE)	return(SYM_PROBE);
	if (fmt == STEVEDORE)	return(SYM_STEVEDORE);
	if (fmt == INSTANCE)	return(SYM_INSTANCE);
	return(SYM_NONE);
}

const char *
VCC_SymKind(struct vcc *tl, const struct symbol *s)
{
	switch(s->kind) {
#define VCC_SYMB(uu, ll)	case SYM_##uu: return(#ll);
#include "tbl/symbol_kind.h"
#undef VCC_SYMB
	default:
		ErrInternal(tl);
		VSB_printf(tl->sb, "Symbol Kind 0x%x\n", s->kind);
		return("INTERNALERROR");
	}
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
	return (sym);
}

struct symbol *
VCC_Symbol(struct vcc *tl, struct symbol *parent,
    const char *b, const char *e, enum symkind kind, int create)
{
	const char *q;
	struct symbol *sym, *sym2 = NULL;
	size_t l;
	int i;

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
		i = strncmp(sym->name, b, l);
		if (i < 0)
			continue;
		if (i > 0 || l < sym->nlen) {
			sym2 = sym;
			sym = NULL;
			break;
		}
		if (l > sym->nlen)
			continue;
		if (q < e)
			break;
		if (kind != SYM_NONE && sym->kind != kind)
			continue;
		if (kind == SYM_NONE && sym->kind == kind)
			continue;
		break;
	}
	if (sym == NULL && create == 0 && parent->wildcard != NULL) {
		AN(parent->wildcard);
		parent->wildcard(tl, parent, b, e);
		if (tl->err)
			return (NULL);
		return (VCC_Symbol(tl, parent, b, e, kind, -1));
	}
	if (sym == NULL && create < 1)
		return (sym);
	if (sym == NULL) {
		sym = vcc_new_symbol(tl, b, q);
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
	return (VCC_Symbol(tl, sym, ++q, e, kind, create));
}

static void
vcc_walksymbols(struct vcc *tl, const struct symbol *root,
    symwalk_f *func, enum symkind kind)
{
	struct symbol *sym;

	VTAILQ_FOREACH(sym, &root->children, list) {
		if (kind == SYM_NONE || kind == sym->kind)
			func(tl, sym);
		ERRCHK(tl);
		vcc_walksymbols(tl, sym, func, kind);
	}
}

void
VCC_WalkSymbols(struct vcc *tl, symwalk_f *func, enum symkind kind)
{

	vcc_walksymbols(tl, tl->symbols, func, kind);
}

static void
vcc_global(struct vcc *tl, struct symbol *sym,
    vcc_type_t fmt, const char *str, va_list ap)
{
	struct vsb *vsb;

	vsb = VSB_new_auto();
	AN(vsb);
	VSB_vprintf(vsb, str, ap);
	AZ(VSB_finish(vsb));
	if (tl != NULL)
		sym->rname = TlDup(tl, VSB_data(vsb));
	else
		sym->rname = strdup(VSB_data(vsb));
	AN(sym->rname);
	VSB_destroy(&vsb);
	sym->fmt = fmt;
	sym->kind = VCC_HandleKind(sym->fmt);
	if (sym->kind != SYM_NONE)
		sym->eval = vcc_Eval_Handle;
	else
		WRONG("Wrong kind of global symbol");

#define VCL_MET_MAC(l,u,t,b)   sym->r_methods |= VCL_MET_##u;
#include "tbl/vcl_returns.h"
#undef VCL_MET_MAC
}

struct symbol *
VCC_HandleSymbol(struct vcc *tl, const struct token *tk, vcc_type_t fmt,
    const char *str, ...)
{
	struct symbol *sym;
	enum symkind kind;
	const char *p;
	va_list ap;

	kind = VCC_HandleKind(fmt);
	assert(kind != SYM_NONE);

	sym = VCC_SymbolTok(tl, NULL, tk, SYM_NONE, 0);
	if (sym != NULL && sym->def_b != NULL && kind == sym->kind) {
		p = VCC_SymKind(tl, sym);
		VSB_printf(tl->sb, "%c%s '%.*s' redefined.\n",
		    toupper(*p), p + 1, PF(tk));
		vcc_ErrWhere(tl, tk);
		VSB_printf(tl->sb, "First definition:\n");
		AN(sym->def_b);
		vcc_ErrWhere(tl, sym->def_b);
		return (sym);
	} else if (sym != NULL && sym->def_b != NULL) {
		VSB_printf(tl->sb, "Name '%.*s' already defined.\n", PF(tk));
		vcc_ErrWhere(tl, tk);
		VSB_printf(tl->sb, "First definition:\n");
		AN(sym->def_b);
		vcc_ErrWhere(tl, sym->def_b);
		return (sym);
	} else if (sym != NULL && sym->kind != kind) {
		VSB_printf(tl->sb,
		    "Name %.*s must have type '%s'.\n",
		    PF(tk), VCC_SymKind(tl, sym));
		vcc_ErrWhere(tl, tk);
		return (sym);
	}
	if (sym == NULL)
		sym = VCC_SymbolTok(tl, NULL, tk, kind, 1);
	AN(sym);
	AZ(sym->ndef);
	va_start(ap, str);
	vcc_global(tl, sym, fmt, str, ap);
	va_end(ap);
	sym->ndef = 1;
	if (sym->def_b == NULL)
		sym->def_b = tk;
	return (sym);
}
