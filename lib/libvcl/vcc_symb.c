/*-
 * Copyright (c) 2010 Linpro AS
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vsb.h"

#include "vcc_priv.h"
#include "vcc_compile.h"
#include "libvarnish.h"

/*--------------------------------------------------------------------*/

const char *
VCC_SymKind(struct vcc *tl, const struct symbol *s)
{
	switch(s->kind) {
#define VCC_SYMB(uu, ll, dd)	case SYM_##uu: return(dd);
#include "symbol_kind.h"
#undef VCC_SYMB
	default:
		ErrInternal(tl);
		vsb_printf(tl->sb, "Symbol Kind 0x%x\n", s->kind);
		return("INTERNALERROR");
	}
}


static struct symbol *
vcc_AddSymbol(struct vcc *tl, const char *nb, int l, enum symkind kind)
{
	struct symbol *sym;

	VTAILQ_FOREACH(sym, &tl->symbols, list) {
		if (sym->nlen != l)
			continue;
		if (memcmp(nb, sym->name, l))
			continue;
		if (kind != sym->kind)
			continue;
		vsb_printf(tl->sb, "Name Collision: <%.*s> <%s>\n",
		    l, nb, VCC_SymKind(tl, sym));
		ErrInternal(tl);
		return (NULL);
	}
	ALLOC_OBJ(sym, SYMBOL_MAGIC);
	AN(sym);
	sym->name = malloc(l + 1);
	AN(sym->name);
	memcpy(sym->name, nb, l);
	sym->name[l] = '\0';
	sym->nlen = l;
	VTAILQ_INSERT_HEAD(&tl->symbols, sym, list);
	sym->kind = kind;
	return (sym);
}

struct symbol *
VCC_AddSymbolStr(struct vcc *tl, const char *name, enum symkind kind)
{

	return (vcc_AddSymbol(tl, name, strlen(name), kind));
}

struct symbol *
VCC_AddSymbolTok(struct vcc *tl, const struct token *t, enum symkind kind)
{

	return (vcc_AddSymbol(tl, t->b, t->e - t->b, kind));
}

struct symbol *
VCC_GetSymbolTok(struct vcc *tl, const struct token *tok, enum symkind kind)
{
	struct symbol *sym;

	sym = VCC_FindSymbol(tl, tok, kind);
	if (sym == NULL) {
		sym = vcc_AddSymbol(tl, tok->b, tok->e - tok->b, kind);
		AN(sym);
		sym->def_b = tok;
	}
	return (sym);
}

struct symbol *
VCC_FindSymbol(struct vcc *tl, const struct token *t, enum symkind kind)
{
	struct symbol *sym;

	assert(t->tok == ID);
	VTAILQ_FOREACH(sym, &tl->symbols, list) {
		if (sym->kind == SYM_WILDCARD &&
		   (t->e - t->b > sym->nlen) &&
		   !memcmp(sym->name, t->b, sym->nlen)) {
			AN (sym->wildcard);
			return (sym->wildcard(tl, t, sym));
		}
		if (kind != SYM_NONE && kind != sym->kind)
			continue;
		if (vcc_IdIs(t, sym->name))
			return (sym);
	}
	return (NULL);
}

void
VCC_WalkSymbols(struct vcc *tl, symwalk_f *func, enum symkind kind)
{
	struct symbol *sym;

	VTAILQ_FOREACH(sym, &tl->symbols, list) {
		if (kind == SYM_NONE || kind == sym->kind)
			func(tl, sym);
		ERRCHK(tl);
	}
}
