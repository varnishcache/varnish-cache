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
 *
 * All stuff related to the storage.* part of the namespace.
 *
 * "All" is actually only a wildcard function, which instantiates variables
 * on demand under the storage.* tree of the namespace.
 *
 * About the syntax:
 * -----------------
 *
 * One of our long term goals is to have dynamic storage configuration, such
 * as the ability to add or remove a stevedore on the fly, without restarting
 * the worker process.
 *
 * Even though this goal is far out in the future, it influences the syntax
 * design of storage selection from VCL.
 *
 * In difference from backends, where we know the possible set of backends at
 * compile time, we will not in the future know the identity of the stevedores
 * available at compile time, so we have to rely on VRT name resolution.
 *
 * This indicates a namespace on the form storage.<stevedore>.<property>
 *
 * For each property, we must define a default value if the named stevedore
 * does not exists, such that for instance stevedore.forgetit.freespace
 * returns zero etc.
 *
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vsb.h"

#include "vcc_priv.h"
#include "vcc_compile.h"
#include "libvarnish.h"

#define PFX "storage."

/*--------------------------------------------------------------------
 *
 */

static struct var *
vcc_Stv_mkvar(struct vcc *tl, const struct token *t, enum var_type fmt)
{
	struct var *v;

	v = TlAlloc(tl, sizeof *v);
	AN(v);

	v->name = TlDupTok(tl, t);
	v->r_methods = 0
#define VCL_MET_MAC(l,u,b)	| VCL_MET_##u
#include "vcl_returns.h"
#undef VCL_MET_MAC
		;
	v->fmt = fmt;

	return (v);
}

static struct stvars {
	const char	*name;
	enum var_type	fmt;
} stvars[] = {
#define VRTSTVVAR(nm, vtype, ctype, dval)	{ #nm, vtype },
#include "vrt_stv_var.h"
#undef VRTSTVVAR
	{ NULL,			BOOL }
};

struct symbol *
vcc_Stv_Wildcard(struct vcc *tl, const struct token *t,
    const struct symbol *wcsym)
{
	const char *p, *q;
	struct var *v = NULL;
	struct symbol *sym;
	struct stvars *sv;
	char stv[1024];
	char buf[1024];

	(void)wcsym;
	assert((t->e - t->b) > strlen(PFX));
	assert(!memcmp(t->b, PFX, strlen(PFX)));

	p = t->b + strlen(PFX);
	for (q = p; q < t->e && *q != '.'; q++)
		continue;
	bprintf(stv, "%.*s", (int)(q - p), p);

	if (q == t->e) {
		v = vcc_Stv_mkvar(tl, t, BOOL);
		bprintf(buf, "VRT_Stv(\"%s\")", stv);
		v->rname = TlDup(tl, buf);
	} else {
		assert(*q  == '.');
		q++;
		for(sv = stvars; sv->name != NULL; sv++) {
			if (strncmp(q, sv->name, t->e - q))
				continue;
			if (sv->name[t->e - q] != '\0')
				continue;
			v = vcc_Stv_mkvar(tl, t, sv->fmt);
			bprintf(buf, "VRT_Stv_%s(\"%s\")", sv->name, stv);
			v->rname = TlDup(tl, buf);
			break;
		}
	}

	if (v == NULL)
		return (NULL);

	sym = VCC_AddSymbolTok(tl, t, SYM_VAR);
	AN(sym);
	sym->var = v;
	sym->fmt = v->fmt;
	sym->eval = vcc_Eval_Var;
	sym->r_methods = v->r_methods;

	return (sym);
}
