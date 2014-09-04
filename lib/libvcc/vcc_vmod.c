/*-
 * Copyright (c) 2010-2014 Varnish Software AS
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

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#include "vcc_compile.h"

#include "vcs_version.h"

#include "vmod_abi.h"
#include "vrt.h"

void
vcc_ParseImport(struct vcc *tl)
{
	void *hdl;
	char fn[1024];
	char buf[256];
	struct token *mod, *t1;
	struct inifin *ifp;
	const char * const *spec;
	struct symbol *sym;
	const struct symbol *osym;
	const char *p;
	// int *modlen;
	const struct vmod_data *vmd;

	t1 = tl->t;
	SkipToken(tl, ID);		/* "import" */

	ExpectErr(tl, ID);
	mod = tl->t;
	vcc_NextToken(tl);

	osym = VCC_FindSymbol(tl, mod, SYM_NONE);
	if (osym != NULL && osym->kind != SYM_VMOD) {
		VSB_printf(tl->sb, "Module %.*s conflicts with other symbol.\n",
		    PF(mod));
		vcc_ErrWhere2(tl, t1, tl->t);
		return;
	}
	if (osym != NULL) {
		VSB_printf(tl->sb, "Module %.*s already imported.\n",
		    PF(mod));
		vcc_ErrWhere2(tl, t1, tl->t);
		VSB_printf(tl->sb, "Previous import was here:\n");
		vcc_ErrWhere2(tl, osym->def_b, osym->def_e);
		return;
	}

	bprintf(fn, "%.*s", PF(mod));
	sym = VCC_AddSymbolStr(tl, fn, SYM_VMOD);
	ERRCHK(tl);
	AN(sym);
	sym->def_b = t1;
	sym->def_e = tl->t;

	if (tl->t->tok == ID) {
		if (!tl->unsafe_path) {
			VSB_printf(tl->sb,
			    "'import ... from path ...' not allowed.\nAt:");
			vcc_ErrToken(tl, tl->t);
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		if (!vcc_IdIs(tl->t, "from")) {
			VSB_printf(tl->sb, "Expected 'from path ...'\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		vcc_NextToken(tl);
		ExpectErr(tl, CSTR);
		bprintf(fn, "%s", tl->t->dec);
		vcc_NextToken(tl);
	} else {
		bprintf(fn, "%s/libvmod_%.*s.so", tl->vmod_dir, PF(mod));
	}

	SkipToken(tl, ';');

	hdl = dlopen(fn, RTLD_NOW | RTLD_LOCAL);
	if (hdl == NULL) {
		VSB_printf(tl->sb, "Could not load VMOD %.*s\n", PF(mod));
		VSB_printf(tl->sb, "\tFile name: %s\n", fn);
		VSB_printf(tl->sb, "\tdlerror:: %s\n", dlerror());
		vcc_ErrWhere(tl, mod);
		return;
	}

	bprintf(buf, "Vmod_%.*s_Data", PF(mod));
	vmd = dlsym(hdl, buf);
	if (vmd == NULL) {
		VSB_printf(tl->sb, "Malformed VMOD %.*s\n", PF(mod));
		VSB_printf(tl->sb, "\tFile name: %s\n", fn);
		VSB_printf(tl->sb, "\t(no Vmod_Data symbol)\n");
		vcc_ErrWhere(tl, mod);
		return;
	}
	if (strcmp(VCS_Branch, "master") == 0 &&
	    strcmp(vmd->abi, VMOD_ABI_Version) != 0) {
		VSB_printf(tl->sb, "Incompatible VMOD %.*s\n", PF(mod));
		VSB_printf(tl->sb, "\tFile name: %s\n", fn);
		VSB_printf(tl->sb, "\tABI mismatch, expected <%s>, got <%s>\n",
			   VMOD_ABI_Version, vmd->abi);
		vcc_ErrWhere(tl, mod);
		return;
	}
	if (vmd->vrt_major != VRT_MAJOR_VERSION ||
	    vmd->vrt_minor > VRT_MINOR_VERSION) {
		VSB_printf(tl->sb, "Incompatible VMOD %.*s\n", PF(mod));
		VSB_printf(tl->sb, "\tFile name: %s\n", fn);
		VSB_printf(tl->sb, "\tVMOD version %u.%u\n",
		    vmd->vrt_major, vmd->vrt_minor);
		VSB_printf(tl->sb, "\tvarnishd version %u.%u\n",
		    VRT_MAJOR_VERSION, VRT_MINOR_VERSION);
		vcc_ErrWhere(tl, mod);
		return;
	}
	if (vmd->name == NULL ||
	    vmd->func == NULL ||
	    vmd->func_len <= 0 ||
	    vmd->proto == NULL ||
	    vmd->abi == NULL) {
		VSB_printf(tl->sb, "Mangled VMOD %.*s\n", PF(mod));
		VSB_printf(tl->sb, "\tFile name: %s\n", fn);
		VSB_printf(tl->sb, "\tInconsistent metadata\n");
		vcc_ErrWhere(tl, mod);
		return;
	}

	if (!vcc_IdIs(mod, vmd->name)) {
		VSB_printf(tl->sb, "Wrong VMOD file %.*s\n", PF(mod));
		VSB_printf(tl->sb, "\tFile name: %s\n", fn);
		VSB_printf(tl->sb, "\tContains vmod \"%s\"\n", vmd->name);
		vcc_ErrWhere(tl, mod);
		return;
	}

	ifp = New_IniFin(tl);

	VSB_printf(ifp->ini, "\tif (VRT_Vmod_Init(&VGC_vmod_%.*s,\n", PF(mod));
	VSB_printf(ifp->ini, "\t    &Vmod_%.*s_Func,\n", PF(mod));
	VSB_printf(ifp->ini, "\t    sizeof(Vmod_%.*s_Func),\n", PF(mod));
	VSB_printf(ifp->ini, "\t    \"%.*s\",\n", PF(mod));
	VSB_printf(ifp->ini, "\t    ");
	EncString(ifp->ini, fn, NULL, 0);
	VSB_printf(ifp->ini, ",\n");
	AN(vmd);
	AN(vmd->file_id);
	VSB_printf(ifp->ini, "\t    \"%s\",\n", vmd->file_id);
	VSB_printf(ifp->ini, "\t    cli))\n");
	VSB_printf(ifp->ini, "\t\treturn(1);");

	/* XXX: zero the function pointer structure ?*/
	VSB_printf(ifp->fin, "\tVRT_priv_fini(&vmod_priv_%.*s);", PF(mod));
	VSB_printf(ifp->fin, "\n\tVRT_Vmod_Fini(&VGC_vmod_%.*s);", PF(mod));

	ifp = NULL;

	spec = vmd->spec;
	for (; *spec != NULL; spec++) {
		p = *spec;
		if (!strcmp(p, "OBJ")) {
			p += strlen(p) + 1;
			sym = VCC_AddSymbolStr(tl, p, SYM_OBJECT);
			XXXAN(sym);
			sym->args = p;
		} else if (!strcmp(p, "INIT")) {
			p += strlen(p) + 1;
			if (ifp == NULL)
				ifp = New_IniFin(tl);
			VSB_printf(ifp->ini,
			    "\t%s(&vmod_priv_%.*s, &VCL_conf);",
			    p, PF(mod));
		} else {
			sym = VCC_AddSymbolStr(tl, p, SYM_FUNC);
			ERRCHK(tl);
			AN(sym);
			sym->eval = vcc_Eval_SymFunc;
			p += strlen(p) + 1;
			sym->cfunc = p;
			p += strlen(p) + 1;
			sym->args = p;

			/* Functions which return VOID are procedures */
			if (!memcmp(p, "VOID\0", 5))
				sym->kind = SYM_PROC;
		}
	}

	Fh(tl, 0, "\n/* --- BEGIN VMOD %.*s --- */\n\n", PF(mod));
	Fh(tl, 0, "static void *VGC_vmod_%.*s;\n", PF(mod));
	Fh(tl, 0, "static struct vmod_priv vmod_priv_%.*s;\n", PF(mod));
	Fh(tl, 0, "\n%s\n", vmd->proto);
	Fh(tl, 0, "\n/* --- END VMOD %.*s --- */\n\n", PF(mod));
}
