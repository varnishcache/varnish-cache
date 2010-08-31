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

#include "svnid.h"
SVNID("$Id$");

#include <stdio.h>
#include <dlfcn.h>
#include <string.h>

#include "vsb.h"

#include "vcc_priv.h"
#include "vcc_compile.h"
#include "libvarnish.h"

void
vcc_ParseImport(struct vcc *tl)
{
	void *hdl;
	char fn[1024];
	struct token *mod;
	const char *modname;
	const char *proto;
	const char **spec;
	struct symbol *sym;
	const char *p;
	// int *modlen;

	SkipToken(tl, ID);

	ExpectErr(tl, ID);
	mod = tl->t;
	vcc_NextToken(tl);

	if (tl->t->tok == ID) {
		vcc_NextToken(tl);
		ExpectErr(tl, CSTR);
		bprintf(fn, "%s", tl->t->dec);
		vcc_NextToken(tl);
	} else {
		Fi(tl, 0, ", NULL);\n");
		bprintf(fn, "XXX: %s", "XXX: no default path");
	}

	Fh(tl, 0, "static void *VGC_vmod_%.*s;\n", PF(mod));

	Fi(tl, 0, "\tVRT_Vmod_Init(&VGC_vmod_%.*s,\n", PF(mod));
	Fi(tl, 0, "\t    &Vmod_Func_%.*s,\n", PF(mod));
	Fi(tl, 0, "\t    sizeof(Vmod_Func_%.*s),\n", PF(mod));
	Fi(tl, 0, "\t    \"%.*s\",\n", PF(mod));
	if (fn != NULL) {
		Fi(tl, 0, "\t    ");
		EncString(tl->fi, fn, NULL, 0);
	} else {
		Fi(tl, 0, "\t    0");
	}
	Fi(tl, 0, ");\n");

	/* XXX: zero the function pointer structure */
	Ff(tl, 0, "\tVRT_Vmod_Fini(&VGC_vmod_%.*s);\n", PF(mod));

	SkipToken(tl, ';');

	hdl = dlopen(fn, RTLD_NOW | RTLD_LOCAL);
	if (hdl == NULL) {
		vsb_printf(tl->sb, "Could not load module %.*s\n\t%s\n\t%s\n", 
		    PF(mod), fn, dlerror());
		vcc_ErrWhere(tl, mod);
		return;
	}

	modname = dlsym(hdl, "Vmod_Name");
	if (modname == NULL) {
		vsb_printf(tl->sb, "Could not load module %.*s\n\t%s\n\t%s\n", 
		    PF(mod), fn, "Symbol Vmod_Name not found");
		vcc_ErrWhere(tl, mod);
		return;
	}
	if (!vcc_IdIs(mod, modname)) {
		vsb_printf(tl->sb, "Could not load module %.*s\n\t%s\n", 
		    PF(mod), fn);
		vsb_printf(tl->sb, "\tModule has wrong name: <%s>\n", modname);
		vcc_ErrWhere(tl, mod);
		return;
	}

	proto = dlsym(hdl, "Vmod_Proto");
	if (modname == NULL) {
		vsb_printf(tl->sb, "Could not load module %.*s\n\t%s\n\t%s\n", 
		    PF(mod), fn, "Symbol Vmod_Proto not found");
		vcc_ErrWhere(tl, mod);
		return;
	}
	spec = dlsym(hdl, "Vmod_Spec");
	if (modname == NULL) {
		vsb_printf(tl->sb, "Could not load module %.*s\n\t%s\n\t%s\n", 
		    PF(mod), fn, "Symbol Vmod_Spec not found");
		vcc_ErrWhere(tl, mod);
		return;
	}
	for (; *spec != NULL; spec++) {
		p = *spec;
		if (!strcmp(p, "META")) {
			p += strlen(p) + 1;
			Fh(tl, 0, "static void *vmod_priv_%.*s;\n", PF(mod));
			Fi(tl, 0, "\t%s(&vmod_priv_%.*s, &VCL_conf);\n", p, PF(mod));
			Ff(tl, 0, "\t%s(&vmod_priv_%.*s, 0);\n", p, PF(mod));
		} else {
			sym = VCC_AddSymbol(tl, p);
			p += strlen(p) + 1;
			sym->cfunc = p;
			p += strlen(p) + 1;
			sym->args = p;
		}
	}
	Fh(tl, 0, "\n%s\n", proto);
}
