/*-
 * Copyright (c) 2019 Varnish Software AS
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
mgt_vcl_import_vcl(struct vclprog *vp1, const struct vjsn_val *vv)
{
	struct vclprog *vp2;

	CHECK_OBJ_NOTNULL(vp1, VCLPROG_MAGIC);
	AN(vv);

	vp2 = mcf_vcl_byname(mgt_vcl_symtab_val(vv, "name"));
	CHECK_OBJ_NOTNULL(vp2, VCLPROG_MAGIC);
	mgt_vcl_dep_add(vp1, vp2)->vj = vv;
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
		    nm, to, VAS_errtxt(errno));
		return (1);
	}
	fi = open(fm, O_RDONLY);
	if (fi < 0) {
		fprintf(stderr, "Opening vmod %s from %s: %s\n",
		    nm, fm, VAS_errtxt(errno));
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
			    nm, VAS_errtxt(errno));
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
mgt_vcl_import_vmod(struct vclprog *vp, const struct vjsn_val *vv)
{
	struct vmodfile *vf;
	struct vmoddep *vd;
	const char *v_name;
	const char *v_file;
	const char *v_dst;

	CHECK_OBJ_NOTNULL(vp, VCLPROG_MAGIC);
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
		VTAILQ_INIT(&vf->vcls);
		AZ(mgt_vcl_cache_vmod(v_name, v_file, v_dst));
		VTAILQ_INSERT_TAIL(&vmodhead, vf, list);
	}
	ALLOC_OBJ(vd, VMODDEP_MAGIC);
	AN(vd);
	vd->to = vf;
	VTAILQ_INSERT_TAIL(&vp->vmods, vd, lfrom);
	VTAILQ_INSERT_TAIL(&vf->vcls, vd, lto);
}

void
mgt_vcl_symtab(struct vclprog *vp, const char *input)
{
	struct vjsn *vj;
	struct vjsn_val *v1, *v2;
	const char *typ, *err;

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
		typ = mgt_vcl_symtab_val(v1, "type");
		if (!strcmp(typ, "$VMOD"))
			mgt_vcl_import_vmod(vp, v1);
		else if (!strcmp(typ, "$VCL"))
			mgt_vcl_import_vcl(vp, v1);
		else
			WRONG("Bad symtab import entry");
	}
}

void
mgt_vcl_symtab_clean(struct vclprog *vp)
{

	if (vp->symtab)
		vjsn_delete(&vp->symtab);
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
	struct vcldep *vd;

	(void)av;
	(void)priv;
	VTAILQ_FOREACH(vp, &vclhead, list) {
		if (mcf_is_label(vp))
			VCLI_Out(cli, "Label: %s\n", vp->name);
		else
			VCLI_Out(cli, "Vcl: %s\n", vp->name);
		if (!VTAILQ_EMPTY(&vp->dfrom)) {
			VCLI_Out(cli, "  imports from:\n");
			VTAILQ_FOREACH(vd, &vp->dfrom, lfrom) {
				VCLI_Out(cli, "    %s\n", vd->to->name);
				if (vd->vj)
					mcf_vcl_vjsn_dump(cli, vd->vj, 6);
			}
		}
		if (!VTAILQ_EMPTY(&vp->dto)) {
			VCLI_Out(cli, "  exports to:\n");
			VTAILQ_FOREACH(vd, &vp->dto, lto) {
				VCLI_Out(cli, "    %s\n", vd->from->name);
				if (vd->vj)
					mcf_vcl_vjsn_dump(cli, vd->vj, 6);
			}
		}
		if (vp->symtab != NULL) {
			VCLI_Out(cli, "  symtab:\n");
			mcf_vcl_vjsn_dump(cli, vp->symtab->value, 4);
		}
	}
}
