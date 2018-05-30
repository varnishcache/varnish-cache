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
#include <stdlib.h>
#include <string.h>

#include "vcc_compile.h"

#include "libvcc.h"
#include "vfil.h"
#include "vjsn.h"
#include "vmod_abi.h"

static int
vcc_path_dlopen(void *priv, const char *fn)
{
	void *hdl, **pp;

	AN(priv);
	AN(fn);

	hdl = dlopen(fn, RTLD_NOW | RTLD_LOCAL);
	if (hdl == NULL)
		return (-1);
	pp = priv;
	*pp = hdl;
	return (0);
}

static void
func_sym(struct symbol *sym, const char *vmod, const struct vjsn_val *v)
{

	assert(v->type == VJSN_ARRAY);
	sym->action = vcc_Act_Call;
	sym->vmod = vmod;
	sym->eval = vcc_Eval_SymFunc;
	sym->eval_priv = v;
	v = VTAILQ_FIRST(&v->children);
	assert(v->type == VJSN_ARRAY);
	v = VTAILQ_FIRST(&v->children);
	assert(v->type == VJSN_STRING);
	sym->type = VCC_Type(v->value);
	AN(sym->type);
}

static void
vcc_json_always(struct vcc *tl, const struct symbol *msym)
{
	struct inifin *ifp;
	const struct vjsn *vj;
	const struct vjsn_val *vv, *vv2;
	double vmod_syntax = 0.0;

	ifp = NULL;

	CAST_OBJ_NOTNULL(vj, msym->eval_priv, VJSN_MAGIC);

	VTAILQ_FOREACH(vv, &vj->value->children, list) {
		assert(vv->type == VJSN_ARRAY);
		vv2 = VTAILQ_FIRST(&vv->children);
		assert(vv2->type == VJSN_STRING);
		if (!strcmp(vv2->value, "$VMOD")) {
			vmod_syntax =
			    strtod(VTAILQ_NEXT(vv2, list)->value, NULL);
			continue;
		}
		assert (vmod_syntax == 1.0);
		if (!strcmp(vv2->value, "$EVENT")) {
			/* XXX: What about the rest of the events ? */
			if (ifp == NULL)
				ifp = New_IniFin(tl);
			vv2 = VTAILQ_NEXT(vv2, list);
			VSB_printf(ifp->ini,
			    "\tif (%s(ctx, &vmod_priv_%s, VCL_EVENT_LOAD))\n"
			    "\t\treturn(1);",
			    vv2->value, msym->name);
			VSB_printf(ifp->fin,
			    "\t\t(void)%s(ctx, &vmod_priv_%s,\n"
			    "\t\t\t    VCL_EVENT_DISCARD);\n",
			    vv2->value, msym->name);
			VSB_printf(ifp->event, "%s(ctx, &vmod_priv_%s, ev)",
			    vv2->value, msym->name);
		} else if (!strcmp(vv2->value, "$FUNC")) {
		} else if (!strcmp(vv2->value, "$OBJ")) {
		} else {
			VTAILQ_FOREACH(vv2, &vv->children, list)
				fprintf(stderr, "\tt %s n %s v %s\n",
				    vv2->type, vv2->name, vv2->value);
			WRONG("Vmod JSON syntax error");
		}
	}
}

static void v_matchproto_(sym_wildcard_t)
vcc_json_wildcard(struct vcc *tl, struct symbol *msym, struct symbol *tsym)
{
	const struct vjsn *vj;
	const struct vjsn_val *vv, *vv1, *vv2;

	assert(msym->kind == SYM_VMOD);
	CAST_OBJ_NOTNULL(vj, msym->eval_priv, VJSN_MAGIC);
	VTAILQ_FOREACH(vv, &vj->value->children, list) {
		assert(vv->type == VJSN_ARRAY);
		vv1 = VTAILQ_FIRST(&vv->children);
		assert(vv1->type == VJSN_STRING);
		vv2 = VTAILQ_NEXT(vv1, list);
		assert(vv2->type == VJSN_STRING);
		if (!strcmp(vv1->value, "$FUNC") &&
		    !strcmp(vv2->value, tsym->name)) {
			tsym->kind = SYM_FUNC;
			tsym->noref = 1;
			func_sym(tsym, msym->name, VTAILQ_NEXT(vv2, list));
			return;
		} else if (!strcmp(vv1->value, "$OBJ") &&
			   !strcmp(vv2->value, tsym->name)) {
			tsym->kind = SYM_OBJECT;
			tsym->eval_priv = vv2;
			tsym->vmod = msym->name;
			return;
		}
	}
	tl->err = 1;
}

void
vcc_ParseImport(struct vcc *tl)
{
	void *hdl;
	char fn[1024], *fnp, *fnpx;
	char buf[256];
	const char *p;
	struct token *mod, *t1;
	struct inifin *ifp;
	struct symbol *msym;
	const struct vmod_data *vmd;
	struct vjsn *vj;
	int again = 0;

	t1 = tl->t;
	SkipToken(tl, ID);		/* "import" */


	ExpectErr(tl, ID);
	mod = tl->t;
	msym = VCC_SymbolGet(tl, SYM_NONE, SYMTAB_NOERR, XREF_NONE);

	if (msym != NULL && msym->kind != SYM_VMOD) {
		/*
		 * We need to make sure the entire std.* namespace is empty
		 */
		VSB_printf(tl->sb, "Module %.*s conflicts with other symbol.\n",
		    PF(mod));
		vcc_ErrWhere2(tl, t1, tl->t);
		return;
	}
	if (msym != NULL) {
		again = 1;
	} else {

		msym = VCC_SymbolGet(tl, SYM_VMOD, SYMTAB_CREATE, XREF_NONE);
		ERRCHK(tl);
		AN(msym);
		msym->def_b = t1;
		msym->def_e = tl->t;
	}

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

	if (!again)
		msym->def_e = tl->t;


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
	if (vmd->vrt_major == 0 && vmd->vrt_minor == 0 &&
	    strcmp(vmd->abi, VMOD_ABI_Version) != 0) {
		VSB_printf(tl->sb, "Incompatible VMOD %.*s\n", PF(mod));
		VSB_printf(tl->sb, "\tFile name: %s\n", fnp);
		VSB_printf(tl->sb, "\tABI mismatch, expected <%s>, got <%s>\n",
			   VMOD_ABI_Version, vmd->abi);
		vcc_ErrWhere(tl, mod);
		return;
	}
	if (vmd->vrt_major != 0 && (vmd->vrt_major != VRT_MAJOR_VERSION ||
	    vmd->vrt_minor > VRT_MINOR_VERSION)) {
		VSB_printf(tl->sb, "Incompatible VMOD %.*s\n", PF(mod));
		VSB_printf(tl->sb, "\tFile name: %s\n", fnp);
		VSB_printf(tl->sb, "\tVMOD wants ABI version %u.%u\n",
		    vmd->vrt_major, vmd->vrt_minor);
		VSB_printf(tl->sb, "\tvarnishd provices ABI version %u.%u\n",
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

	if (again && strcmp(vmd->file_id, msym->extra)) {
		VSB_printf(tl->sb,
		    "Different version of module %.*s already imported.\n",
		    PF(mod));
		vcc_ErrWhere2(tl, t1, tl->t);
		VSB_printf(tl->sb, "Previous import was here:\n");
		vcc_ErrWhere2(tl, msym->def_b, msym->def_e);
	}
	if (again) {
		AZ(dlclose(hdl));
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

	vj = vjsn_parse(vmd->json, &p);
	XXXAZ(p);
	AN(vj);
	msym->eval_priv = vj;
	msym->wildcard = vcc_json_wildcard;
	msym->extra = TlDup(tl, vmd->file_id);

	vcc_json_always(tl, msym);

	Fh(tl, 0, "\n/* --- BEGIN VMOD %.*s --- */\n\n", PF(mod));
	Fh(tl, 0, "static struct vmod *VGC_vmod_%.*s;\n", PF(mod));
	Fh(tl, 0, "static struct vmod_priv vmod_priv_%.*s;\n", PF(mod));
	Fh(tl, 0, "\n%s\n", vmd->proto);
	Fh(tl, 0, "\n/* --- END VMOD %.*s --- */\n\n", PF(mod));
}

void v_matchproto_(sym_act_f)
vcc_Act_New(struct vcc *tl, struct token *t, struct symbol *sym)
{
	struct symbol *sy1, *sy2, *sy3;
	struct inifin *ifp;
	char buf1[128];
	char buf2[128];
	const struct vjsn_val *vv, *vf;
	const char *p;

	(void)sym;
	ExpectErr(tl, ID);
	vcc_ExpectVid(tl, "VCL object");
	ERRCHK(tl);
	sy1 = VCC_HandleSymbol(tl, INSTANCE, "vo");
	ERRCHK(tl);
	AN(sy1);
	sy1->noref = 1;

	ExpectErr(tl, '=');
	vcc_NextToken(tl);

	ExpectErr(tl, ID);
	t = tl->t;
	sy2 = VCC_SymbolGet(tl, SYM_OBJECT, "Symbol not found", XREF_NONE);
	ERRCHK(tl);
	AN(sy2);
	if (sy2->eval_priv == NULL) {
		VSB_printf(tl->sb, "Constructor not found: ");
		vcc_ErrToken(tl, t);
		VSB_printf(tl->sb, " at ");
		vcc_ErrWhere(tl, t);
		return;
	}

	CAST_OBJ_NOTNULL(vv, sy2->eval_priv, VJSN_VAL_MAGIC);

	vv = VTAILQ_NEXT(vv, list);

	Fh(tl, 0, "static %s *%s;\n\n", vv->value, sy1->rname);
	vv = VTAILQ_NEXT(vv, list);

	vf = VTAILQ_FIRST(&vv->children);
	vv = VTAILQ_NEXT(vv, list);
	assert(vf->type == VJSN_STRING);
	assert(!strcmp(vf->value, "$INIT"));

	vf = VTAILQ_NEXT(vf, list);

	bprintf(buf1, ", &%s, \"%s\"", sy1->rname, sy1->name);
	vcc_Eval_Func(tl, vf, buf1, sy2);
	ERRCHK(tl);
	SkipToken(tl, ';');
	sy1->def_e = tl->t;

	vf = VTAILQ_FIRST(&vv->children);
	vv = VTAILQ_NEXT(vv, list);
	assert(vf->type == VJSN_STRING);
	assert(!strcmp(vf->value, "$FINI"));

	vf = VTAILQ_NEXT(vf, list);
	vf = VTAILQ_FIRST(&vf->children);
	vf = VTAILQ_NEXT(vf, list);
	ifp = New_IniFin(tl);
	VSB_printf(ifp->fin, "\t\t%s(&%s);", vf->value, sy1->rname);

	/* Instantiate symbols for the methods */
	bprintf(buf1, ", %s", sy1->rname);
	p = TlDup(tl, buf1);
	while (vv != NULL) {
		vf = VTAILQ_FIRST(&vv->children);
		assert(vf->type == VJSN_STRING);
		assert(!strcmp(vf->value, "$METHOD"));
		vf = VTAILQ_NEXT(vf, list);
		assert(vf->type == VJSN_STRING);

		bprintf(buf2, "%s.%s", sy1->name, vf->value);
		sy3 = VCC_MkSym(tl, buf2, SYM_FUNC, VCL_LOW, VCL_HIGH);
		AN(sy3);
		func_sym(sy3, sy2->vmod, VTAILQ_NEXT(vf, list));
		sy3->extra = p;
		vv = VTAILQ_NEXT(vv, list);
	}
}
