/*-
 * Copyright (c) 2010-2015 Varnish Software AS
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

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

#include "vcc_compile.h"

#include "libvcc.h"
#include "vfil.h"
#include "vjsn.h"
#include "vmod_abi.h"
#include "vsb.h"

struct vmod_open {
	unsigned		magic;
#define VMOD_OPEN_MAGIC		0x9995b1f3
	void			*hdl;
	const char		*err;
};

struct vmod_obj {
	unsigned		magic;
#define VMOD_OBJ_MAGIC		0x349885f8
	char			*name;
	struct type		type[1];
	VTAILQ_ENTRY(vmod_obj)	list;
};

static int
vcc_path_dlopen(void *priv, const char *fn)
{
	struct vmod_open *vop;

	CAST_OBJ_NOTNULL(vop, priv, VMOD_OPEN_MAGIC);
	AN(fn);

	vop->hdl = dlopen(fn, RTLD_NOW | RTLD_LOCAL);
	if (vop->hdl == NULL) {
		vop->err = dlerror();
		return (-1);
	}
	return (0);
}

static void vcc_VmodObject(struct vcc *tl, struct symbol *sym);
static void vcc_VmodSymbols(struct vcc *tl, const struct symbol *sym);

static void
func_sym(struct vcc *tl, vcc_kind_t kind, const struct symbol *psym,
    const struct vjsn_val *v)
{
	struct symbol *sym;
	struct vsb *buf;

	buf = VSB_new_auto();
	AN(buf);

	VSB_clear(buf);
	VCC_SymName(buf, psym);
	VSB_printf(buf, ".%s", v->value);
	AZ(VSB_finish(buf));
	sym = VCC_MkSym(tl, VSB_data(buf), SYM_MAIN, kind, VCL_LOW, VCL_HIGH);
	AN(sym);
	VSB_destroy(&buf);

	if (kind == SYM_OBJECT) {
		sym->eval_priv = v;
		sym->vmod_name = psym->vmod_name;
		sym->r_methods = VCL_MET_INIT;
		vcc_VmodObject(tl, sym);
		vcc_VmodSymbols(tl, sym);
		return;
	}

	if (kind == SYM_METHOD)
		sym->extra = psym->rname;

	v = VTAILQ_NEXT(v, list);

	assert(v->type == VJSN_ARRAY);
	sym->action = vcc_Act_Call;
	sym->vmod_name = psym->vmod_name;
	sym->eval = vcc_Eval_SymFunc;
	sym->eval_priv = v;
	v = VTAILQ_FIRST(&v->children);
	assert(v->type == VJSN_ARRAY);
	v = VTAILQ_FIRST(&v->children);
	assert(v->type == VJSN_STRING);
	sym->type = VCC_Type(v->value);
	AN(sym->type);
	sym->r_methods = VCL_MET_TASK_ALL;
}

static void
vcc_json_always(struct vcc *tl, const struct vjsn *vj, const char *vmod_name)
{
	struct inifin *ifp;
	const struct vjsn_val *vv, *vv2;
	double vmod_syntax = 0.0;

	AN(vj);
	AN(vmod_name);
	ifp = NULL;

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
			    vv2->value, vmod_name);
			VSB_printf(ifp->fin,
			    "\t\t(void)%s(ctx, &vmod_priv_%s,\n"
			    "\t\t\t    VCL_EVENT_DISCARD);",
			    vv2->value, vmod_name);
			VSB_printf(ifp->event, "%s(ctx, &vmod_priv_%s, ev)",
			    vv2->value, vmod_name);
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

static const struct vmod_data *
vcc_VmodSanity(struct vcc *tl, void *hdl, const struct token *mod, char *fnp)
{
	char buf[256];
	const struct vmod_data *vmd;

	bprintf(buf, "Vmod_%.*s_Data", PF(mod));
	vmd = dlsym(hdl, buf);
	if (vmd == NULL) {
		VSB_printf(tl->sb, "Malformed VMOD %.*s\n", PF(mod));
		VSB_printf(tl->sb, "\tFile name: %s\n", fnp);
		VSB_cat(tl->sb, "\t(no Vmod_Data symbol)\n");
		vcc_ErrWhere(tl, mod);
		return (NULL);
	}
	if (vmd->vrt_major == 0 && vmd->vrt_minor == 0 &&
	    (vmd->abi == NULL || strcmp(vmd->abi, VMOD_ABI_Version) != 0)) {
		VSB_printf(tl->sb, "Incompatible VMOD %.*s\n", PF(mod));
		VSB_printf(tl->sb, "\tFile name: %s\n", fnp);
		VSB_printf(tl->sb, "\tABI mismatch, expected <%s>, got <%s>\n",
			   VMOD_ABI_Version, vmd->abi);
		vcc_ErrWhere(tl, mod);
		return (NULL);
	}
	if (vmd->vrt_major != 0 && (vmd->vrt_major != VRT_MAJOR_VERSION ||
	    vmd->vrt_minor > VRT_MINOR_VERSION)) {
		VSB_printf(tl->sb, "Incompatible VMOD %.*s\n", PF(mod));
		VSB_printf(tl->sb, "\tFile name: %s\n", fnp);
		VSB_printf(tl->sb, "\tVMOD wants ABI version %u.%u\n",
		    vmd->vrt_major, vmd->vrt_minor);
		VSB_printf(tl->sb, "\tvarnishd provides ABI version %u.%u\n",
		    VRT_MAJOR_VERSION, VRT_MINOR_VERSION);
		vcc_ErrWhere(tl, mod);
		return (NULL);
	}
	if (vmd->name == NULL ||
	    vmd->func == NULL ||
	    vmd->func_len <= 0 ||
	    vmd->proto == NULL ||
	    vmd->abi == NULL) {
		VSB_printf(tl->sb, "Mangled VMOD %.*s\n", PF(mod));
		VSB_printf(tl->sb, "\tFile name: %s\n", fnp);
		VSB_cat(tl->sb, "\tInconsistent metadata\n");
		vcc_ErrWhere(tl, mod);
		return (NULL);
	}
	if (!vcc_IdIs(mod, vmd->name)) {
		VSB_printf(tl->sb, "Wrong file for VMOD %.*s\n", PF(mod));
		VSB_printf(tl->sb, "\tFile name: %s\n", fnp);
		VSB_printf(tl->sb, "\tContains vmod \"%s\"\n", vmd->name);
		vcc_ErrWhere(tl, mod);
		return (NULL);
	}
	return (vmd);
}

static vcc_kind_t
vcc_vmod_kind(const char *type)
{

#define VMOD_KIND(str, kind)		\
	do {				\
		if (!strcmp(str, type))	\
			return (kind);	\
	} while (0)
	VMOD_KIND("$OBJ", SYM_OBJECT);
	VMOD_KIND("$METHOD", SYM_METHOD);
	VMOD_KIND("$FUNC", SYM_FUNC);
#undef VMOD_KIND
	return (SYM_NONE);
}

static void
vcc_VmodObject(struct vcc *tl, struct symbol *sym)
{
	struct vmod_obj *obj;
	struct vsb *buf;

	buf = VSB_new_auto();
	AN(buf);

	VSB_printf(buf, "%s.%s", sym->vmod_name, sym->name);
	AZ(VSB_finish(buf));

	ALLOC_OBJ(obj, VMOD_OBJ_MAGIC);
	AN(obj);
	REPLACE(obj->name, VSB_data(buf));

	INIT_OBJ(obj->type, TYPE_MAGIC);
	obj->type->name = obj->name;
	sym->type = obj->type;
	VTAILQ_INSERT_TAIL(&tl->vmod_objects, obj, list);
	VSB_destroy(&buf);
}

static void
vcc_VmodSymbols(struct vcc *tl, const struct symbol *sym)
{
	const struct vjsn *vj;
	const struct vjsn_val *vv, *vv1, *vv2;
	vcc_kind_t kind;

	if (sym->kind == SYM_VMOD) {
		CAST_OBJ_NOTNULL(vj, sym->eval_priv, VJSN_MAGIC);
		vv = VTAILQ_FIRST(&vj->value->children);
	} else if (sym->kind == SYM_OBJECT) {
		CAST_OBJ_NOTNULL(vv, sym->eval_priv, VJSN_VAL_MAGIC);
	} else {
		WRONG("symbol kind");
	}

	for (; vv != NULL; vv = VTAILQ_NEXT(vv, list)) {
		if (vv->type != VJSN_ARRAY)
			continue;
		vv1 = VTAILQ_FIRST(&vv->children);
		assert(vv1->type == VJSN_STRING);
		vv2 = VTAILQ_NEXT(vv1, list);
		if (vv2->type != VJSN_STRING)
			continue;

		kind = vcc_vmod_kind(vv1->value);
		if (kind == SYM_NONE)
			continue;

		func_sym(tl, kind, sym, vv2);
	}
}

void
vcc_ParseImport(struct vcc *tl)
{
	char fn[1024], *fnpx;
	const char *p;
	struct token *mod, *tmod, *t1;
	struct inifin *ifp;
	struct symbol *msym, *vsym;
	const struct vmod_data *vmd;
	struct vjsn *vj;
	struct vmod_open vop[1];

	INIT_OBJ(vop, VMOD_OPEN_MAGIC);
	t1 = tl->t;
	SkipToken(tl, ID);		/* "import" */

	ExpectErr(tl, ID);		/* "vmod_name" */
	mod = tl->t;
	tmod = VTAILQ_NEXT(mod, list);
	if (tmod->tok == ID && vcc_IdIs(tmod, "as")) {
		vcc_NextToken(tl);		/* "vmod_name" */
		vcc_NextToken(tl);		/* "as" */
		ExpectErr(tl, ID);		/* "vcl_name" */
	}
	tmod = tl->t;

	msym = VCC_SymbolGet(tl, SYM_MAIN, SYM_VMOD, SYMTAB_CREATE, XREF_NONE);
	ERRCHK(tl);
	AN(msym);

	if (tl->t->tok == ID) {
		if (!vcc_IdIs(tl->t, "from")) {
			VSB_cat(tl->sb, "Expected 'from path ...'\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		vcc_NextToken(tl);
		if (!tl->unsafe_path && strchr(tl->t->dec, '/')) {
			VSB_cat(tl->sb,
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

	if (VFIL_searchpath(tl->vmod_path, vcc_path_dlopen, vop, fn, &fnpx)) {
		if (vop->err == NULL) {
			VSB_printf(tl->sb,
			    "Could not find VMOD %.*s\n", PF(mod));
		} else {
			VSB_printf(tl->sb,
			    "Could not open VMOD %.*s\n", PF(mod));
			VSB_printf(tl->sb, "\tFile name: %s\n",
			    fnpx != NULL ? fnpx : fn);
			VSB_printf(tl->sb, "\tdlerror: %s\n", vop->err);
		}
		vcc_ErrWhere(tl, mod);
		free(fnpx);
		return;
	}

	vmd = vcc_VmodSanity(tl, vop->hdl, mod, fnpx);
	if (vmd == NULL || tl->err) {
		AZ(dlclose(vop->hdl));
		free(fnpx);
		return;
	}

	if (msym->extra != NULL) {
		if (!strcmp(msym->extra, vmd->file_id)) {
			/* Identical import is OK */
		} else {
			VSB_printf(tl->sb,
			    "Another module already imported as %.*s.\n",
			    PF(tmod));
			vcc_ErrWhere2(tl, t1, tl->t);
		}
		AZ(dlclose(vop->hdl));
		free(fnpx);
		return;
	}
	msym->def_b = t1;
	msym->def_e = tl->t;

	VTAILQ_FOREACH(vsym, &tl->sym_vmods, sideways) {
		assert(vsym->kind == SYM_VMOD);
		if (!strcmp(vsym->extra, vmd->file_id)) {
			/* Already loaded under different name */
			msym->eval_priv = vsym->eval_priv;
			msym->extra = vsym->extra;
			msym->vmod_name = vsym->vmod_name;
			vcc_VmodSymbols(tl, msym);
			AZ(dlclose(vop->hdl));
			free(fnpx);
			return;
		}
	}

	VTAILQ_INSERT_TAIL(&tl->sym_vmods, msym, sideways);

	ifp = New_IniFin(tl);

	VSB_cat(ifp->ini, "\tif (VPI_Vmod_Init(ctx,\n");
	VSB_printf(ifp->ini, "\t    &VGC_vmod_%.*s,\n", PF(mod));
	VSB_printf(ifp->ini, "\t    %u,\n", tl->vmod_count++);
	VSB_printf(ifp->ini, "\t    &%s,\n", vmd->func_name);
	VSB_printf(ifp->ini, "\t    sizeof(%s),\n", vmd->func_name);
	VSB_printf(ifp->ini, "\t    \"%.*s\",\n", PF(mod));
	VSB_cat(ifp->ini, "\t    ");
	VSB_quote(ifp->ini, fnpx, -1, VSB_QUOTE_CSTR);
	VSB_cat(ifp->ini, ",\n");
	AN(vmd);
	AN(vmd->file_id);
	VSB_printf(ifp->ini, "\t    \"%s\",\n", vmd->file_id);
	VSB_printf(ifp->ini, "\t    \"./vmod_cache/_vmod_%.*s.%s\"\n",
	    PF(mod), vmd->file_id);
	VSB_cat(ifp->ini, "\t    ))\n");
	VSB_cat(ifp->ini, "\t\treturn(1);");

	VSB_cat(tl->symtab, ",\n    {\n");
	VSB_cat(tl->symtab, "\t\"dir\": \"import\",\n");
	VSB_cat(tl->symtab, "\t\"type\": \"$VMOD\",\n");
	VSB_printf(tl->symtab, "\t\"name\": \"%.*s\",\n", PF(mod));
	VSB_printf(tl->symtab, "\t\"file\": \"%s\",\n", fnpx);
	VSB_printf(tl->symtab, "\t\"dst\": \"./vmod_cache/_vmod_%.*s.%s\"\n",
	    PF(mod), vmd->file_id);
	VSB_cat(tl->symtab, "    }");

	/* XXX: zero the function pointer structure ?*/
	VSB_printf(ifp->fin, "\t\tVRT_priv_fini(ctx, &vmod_priv_%.*s);", PF(mod));
	VSB_printf(ifp->final, "\t\tVPI_Vmod_Unload(ctx, &VGC_vmod_%.*s);", PF(mod));

	vj = vjsn_parse(vmd->json, &p);
	XXXAZ(p);
	AN(vj);
	msym->eval_priv = vj;
	msym->extra = TlDup(tl, vmd->file_id);
	msym->vmod_name = TlDup(tl, vmd->name);
	vcc_VmodSymbols(tl, msym);

	vcc_json_always(tl, vj, msym->vmod_name);

	Fh(tl, 0, "\n/* --- BEGIN VMOD %.*s --- */\n\n", PF(mod));
	Fh(tl, 0, "static struct vmod *VGC_vmod_%.*s;\n", PF(mod));
	Fh(tl, 0, "static struct vmod_priv vmod_priv_%.*s;\n", PF(mod));
	Fh(tl, 0, "\n%s\n", vmd->proto);
	Fh(tl, 0, "\n/* --- END VMOD %.*s --- */\n\n", PF(mod));
	free(fnpx);
}

void v_matchproto_(sym_act_f)
vcc_Act_New(struct vcc *tl, struct token *t, struct symbol *sym)
{
	struct symbol *isym, *osym;
	struct inifin *ifp;
	struct vsb *buf;
	const struct vjsn_val *vv, *vf;
	int null_ok = 0;

	(void)sym;
	(void)t;

	ExpectErr(tl, ID);
	vcc_ExpectVid(tl, "VCL object");
	ERRCHK(tl);
	isym = VCC_HandleSymbol(tl, INSTANCE, "vo");
	ERRCHK(tl);
	AN(isym);
	isym->noref = 1;
	isym->action = vcc_Act_Obj;

	SkipToken(tl, '=');
	ExpectErr(tl, ID);
	osym = VCC_SymbolGet(tl, SYM_MAIN, SYM_OBJECT, SYMTAB_EXISTING,
	    XREF_NONE);
	ERRCHK(tl);
	AN(osym);

	/* Scratch the generic INSTANCE type */
	isym->type = osym->type;

	CAST_OBJ_NOTNULL(vv, osym->eval_priv, VJSN_VAL_MAGIC);
	// vv = object name

	isym->vmod_name = osym->vmod_name;
	isym->eval_priv = vv;

	vv = VTAILQ_NEXT(vv, list);
	// vv = flags
	assert(vv->type == VJSN_OBJECT);
	VTAILQ_FOREACH(vf, &vv->children, list)
		if (!strcmp(vf->name, "NULL_OK") && vf->type == VJSN_TRUE)
			null_ok = 1;
	if (!null_ok)
		VTAILQ_INSERT_TAIL(&tl->sym_objects, isym, sideways);

	vv = VTAILQ_NEXT(vv, list);
	// vv = struct name

	Fh(tl, 0, "static %s *%s;\n\n", vv->value, isym->rname);
	vv = VTAILQ_NEXT(vv, list);

	vf = VTAILQ_FIRST(&vv->children);
	vv = VTAILQ_NEXT(vv, list);
	assert(vf->type == VJSN_STRING);
	assert(!strcmp(vf->value, "$INIT"));

	vf = VTAILQ_NEXT(vf, list);

	buf = VSB_new_auto();
	AN(buf);
	VSB_printf(buf, "&%s, \"%s\"", isym->rname, isym->name);
	AZ(VSB_finish(buf));
	vcc_Eval_Func(tl, vf, VSB_data(buf), osym);
	VSB_destroy(&buf);
	ERRCHK(tl);
	SkipToken(tl, ';');
	isym->def_e = tl->t;

	vf = VTAILQ_FIRST(&vv->children);
	assert(vf->type == VJSN_STRING);
	assert(!strcmp(vf->value, "$FINI"));

	vf = VTAILQ_NEXT(vf, list);
	vf = VTAILQ_FIRST(&vf->children);
	vf = VTAILQ_NEXT(vf, list);
	ifp = New_IniFin(tl);
	VSB_printf(ifp->fin, "\t\tif (%s)\n", isym->rname);
	VSB_printf(ifp->fin, "\t\t\t\t%s(&%s);", vf->value, isym->rname);
}
