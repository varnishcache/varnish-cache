/*-
 * Copyright (c) 2010-2015 Varnish Software AS
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

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "vcc_compile.h"
#include "libvcc.h"		// VCC_Stevedore() proto

/*--------------------------------------------------------------------
 *
 */

static struct stvars {
	const char	*name;
	vcc_type_t	type;
} stvars[] = {
#define VRTSTVVAR(nm, vtype, ctype, dval)	{ #nm, vtype },
#include "tbl/vrt_stv_var.h"
#undef VRTSTVVAR
	{ NULL,			BOOL }
};

void
vcc_stevedore(struct vcc *vcc, const char *stv_name)
{
	struct symbol *sym;
	struct stvars *sv;
	char buf[1024];

	CHECK_OBJ_NOTNULL(vcc, VCC_MAGIC);
	bprintf(buf, "storage.%s", stv_name);
	sym = VCC_MkSym(vcc, buf, SYM_VAR, VCL_LOW, VCL_41);
	AN(sym);
	sym->type = STEVEDORE;
	sym->eval = vcc_Eval_Var;
	bprintf(buf, "VRT_stevedore(\"%s\")", stv_name);
	sym->rname = TlDup(vcc, buf);
	sym->r_methods = ~0;

	for (sv = stvars; sv->name != NULL; sv++) {
		bprintf(buf, "storage.%s.%s", stv_name, sv->name);
		sym = VCC_MkSym(vcc, buf, SYM_VAR, VCL_LOW, VCL_41);
		AN(sym);
		sym->type = sv->type;
		sym->eval = vcc_Eval_Var;
		bprintf(buf, "VRT_Stv_%s(\"%s\")", sv->name, stv_name);
		sym->rname = TlDup(vcc, buf);
		sym->r_methods = ~0;
	}
}
