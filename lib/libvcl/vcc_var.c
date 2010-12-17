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
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <stdio.h>
#include <string.h>

#include "vsb.h"

#include "vcc_priv.h"
#include "vcc_compile.h"
#include "libvarnish.h"

/*--------------------------------------------------------------------*/

struct symbol *
vcc_Var_Wildcard(struct vcc *tl, const struct token *t, const struct symbol *wc)
{
	struct symbol *sym;
	struct var *v;
	const struct var *vh;
	int l;
	char buf[258];

	vh = wc->var;

	v = TlAlloc(tl, sizeof *v);
	AN(v);

	v->name = TlDupTok(tl, t);
	v->r_methods = vh->r_methods;
	v->w_methods = vh->w_methods;
	v->fmt = STRING;
	v->hdr = vh->hdr;
	l = strlen(v->name + vh->len) + 1;

	bprintf(buf, "VRT_GetHdr(sp, %s, \"\\%03o%s:\")",
	    v->hdr, (unsigned)l, v->name + vh->len);
	v->rname = TlDup(tl, buf);

	bprintf(buf, "VRT_SetHdr(sp, %s, \"\\%03o%s:\", ",
	    v->hdr, (unsigned)l, v->name + vh->len);
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
			vsb_printf(tl->sb, "Variable ");
			vcc_ErrToken(tl, t);
			vsb_printf(tl->sb, " is read only.");
			vsb_cat(tl->sb, "\nAt: ");
			vcc_ErrWhere(tl, t);
			return (NULL);
		} else if (wr_access) {
			vcc_AddUses(tl, t, v->w_methods, use);
		} else if (v->r_methods == 0) {
			vsb_printf(tl->sb, "Variable ");
			vcc_ErrToken(tl, t);
			vsb_printf(tl->sb, " is write only.");
			vsb_cat(tl->sb, "\nAt: ");
			vcc_ErrWhere(tl, t);
			return (NULL);
		} else {
			vcc_AddUses(tl, t, v->r_methods, use);
		}
		assert(v->fmt != HEADER);
		return (v);
	}
	vsb_printf(tl->sb, "Unknown variable ");
	vcc_ErrToken(tl, t);
	vsb_cat(tl->sb, "\nAt: ");
	vcc_ErrWhere(tl, t);
	return (NULL);
}

/*--------------------------------------------------------------------*/

void
vcc_VarVal(struct vcc *tl, const struct var *vp, const struct token *vt)
{
	double d;

	if (vp->fmt == TIME) {
		vcc_TimeVal(tl, &d);
		ERRCHK(tl);
		Fb(tl, 0, "%g", d);
	} else if (vp->fmt == DURATION) {
		vcc_RTimeVal(tl, &d);
		ERRCHK(tl);
		Fb(tl, 0, "%g", d);
	} else if (vp->fmt == INT) {
		Fb(tl, 0, "%u", vcc_UintVal(tl));
	} else {
		AN(vt);
		vsb_printf(tl->sb,
		    "Variable has incompatible type.\n");
		vcc_ErrWhere(tl, vt);
		return;
	}
}
