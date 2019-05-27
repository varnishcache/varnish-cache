/*-
 * Copyright (c) 2019 Varnish Software AS
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
 * VCL/VMOD symbol table
 */

#include "config.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "mgt/mgt.h"
#include "mgt/mgt_vcl.h"

#include "vcli_serve.h"
#include "libvcc.h"
#include "vjsn.h"

/*--------------------------------------------------------------------*/

static const char *
mgt_vcl_symtab_val(const struct vjsn_val *vv, const char *val)
{
	const struct vjsn_val *jv;

	jv = vjsn_child(vv, val);
	AN(jv);
	assert(jv->type == VJSN_STRING);
	AN(jv->value);
	return (jv->value);
}

static void
mgt_vcl_import_vcl(struct vclprog *vp1, struct import *ip, const struct vjsn_val *vv)
{
	struct vclprog *vp2;

	CHECK_OBJ_NOTNULL(vp1, VCLPROG_MAGIC);
	AN(ip);
	AN(vv);

	vp2 = mcf_vcl_byname(mgt_vcl_symtab_val(vv, "name"));
	CHECK_OBJ_NOTNULL(vp2, VCLPROG_MAGIC);
	ip->vcl = vp2;
	VTAILQ_INSERT_TAIL(&vp2->exports, ip, to);
	mgt_vcl_dep_add(vp1, vp2);
}

static int
mgt_vcl_cache_vmod(const char *nm, const char *fm, const char *to)
{
	int fi, fo;
	int ret = 0;
	ssize_t sz;
	char buf[BUFSIZ];

	fo = open(to, O_WRONLY | O_CREAT | O_EXCL, 0744);
	if (fo < 0 && errno == EEXIST)
		return (0);
	if (fo < 0) {
		fprintf(stderr, "While creating copy of vmod %s:\n\t%s: %s\n",
		    nm, to, vstrerror(errno));
		return (1);
	}
	fi = open(fm, O_RDONLY);
	if (fi < 0) {
		fprintf(stderr, "Opening vmod %s from %s: %s\n",
		    nm, fm, vstrerror(errno));
		AZ(unlink(to));
		closefd(&fo);
		return (1);
	}
	while (1) {
		sz = read(fi, buf, sizeof buf);
		if (sz == 0)
			break;
		if (sz < 0 || sz != write(fo, buf, sz)) {
			fprintf(stderr, "Copying vmod %s: %s\n",
			    nm, vstrerror(errno));
			AZ(unlink(to));
			ret = 1;
			break;
		}
	}
	closefd(&fi);
	AZ(fchmod(fo, 0444));
	closefd(&fo);
	return (ret);
}

static void
mgt_vcl_import_vmod(struct vclprog *vp, struct import *ip, const struct vjsn_val *vv)
{
	struct vmodfile *vf;
	struct vmoddep *vd;
	const char *v_name;
	const char *v_file;
	const char *v_dst;

	CHECK_OBJ_NOTNULL(vp, VCLPROG_MAGIC);
	AN(ip);
	AN(vv);

	v_name = mgt_vcl_symtab_val(vv, "name");
	v_file = mgt_vcl_symtab_val(vv, "file");
	v_dst = mgt_vcl_symtab_val(vv, "dst");

	VTAILQ_FOREACH(vf, &vmodhead, list)
		if (!strcmp(vf->fname, v_dst))
			break;
	if (vf == NULL) {
		ALLOC_OBJ(vf, VMODFILE_MAGIC);
		AN(vf);
		REPLACE(vf->fname, v_dst);
		AN(vf->fname);
		VTAILQ_INIT(&vf->vcls);
		VTAILQ_INIT(&vf->exports);
		AZ(mgt_vcl_cache_vmod(v_name, v_file, v_dst));
		VTAILQ_INSERT_TAIL(&vmodhead, vf, list);
	}
	ALLOC_OBJ(vd, VMODDEP_MAGIC);
	AN(vd);
	vd->to = vf;
	VTAILQ_INSERT_TAIL(&vp->vmods, vd, lfrom);
	VTAILQ_INSERT_TAIL(&vf->vcls, vd, lto);
	ip->vmod = vf;
	VTAILQ_INSERT_TAIL(&vf->exports, ip, to);
}

void
mgt_vcl_symtab(struct vclprog *vp, const char *input)
{
	struct vjsn *vj;
	struct vjsn_val *v1, *v2;
	const char *typ, *err;
	struct import *ip;

	CHECK_OBJ_NOTNULL(vp, VCLPROG_MAGIC);
	AN(input);
	vj = vjsn_parse(input, &err);
	if (err != NULL) {
		fprintf(stderr, "FATAL: Symtab parse error: %s\n%s\n",
		    err, input);
	}
	AZ(err);
	AN(vj);
	vp->symtab = vj;
	assert(vj->value->type == VJSN_ARRAY);
	VTAILQ_FOREACH(v1, &vj->value->children, list) {
		assert(v1->type == VJSN_OBJECT);
		v2 = vjsn_child(v1, "dir");
		if (v2 == NULL)
			continue;
		assert(v2->type == VJSN_STRING);
		if (strcmp(v2->value, "import"))
			continue;
		ALLOC_OBJ(ip, IMPORT_MAGIC);
		AN(ip);
		ip->vj = v1;
		ip->target = vp;
		VTAILQ_INSERT_TAIL(&vp->imports, ip, from);
		typ = mgt_vcl_symtab_val(v1, "type");
		if (!strcmp(typ, "$VMOD"))
			mgt_vcl_import_vmod(vp, ip, v1);
		else if (!strcmp(typ, "$VCL"))
			mgt_vcl_import_vcl(vp, ip, v1);
		else
			WRONG("Bad symtab import entry");
	}
}

void
mgt_vcl_symtab_clean(struct vclprog *vp)
{
	struct import *ip;

	if (vp->symtab)
		vjsn_delete(&vp->symtab);
	while (!VTAILQ_EMPTY(&vp->imports)) {
		ip = VTAILQ_FIRST(&vp->imports);
		VTAILQ_REMOVE(&vp->imports, ip, from);
		if (ip->vmod)
			VTAILQ_REMOVE(&ip->vmod->exports, ip, to);
		if (ip->vcl)
			VTAILQ_REMOVE(&ip->vcl->exports, ip, to);
		FREE_OBJ(ip);
	}
}

/*--------------------------------------------------------------------*/

void
mgt_vcl_export_labels(struct vcc *vcc)
{
	struct vclprog *vp;
	VTAILQ_FOREACH(vp, &vclhead, list) {
		if (mcf_is_label(vp))
			VCC_Predef(vcc, "VCL_VCL", vp->name);
	}
}

/*--------------------------------------------------------------------*/

static void
mcf_vcl_vjsn_dump(struct cli *cli, const struct vjsn_val *vj, int indent)
{
	struct vjsn_val *vj1;
	AN(cli);
	AN(vj);

	VCLI_Out(cli, "%*s", indent, "");
	if (vj->name != NULL)
		VCLI_Out(cli, "[\"%s\"]: ", vj->name);
	VCLI_Out(cli, "{%s}", vj->type);
	if (vj->value != NULL)
		VCLI_Out(cli, " <%s>", vj->value);
	VCLI_Out(cli, "\n");
	VTAILQ_FOREACH(vj1, &vj->children, list)
		mcf_vcl_vjsn_dump(cli, vj1, indent + 2);
}

void v_matchproto_(cli_func_t)
mcf_vcl_symtab(struct cli *cli, const char * const *av, void *priv)
{
	struct vclprog *vp;
	struct import *ip;
	(void)av;
	(void)priv;
	VTAILQ_FOREACH(vp, &vclhead, list) {
		VCLI_Out(cli, "VCL: %s\n", vp->name);
		VCLI_Out(cli, "  imports:\n");
		VTAILQ_FOREACH(ip, &vp->imports, from) {
			VCLI_Out(cli, "    %p -> (%p, %p)\n",
				ip, ip->vcl, ip->vmod);
			mcf_vcl_vjsn_dump(cli, ip->vj, 6);
		}
		VCLI_Out(cli, "  exports:\n");
		VTAILQ_FOREACH(ip, &vp->exports, to) {
			VCLI_Out(cli, "    %p -> (%p, %p) %s\n",
				ip, ip->vcl, ip->vmod, ip->target->name);
			mcf_vcl_vjsn_dump(cli, ip->vj, 6);
		}
	}
}
