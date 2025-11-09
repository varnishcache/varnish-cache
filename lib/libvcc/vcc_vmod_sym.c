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
 *
 * Turn vmod JSON spec into symbols
 *
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "vcc_compile.h"

#include "libvcc.h"
#include "vjsn.h"

#include "vcc_vmod.h"

struct vmod_obj {
	unsigned		magic;
#define VMOD_OBJ_MAGIC		0x349885f8
	char			*name;
	struct type		type[1];
	VTAILQ_ENTRY(vmod_obj)	list;
};

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
alias_sym(struct vcc *tl, const struct symbol *psym, const struct vjsn_val *v)
{
	char *alias = NULL, *func = NULL;
	struct symbol *sym;
	struct vsb *buf;

	buf = VSB_new_auto();
	AN(buf);

	VCC_SymName(buf, psym);
	VSB_printf(buf, ".%s", v->value);
	AZ(VSB_finish(buf));
	REPLACE(alias, VSB_data(buf));

	v = VTAILQ_NEXT(v, list);
	assert(vjsn_is_string(v));

	VSB_clear(buf);
	VCC_SymName(buf, psym);
	VSB_printf(buf, ".%s", v->value);
	AZ(VSB_finish(buf));
	REPLACE(func, VSB_data(buf));

	sym = VCC_MkSymAlias(tl, alias, func);
	AN(sym);
	assert(sym->kind == SYM_FUNC || sym->kind == SYM_METHOD);
	VSB_destroy(&buf);
	free(alias);
	free(func);
}

static void
func_restrict(struct vcc *tl, struct symbol *sym, vcc_kind_t kind, const struct vjsn_val *v)
{
	struct vjsn_val *vv;

	AN(v);
	AN(sym);

	if (kind != SYM_FUNC && kind != SYM_METHOD)
		return;

	v = VTAILQ_NEXT(v, list);
	if (!v || !vjsn_is_array(v))
		return;
	vv = VTAILQ_FIRST(&v->children);
	AN(vv);
	assert(vjsn_is_string(vv));
	if (strcmp(vv->value, "$RESTRICT"))
		return;
	vv = VTAILQ_NEXT(vv, list);
	AN(vv);
	assert(vjsn_is_array(vv));
	sym->r_methods = 0;
	vv = VTAILQ_FIRST(&vv->children);
	unsigned s;
	while (vv) {
		s = 0;
#define VCL_CTX(l,H)							\
		if (strcmp(vv->value, #l) == 0) s = VCL_MET_##H;
#include "tbl/vcl_context.h"
		if (!s) {
			VSB_printf(tl->sb, "Error in vmod \"%s\", invalid scope for $Restrict: %s\n",sym->vmod_name, vv->value);
			tl->err = 1;
			break;
		}
		sym->r_methods |= s;
		vv = VTAILQ_NEXT(vv,list);
	}
}

static void
func_sym(struct vcc *tl, vcc_kind_t kind, struct symbol *psym,
    const struct vjsn_val *v, const struct vjsn_val *vr)
{
	const struct vjsn_val *vv;
	struct symbol *sym;
	struct vsb *buf;

	if (kind == SYM_ALIAS) {
		alias_sym(tl, psym, v);
		return;
	}

	buf = VSB_new_auto();
	AN(buf);

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

	assert(vjsn_is_array(v));
	sym->action = vcc_Act_Call;
	sym->vmod_name = psym->vmod_name;
	sym->eval = vcc_Eval_SymFunc;
	sym->eval_priv = v;
	v = VTAILQ_FIRST(&v->children);
	assert(vjsn_is_array(v));
	vv = v;
	v = VTAILQ_FIRST(&v->children);
	assert(vjsn_is_string(v));
	sym->type = VCC_Type(v->value);
	AN(sym->type);
	sym->r_methods = VCL_MET_TASK_ALL;
	func_restrict(tl, sym, kind, vr);
	if (kind == SYM_CLI_METHOD) {
		vv = VTAILQ_NEXT(vv, list);
		assert(vjsn_is_string(vv));
		AZ(psym->extra);
		psym->extra = vv->value;
	}
}

void
vcc_VmodSymbols(struct vcc *tl, struct symbol *sym)
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
		if (!vjsn_is_array(vv))
			continue;
		vv1 = VTAILQ_FIRST(&vv->children);
		AN(vv1);
		assert(vjsn_is_string(vv1));
		vv2 = VTAILQ_NEXT(vv1, list);
		AN(vv2);
		if (!vjsn_is_string(vv2))
			continue;

		kind = SYM_NONE;
#define STANZA(UU, ll, ss) if (!strcmp(vv1->value, "$" #UU)) kind = ss;
	STANZA_TBL
#undef STANZA
		if (kind != SYM_NONE) {
			func_sym(tl, kind, sym, vv2, vv);
			ERRCHK(tl);
		}
	}
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
	isym = VCC_HandleSymbol(tl, INSTANCE);
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
	isym->extra = osym->extra;

	CAST_OBJ_NOTNULL(vv, osym->eval_priv, VJSN_VAL_MAGIC);
	// vv = object name

	isym->vmod_name = osym->vmod_name;
	isym->eval_priv = vv;

	vv = VTAILQ_NEXT(vv, list);
	// vv = flags
	assert(vjsn_is_object(vv));
	VTAILQ_FOREACH(vf, &vv->children, list)
		if (!strcmp(vf->name, "NULL_OK") && vjsn_is_true(vf))
			null_ok = 1;
	if (!null_ok)
		VTAILQ_INSERT_TAIL(&tl->sym_objects, isym, sideways);

	vv = VTAILQ_NEXT(vv, list);
	// vv = struct name

	Fh(tl, 0, "static %s *%s;\n\n", vv->value, isym->rname);
	vv = VTAILQ_NEXT(vv, list);

	vf = VTAILQ_FIRST(&vv->children);
	vv = VTAILQ_NEXT(vv, list);
	assert(vjsn_is_string(vf));
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
	assert(vjsn_is_string(vf));
	assert(!strcmp(vf->value, "$FINI"));

	vf = VTAILQ_NEXT(vf, list);
	vf = VTAILQ_FIRST(&vf->children);
	vf = VTAILQ_NEXT(vf, list);
	ifp = New_IniFin(tl);
	VSB_printf(ifp->fin, "\t\tif (%s)\n", isym->rname);
	VSB_printf(ifp->fin, "\t\t\t\t%s(&%s);", vf->value, isym->rname);
}
