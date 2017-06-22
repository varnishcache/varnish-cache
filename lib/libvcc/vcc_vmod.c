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

#include "libvcc.h"
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
	struct symbol *msym;
	const struct symbol *osym;
	const char *p;
	// int *modlen;
	const struct vmod_data *vmd;

	t1 = tl->t;
	SkipToken(tl, ID);		/* "import" */

	ExpectErr(tl, ID);
	mod = tl->t;
	vcc_NextToken(tl);

	osym = VCC_SymbolTok(tl, NULL, mod, SYM_NONE, 0);
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
	msym = VCC_Symbol(tl, NULL, fn, NULL, SYM_VMOD, 1);
	ERRCHK(tl);
	AN(msym);
	msym->def_b = t1;
	msym->def_e = tl->t;

	if (tl->t->tok == ID) {
		if (!vcc_IdIs(tl->t, "from")) {
			VSB_printf(tl->sb, "Expected 'from path ...'\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		vcc_NextToken(tl);
		if (!tl->unsafe_path && strchr(tl->t->dec, '/')) {
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

	if (VFIL_searchpath(tl->vmod_path,
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

	VSB_printf(ifp->ini, "\tif (VRT_Vmod_Init(ctx,\n");
	VSB_printf(ifp->ini, "\t    &VGC_vmod_%.*s,\n", PF(mod));
	VSB_printf(ifp->ini, "\t    &Vmod_%.*s_Func,\n", PF(mod));
	VSB_printf(ifp->ini, "\t    sizeof(Vmod_%.*s_Func),\n", PF(mod));
	VSB_printf(ifp->ini, "\t    \"%.*s\",\n", PF(mod));
	VSB_printf(ifp->ini, "\t    ");
	VSB_quote(ifp->ini, fnp, -1, VSB_QUOTE_CSTR);
	VSB_printf(ifp->ini, ",\n");
	AN(vmd);
	AN(vmd->file_id);
	VSB_printf(ifp->ini, "\t    \"%s\",\n", vmd->file_id);
	VSB_printf(ifp->ini, "\t    \"./vmod_cache/_vmod_%.*s.%s\"\n",
	    PF(mod), vmd->file_id);
	VSB_printf(ifp->ini, "\t    ))\n");
	VSB_printf(ifp->ini, "\t\treturn(1);");

	VSB_printf(tl->fi, "%s VMOD %s ./vmod_cache/_vmod_%.*s.%s */\n",
	    VCC_INFO_PREFIX, fnp, PF(mod), vmd->file_id);

	/* XXX: zero the function pointer structure ?*/
	VSB_printf(ifp->fin, "\t\tVRT_priv_fini(&vmod_priv_%.*s);\n", PF(mod));
	VSB_printf(ifp->fin, "\t\t\tVRT_Vmod_Fini(&VGC_vmod_%.*s);", PF(mod));

	ifp = NULL;

	spec = vmd->spec;
	for (; *spec != NULL; spec++) {
		p = *spec;
		if (!strcmp(p, "$OBJ")) {
			p += strlen(p) + 1;
			sym = VCC_Symbol(tl, NULL, p, NULL, SYM_OBJECT, 1);
			XXXAN(sym);
			sym->extra = p;
			sym->vmod = msym->name;
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
			    "\t\t\t    VCL_EVENT_DISCARD);\n", p, PF(mod));
			VSB_printf(ifp->event, "\t%s(ctx, &vmod_priv_%.*s, ev)",
			    p, PF(mod));
		} else if (!strcmp(p, "$FUNC")) {
			p += strlen(p) + 1;
			sym = VCC_Symbol(tl, NULL, p, NULL, SYM_FUNC, 1);
			ERRCHK(tl);
			AN(sym);
			sym->vmod = msym->name;
			sym->eval = vcc_Eval_SymFunc;
			p += strlen(p) + 1;
			sym->eval_priv = p;
			sym->fmt = VCC_Type(p);
			AN(sym->fmt);
		} else {
			VSB_printf(tl->sb, "Internal spec error (%s)\n", p);
			vcc_ErrWhere(tl, mod);
			return;
		}
	}

	Fh(tl, 0, "\n/* --- BEGIN VMOD %.*s --- */\n\n", PF(mod));
	Fh(tl, 0, "static struct vmod *VGC_vmod_%.*s;\n", PF(mod));
	Fh(tl, 0, "static struct vmod_priv vmod_priv_%.*s;\n", PF(mod));
	Fh(tl, 0, "\n%s\n", vmd->proto);
	Fh(tl, 0, "\n/* --- END VMOD %.*s --- */\n\n", PF(mod));
}

void
vcc_ParseNew(struct vcc *tl)
{
	struct symbol *sy1, *sy2, *sy3;
	struct inifin *ifp;
	const char *p, *s_obj;
	char buf1[128];
	char buf2[128];

	vcc_NextToken(tl);
	ExpectErr(tl, ID);
	vcc_ExpectCid(tl, "VCL object");
	ERRCHK(tl);
	sy1 = VCC_HandleSymbol(tl, tl->t, INSTANCE, "vo");
	ERRCHK(tl);

	/* We allow implicit use of VMOD objects:  Pretend it's ref'ed */
	sy1->nref++;

	vcc_NextToken(tl);

	ExpectErr(tl, '=');
	vcc_NextToken(tl);

	ExpectErr(tl, ID);
	sy2 = VCC_SymbolTok(tl, NULL, tl->t, SYM_OBJECT, 0);
	if (sy2 == NULL) {
		VSB_printf(tl->sb, "Symbol not found: ");
		vcc_ErrToken(tl, tl->t);
		VSB_printf(tl->sb, " at ");
		vcc_ErrWhere(tl, tl->t);
		return;
	}
	vcc_NextToken(tl);

	p = sy2->extra;

	s_obj = p;
	p += strlen(p) + 1;

	Fh(tl, 0, "static %s *%s;\n\n", p, sy1->rname);
	p += strlen(p) + 1;

	bprintf(buf1, ", &%s, \"%s\"", sy1->rname, sy1->name);
	vcc_Eval_Func(tl, p, buf1, sy2);
	ExpectErr(tl, ';');

	while (p[0] != '\0' || p[1] != '\0' || p[2] != '\0')
		p++;
	p += 3;

	ifp = New_IniFin(tl);
	p += strlen(p) + 1;
	VSB_printf(ifp->fin, "\t\t%s(&%s);", p, sy1->rname);

	while (p[0] != '\0' || p[1] != '\0' || p[2] != '\0')
		p++;
	p += 3;

	/* Instantiate symbols for the methods */
	bprintf(buf1, ", %s", sy1->rname);
	while (*p != '\0') {
		p += strlen(s_obj);
		bprintf(buf2, "%s%s", sy1->name, p);
		sy3 = VCC_Symbol(tl, NULL, buf2, NULL, SYM_FUNC, 1);
		AN(sy3);
		sy3->eval = vcc_Eval_SymFunc;
		p += strlen(p) + 1;
		sy3->eval_priv = p;
		sy3->fmt = VCC_Type(p);
		sy3->extra = TlDup(tl, buf1);
		sy3->vmod = sy2->vmod;
		while (p[0] != '\0' || p[1] != '\0' || p[2] != '\0')
			p++;
		p += 3;
	}
	sy1->def_e = tl->t;
}
