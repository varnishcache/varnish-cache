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
 */

#include "config.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vcc_compile.h"

#include "vcs_version.h"

#include "vfil.h"
#include "vmod_abi.h"
#include "vrt.h"

static int
vcc_path_dlopen(void *priv, const char *fn)
{
	void *hdl, **pp;

	AN(priv);
	AN(fn);

	hdl = dlopen(fn, RTLD_NOW | RTLD_LOCAL);
	if (hdl == NULL)
		return (1);
	pp = priv;
	*pp = hdl;
	return (0);
}

void
vcc_ParseImport(struct vcc *tl)
{
	void *hdl;
	char fn[1024], *fnp, *fnpx;
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
		if (!vcc_IdIs(tl->t, "from")) {
			VSB_printf(tl->sb, "Expected 'from path ...'\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		vcc_NextToken(tl);
		if (!tl->param->unsafe_path && strchr(tl->t->dec, '/')) {
			VSB_printf(tl->sb,
			    "'import ... from path ...' is unsafe.\nAt:");
			vcc_ErrToken(tl, tl->t);
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		ExpectErr(tl, CSTR);
		p = strrchr(tl->t->dec, '/');
		if (p != NULL && p[1] == '\0')
			bprintf(fn, "%slibvmod_%.*s.so", tl->t->dec, PF(mod));
		else
			bprintf(fn, "%s", tl->t->dec);
		vcc_NextToken(tl);
	} else {
		bprintf(fn, "libvmod_%.*s.so", PF(mod));
	}

	SkipToken(tl, ';');

	if (VFIL_searchpath(tl->param->vmod_path,
	    vcc_path_dlopen, &hdl, fn, &fnpx)) {
		VSB_printf(tl->sb, "Could not load VMOD %.*s\n", PF(mod));
		VSB_printf(tl->sb, "\tFile name: %s\n",
		    fnpx != NULL ? fnpx : fn);
		VSB_printf(tl->sb, "\tdlerror: %s\n", dlerror());
		vcc_ErrWhere(tl, mod);
		free(fnpx);
		return;
	}

	AN(fnpx);
	fnp = TlDup(tl, fnpx);
	free(fnpx);

	bprintf(buf, "Vmod_%.*s_Data", PF(mod));
	vmd = dlsym(hdl, buf);
	if (vmd == NULL) {
		VSB_printf(tl->sb, "Malformed VMOD %.*s\n", PF(mod));
		VSB_printf(tl->sb, "\tFile name: %s\n", fnp);
		VSB_printf(tl->sb, "\t(no Vmod_Data symbol)\n");
		vcc_ErrWhere(tl, mod);
		return;
	}
	if (strcmp(VCS_Branch, "master") == 0 &&
	    strcmp(vmd->abi, VMOD_ABI_Version) != 0) {
		VSB_printf(tl->sb, "Incompatible VMOD %.*s\n", PF(mod));
		VSB_printf(tl->sb, "\tFile name: %s\n", fnp);
		VSB_printf(tl->sb, "\tABI mismatch, expected <%s>, got <%s>\n",
			   VMOD_ABI_Version, vmd->abi);
		vcc_ErrWhere(tl, mod);
		return;
	}
	if (vmd->vrt_major != VRT_MAJOR_VERSION ||
	    vmd->vrt_minor > VRT_MINOR_VERSION) {
		VSB_printf(tl->sb, "Incompatible VMOD %.*s\n", PF(mod));
		VSB_printf(tl->sb, "\tFile name: %s\n", fnp);
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
		VSB_printf(tl->sb, "\tFile name: %s\n", fnp);
		VSB_printf(tl->sb, "\tInconsistent metadata\n");
		vcc_ErrWhere(tl, mod);
		return;
	}

	if (!vcc_IdIs(mod, vmd->name)) {
		VSB_printf(tl->sb, "Wrong VMOD file %.*s\n", PF(mod));
		VSB_printf(tl->sb, "\tFile name: %s\n", fnp);
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
	EncString(ifp->ini, fnp, NULL, 0);
	VSB_printf(ifp->ini, ",\n");
	AN(vmd);
	AN(vmd->file_id);
	VSB_printf(ifp->ini, "\t    \"%s\",\n", vmd->file_id);
	VSB_printf(ifp->ini, "\t    ctx))\n");
	VSB_printf(ifp->ini, "\t\treturn(1);");

	/* XXX: zero the function pointer structure ?*/
	VSB_printf(ifp->fin, "\t\tVRT_priv_fini(&vmod_priv_%.*s);", PF(mod));
	VSB_printf(ifp->fin, "\n\t\tVRT_Vmod_Fini(&VGC_vmod_%.*s);", PF(mod));

	ifp = NULL;

	spec = vmd->spec;
	for (; *spec != NULL; spec++) {
		p = *spec;
		if (!strcmp(p, "$OBJ")) {
			p += strlen(p) + 1;
			sym = VCC_AddSymbolStr(tl, p, SYM_OBJECT);
			XXXAN(sym);
			sym->args = p;
		} else if (!strcmp(p, "$EVENT")) {
			p += strlen(p) + 1;
			if (ifp == NULL)
				ifp = New_IniFin(tl);
			VSB_printf(ifp->ini,
			    "\tif (%s(ctx, &vmod_priv_%.*s, VCL_EVENT_LOAD))\n"
			    "\t\treturn(1);",
			    p, PF(mod));
			VSB_printf(ifp->fin,
			    "\t\t(void)%s(ctx, &vmod_priv_%.*s,\n"
			    "\t\t    VCL_EVENT_DISCARD);\n", p, PF(mod));
			VSB_printf(ifp->event, "\t%s(ctx, &vmod_priv_%.*s, ev)",
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
	Fh(tl, 0, "static struct vmod *VGC_vmod_%.*s;\n", PF(mod));
	Fh(tl, 0, "static struct vmod_priv vmod_priv_%.*s;\n", PF(mod));
	Fh(tl, 0, "\n%s\n", vmd->proto);
	Fh(tl, 0, "\n/* --- END VMOD %.*s --- */\n\n", PF(mod));
}
