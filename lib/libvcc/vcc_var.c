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

#include "vcc_compile.h"
#include "vct.h"

/*--------------------------------------------------------------------*/

void __match_proto__(sym_wildcard_t)
vcc_Var_Wildcard(struct vcc *tl, struct symbol *parent,
    const char *b, const char *e)
{
	struct symbol *sym;
	struct var *v;
	const struct var *vh;
	unsigned u;
	const char *p;
	struct vsb *vsb;

	vh = parent->wildcard_priv;
	assert(vh->fmt == HEADER);

	if (b + 127 <= e) {
		VSB_printf(tl->sb, "HTTP header (%.20s..) is too long.\n", b);
		VSB_cat(tl->sb, "\nAt: ");
		vcc_ErrWhere(tl, tl->t);
		return;
	}

	v = TlAlloc(tl, sizeof *v);
	AN(v);
	v->r_methods = vh->r_methods;
	v->w_methods = vh->w_methods;
	v->fmt = vh->fmt;

	/* Create a C-name version of the header name */
	vsb = VSB_new_auto();
	AN(vsb);
	VSB_printf(vsb, "&VGC_%s_", vh->rname);
	for (p = b, u = 1; p < e; p++, u++)
		if (vct_isalnum(*p))
			VSB_putc(vsb, *p);
		else
			VSB_printf(vsb, "_%02x_", *p);
	AZ(VSB_finish(vsb));

	/* Create the static identifier */
	Fh(tl, 0, "static const struct gethdr_s %s =\n", VSB_data(vsb) + 1);
	Fh(tl, 0, "    { %s, \"\\%03o%.*s:\"};\n",
	    vh->rname, u, (int)(e - b), b);

	/* Create the symbol r/l values */
	v->rname = TlDup(tl, VSB_data(vsb));
	VSB_clear(vsb);
	VSB_printf(vsb, "VRT_SetHdr(ctx, %s,", v->rname);
	AZ(VSB_finish(vsb));
	v->lname = TlDup(tl, VSB_data(vsb));
	VSB_destroy(&vsb);

	sym = VCC_Symbol(tl, parent, b, e, SYM_VAR, 1);
	AN(sym);
	sym->fmt = v->fmt;
	sym->eval = vcc_Eval_Var;
	sym->r_methods = v->r_methods;
	sym->rname = v->rname;
	sym->w_methods = v->w_methods;
	sym->lname = v->lname;
}

/*--------------------------------------------------------------------*/

const struct symbol *
vcc_FindVar(struct vcc *tl, const struct token *t, int wr_access,
    const char *use)
{
	const struct symbol *sym;

	sym = VCC_SymbolTok(tl, NULL, t, SYM_VAR, 0);
	if (tl->err)
		return (NULL);
	if (sym != NULL) {
		if (wr_access && sym->w_methods == 0) {
			VSB_printf(tl->sb, "Variable ");
			vcc_ErrToken(tl, t);
			VSB_printf(tl->sb, " is read only.");
			VSB_cat(tl->sb, "\nAt: ");
			vcc_ErrWhere(tl, t);
			return (NULL);
		} else if (wr_access) {
			vcc_AddUses(tl, t, sym->w_methods, use);
		} else if (sym->r_methods == 0) {
			VSB_printf(tl->sb, "Variable ");
			vcc_ErrToken(tl, t);
			VSB_printf(tl->sb, " is write only.");
			VSB_cat(tl->sb, "\nAt: ");
			vcc_ErrWhere(tl, t);
			return (NULL);
		} else {
			vcc_AddUses(tl, t, sym->r_methods, use);
		}
		return (sym);
	}
	VSB_printf(tl->sb, "Unknown variable ");
	vcc_ErrToken(tl, t);
	VSB_cat(tl->sb, "\nAt: ");
	vcc_ErrWhere(tl, t);
	return (NULL);
}
