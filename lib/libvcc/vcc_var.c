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
	unsigned u;
	const char *p, *leaf;
	struct vsb *vsb;

	vh = wc->var;
	assert(vh->fmt == HEADER);

	v = TlAlloc(tl, sizeof *v);
	AN(v);
	v->name = TlDupTok(tl, t);
	v->r_methods = vh->r_methods;
	v->w_methods = vh->w_methods;
	v->fmt = vh->fmt;
	leaf = v->name + vh->len;

	/* Create a C-name version of the header name */
	vsb = VSB_new_auto();
	AN(vsb);
	VSB_printf(vsb, "&VGC_%s_", vh->rname);
	for (p = leaf, u = 1; *p != '\0'; p++, u++)
		if (vct_isalpha(*p) || vct_isdigit(*p))
			VSB_putc(vsb, *p);
		else
			VSB_printf(vsb, "_%02x_", *p);
	AZ(VSB_finish(vsb));

	/* Create the static identifier */
	Fh(tl, 0, "static const struct gethdr_s %s =\n", VSB_data(vsb) + 1);
	Fh(tl, 0, "    { %s, \"\\%03o%s:\"};\n", vh->rname, u, leaf);

	/* Create the symbol r/l values */
	v->rname = TlDup(tl, VSB_data(vsb));
	VSB_clear(vsb);
	VSB_printf(vsb, "VRT_SetHdr(ctx, %s,", v->rname);
	AZ(VSB_finish(vsb));
	v->lname = TlDup(tl, VSB_data(vsb));
	VSB_delete(vsb);

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
