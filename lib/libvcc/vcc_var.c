/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "vcc_compile.h"

#include "vct.h"

/*--------------------------------------------------------------------*/

static void
vcc_Header_Fh(struct vcc *tl, struct symbol *sym)
{
	const struct symbol *parent;

	AN(tl);
	AN(sym);
	AZ(sym->wildcard);
	assert(sym->type == HEADER);

	parent = sym->eval_priv;
	AN(parent);
	AN(parent->wildcard);
	assert(parent->type == HEADER);

	/* Create the static identifier */
	Fh(tl, 0, "static const struct gethdr_s %s =\n", sym->rname + 1);
	Fh(tl, 0, "    { %s, \"\\%03o%s:\"};\n",
	    parent->rname, (unsigned int)strlen(sym->name) + 1, sym->name);
}

/*--------------------------------------------------------------------*/

void v_matchproto_(sym_wildcard_t)
vcc_Var_Wildcard(struct vcc *tl, struct symbol *parent, struct symbol *sym)
{
	struct vsb *vsb;
	const char *p;

	AN(sym);
	assert(parent->type == HEADER);

	if (strlen(sym->name) >= 127) {
		VSB_printf(tl->sb, "HTTP header (%.20s..) is too long.\n",
		    sym->name);
		tl->err = 1;
		return;
	}

	for (p = sym->name; *p != '\0'; p++) {
		if (!vct_istchar(*p)) {
			VSB_cat(tl->sb, "Invalid character '");
			VSB_quote(tl->sb, p, 1, VSB_QUOTE_PLAIN);
			VSB_cat(tl->sb, "' in header name.\n");
			tl->err = 1;
			return;
		}
	}

	sym->noref = 1;
	sym->kind = SYM_VAR;
	sym->type = parent->type;
	sym->eval = vcc_Eval_Var;
	sym->eval_priv = parent;
	sym->r_methods = parent->r_methods;
	sym->w_methods = parent->w_methods;
	sym->u_methods = parent->u_methods;

	/* Create a C-name version of the header name */
	vsb = VSB_new_auto();
	AN(vsb);
	VSB_printf(vsb, "&VGC_%s_", parent->rname);
	VCC_PrintCName(vsb, sym->name, NULL);
	AZ(VSB_finish(vsb));

	/* Create the symbol r/l values */
	sym->rname = TlDup(tl, VSB_data(vsb));

	if (sym->w_methods) {
		VSB_clear(vsb);
		VSB_printf(vsb, "VRT_SetHdr(ctx, %s,", sym->rname);
		AZ(VSB_finish(vsb));
		sym->lname = TlDup(tl, VSB_data(vsb));
	}

	if (sym->u_methods) {
		VSB_clear(vsb);
		VSB_printf(vsb, "VRT_UnsetHdr(ctx, %s)", sym->rname);
		AZ(VSB_finish(vsb));
		sym->uname = TlDup(tl, VSB_data(vsb));
	}
	VSB_destroy(&vsb);

	vcc_Header_Fh(tl, sym);
}
