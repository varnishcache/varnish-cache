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
 */

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "vcc_compile.h"
#include "vct.h"

/*--------------------------------------------------------------------*/

struct symbol *
vcc_Var_Wildcard(struct vcc *tl, const struct token *t, const struct symbol *wc)
{
	struct symbol *sym;
	struct var *v;
	const struct var *vh;
	int l, i;
	char c;
	char buf[258];
	char cnam[256];

	vh = wc->var;

	v = TlAlloc(tl, sizeof *v);
	AN(v);

	assert(vh->fmt == HEADER);
	v->name = TlDupTok(tl, t);
	v->r_methods = vh->r_methods;
	v->w_methods = vh->w_methods;
	v->fmt = vh->fmt;

	/* Create a C-name version of the header name */
	l = strlen(v->name + vh->len) + 1;
	for (i = 0; i < l - 1; i++) {
		c = *(v->name + vh->len + i);
		if (vct_isalpha(c) || vct_isdigit(c))
			cnam[i] = c;
		else
			cnam[i] = '_';
	}
	cnam[i] = '\0';

	/* Create the static identifier */
	Fh(tl, 0, "static const struct gethdr_s VGC_%s_%s =\n",
	    vh->rname, cnam);
	Fh(tl, 0, "    { %s, \"\\%03o%s:\"};\n",
	    vh->rname, (unsigned)l, v->name + vh->len);

	bprintf(buf, "&VGC_%s_%s", vh->rname, cnam);
	v->rname = TlDup(tl, buf);
	bprintf(buf, "VRT_SetHdr(ctx, %s, ", v->rname);
	v->lname = TlDup(tl, buf);

	sym = VCC_AddSymbolTok(tl, t, SYM_VAR);
	AN(sym);
	sym->var = v;
	sym->fmt = v->fmt;
	sym->eval = vcc_Eval_Var;
	sym->r_methods = v->r_methods;
	return (sym);
}

/*--------------------------------------------------------------------*/

const struct var *
vcc_FindVar(struct vcc *tl, const struct token *t, int wr_access,
    const char *use)
{
	const struct var *v;
	const struct symbol *sym;

	AN(tl->vars);
	sym = VCC_FindSymbol(tl, t, SYM_VAR);
	if (sym != NULL) {
		v = sym->var;
		AN(v);

		if (wr_access && v->w_methods == 0) {
			VSB_printf(tl->sb, "Variable ");
			vcc_ErrToken(tl, t);
			VSB_printf(tl->sb, " is read only.");
			VSB_cat(tl->sb, "\nAt: ");
			vcc_ErrWhere(tl, t);
			return (NULL);
		} else if (wr_access) {
			vcc_AddUses(tl, t, v->w_methods, use);
		} else if (v->r_methods == 0) {
			VSB_printf(tl->sb, "Variable ");
			vcc_ErrToken(tl, t);
			VSB_printf(tl->sb, " is write only.");
			VSB_cat(tl->sb, "\nAt: ");
			vcc_ErrWhere(tl, t);
			return (NULL);
		} else {
			vcc_AddUses(tl, t, v->r_methods, use);
		}
		return (v);
	}
	VSB_printf(tl->sb, "Unknown variable ");
	vcc_ErrToken(tl, t);
	VSB_cat(tl->sb, "\nAt: ");
	vcc_ErrWhere(tl, t);
	return (NULL);
}
