/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 * VCL compiler stuff
 */

#include "config.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "common/params.h"
#include "mgt/mgt.h"

#include "libvcc.h"
#include "vcl.h"
#include "vcli.h"
#include "vcli_priv.h"
#include "vfil.h"
#include "vsub.h"

#include "mgt_cli.h"

struct vclprog {
	VTAILQ_ENTRY(vclprog)	list;
	char			*name;
	char			*fname;
	int			active;
};

struct vcc_priv {
	unsigned	magic;
#define VCC_PRIV_MAGIC	0x70080cb8
	const char	*src;
	char		*srcfile;
	char		*libfile;
};

static VTAILQ_HEAD(, vclprog) vclhead = VTAILQ_HEAD_INITIALIZER(vclhead);

char *mgt_cc_cmd;
const char *mgt_vcl_dir;
const char *mgt_vmod_dir;
unsigned mgt_vcc_err_unref;
unsigned mgt_vcc_allow_inline_c;
unsigned mgt_vcc_unsafe_path;

static struct vcc *vcc;

/*--------------------------------------------------------------------*/

static const char * const builtin_vcl =
#include "builtin_vcl.h"
    ""	;

/*--------------------------------------------------------------------*/

static void
mgt_vcc_add(const char *name, const char *libfile)
{
	struct vclprog *vp;

	vp = calloc(sizeof *vp, 1);
	XXXAN(vp);
	REPLACE(vp->name, name);
	REPLACE(vp->fname, libfile);
	if (VTAILQ_EMPTY(&vclhead))
		vp->active = 1;
	VTAILQ_INSERT_TAIL(&vclhead, vp, list);
}

static void
mgt_vcc_del(struct vclprog *vp)
{
	VTAILQ_REMOVE(&vclhead, vp, list);
	printf("unlink %s\n", vp->fname);
	XXXAZ(unlink(vp->fname));
	free(vp->fname);
	free(vp->name);
	free(vp);
}

static struct vclprog *
mgt_vcc_byname(const char *name)
{
	struct vclprog *vp;

	VTAILQ_FOREACH(vp, &vclhead, list)
		if (!strcmp(name, vp->name))
			return (vp);
	return (NULL);
}


static int
mgt_vcc_delbyname(const char *name)
{
	struct vclprog *vp;

	vp = mgt_vcc_byname(name);
	if (vp != NULL) {
		mgt_vcc_del(vp);
		return (0);
	}
	return (1);
}

int
mgt_has_vcl(void)
{

	return (!VTAILQ_EMPTY(&vclhead));
}

/*--------------------------------------------------------------------
 * Invoke system VCC compiler in a sub-process
 */

static void
run_vcc(void *priv)
{
	char *csrc;
	struct vsb *sb;
	struct vcc_priv *vp;
	int fd, i, l;

	CAST_OBJ_NOTNULL(vp, priv, VCC_PRIV_MAGIC);
	VJ_subproc(JAIL_SUBPROC_VCC);
	sb = VSB_new_auto();
	XXXAN(sb);
	VCC_VCL_dir(vcc, mgt_vcl_dir);
	VCC_VMOD_dir(vcc, mgt_vmod_dir);
	VCC_Err_Unref(vcc, mgt_vcc_err_unref);
	VCC_Allow_InlineC(vcc, mgt_vcc_allow_inline_c);
	VCC_Unsafe_Path(vcc, mgt_vcc_unsafe_path);
	csrc = VCC_Compile(vcc, sb, vp->src);
	AZ(VSB_finish(sb));
	if (VSB_len(sb))
		printf("%s", VSB_data(sb));
	VSB_delete(sb);
	if (csrc == NULL)
		exit(2);

	fd = open(vp->srcfile, O_WRONLY|O_TRUNC|O_CREAT, 0600);
	if (fd < 0) {
		fprintf(stderr, "Cannot open %s", vp->srcfile);
		exit(2);
	}
	l = strlen(csrc);
	i = write(fd, csrc, l);
	if (i != l) {
		fprintf(stderr, "Cannot write %s", vp->srcfile);
		exit(2);
	}
	AZ(close(fd));
	free(csrc);
	exit(0);
}

/*--------------------------------------------------------------------
 * Invoke system C compiler in a sub-process
 */

static void
run_cc(void *priv)
{
	struct vcc_priv *vp;
	struct vsb *sb;
	int pct;
	char *p;

	VJ_subproc(JAIL_SUBPROC_CC);
	CAST_OBJ_NOTNULL(vp, priv, VCC_PRIV_MAGIC);

	sb = VSB_new_auto();
	AN(sb);
	for (p = mgt_cc_cmd, pct = 0; *p; ++p) {
		if (pct) {
			switch (*p) {
			case 's':
				VSB_cat(sb, vp->srcfile);
				break;
			case 'o':
				VSB_cat(sb, vp->libfile);
				break;
			case '%':
				VSB_putc(sb, '%');
				break;
			default:
				VSB_putc(sb, '%');
				VSB_putc(sb, *p);
				break;
			}
			pct = 0;
		} else if (*p == '%') {
			pct = 1;
		} else {
			VSB_putc(sb, *p);
		}
	}
	if (pct)
		VSB_putc(sb, '%');
	AZ(VSB_finish(sb));

	(void)umask(0177);
	(void)execl("/bin/sh", "/bin/sh", "-c", VSB_data(sb), (char*)0);
	VSB_delete(sb);				// For flexelint
}

/*--------------------------------------------------------------------
 * Attempt to open compiled VCL in a sub-process
 */

static void __match_proto__(sub_func_f)
run_dlopen(void *priv)
{
	void *dlh;
	struct VCL_conf const *cnf;
	struct vcc_priv *vp;

	VJ_subproc(JAIL_SUBPROC_VCLLOAD);
	CAST_OBJ_NOTNULL(vp, priv, VCC_PRIV_MAGIC);

	/* Try to load the object into this sub-process */
	if ((dlh = dlopen(vp->libfile, RTLD_NOW | RTLD_LOCAL)) == NULL) {
		fprintf(stderr, "Compiled VCL program failed to load:\n  %s\n",
		    dlerror());
		exit(1);
	}

	cnf = dlsym(dlh, "VCL_conf");
	if (cnf == NULL) {
		fprintf(stderr, "Compiled VCL program, metadata not found\n");
		exit(1);
	}

	if (cnf->magic != VCL_CONF_MAGIC) {
		fprintf(stderr, "Compiled VCL program, mangled metadata\n");
		exit(1);
	}

	if (dlclose(dlh)) {
		fprintf(stderr,
		    "Compiled VCL program failed to unload:\n  %s\n",
		    dlerror());
		exit(1);
	}
	exit(0);
}

/*--------------------------------------------------------------------
 * Touch a filename and make it available to privsep-privs
 */

static int
mgt_vcc_touchfile(const char *fn, struct vsb *sb)
{
	int i;

	i = open(fn, O_WRONLY|O_CREAT|O_TRUNC, 0600);
	if (i < 0) {
		VSB_printf(sb, "Failed to create %s: %s", fn, strerror(errno));
		return (2);
	}
	if (fchown(i, mgt_param.uid, mgt_param.gid) != 0)
		if (geteuid() == 0)
			VSB_printf(sb, "Failed to change owner on %s: %s\n",
			    fn, strerror(errno));
	AZ(close(i));
	return (0);
}

/*--------------------------------------------------------------------
 * Compile a VCL program, return shared object, errors in sb.
 */

static unsigned
mgt_vcc_compile(struct vcc_priv *vp, struct vsb *sb, int C_flag)
{
	char *csrc;
	unsigned subs;

	if (mgt_vcc_touchfile(vp->srcfile, sb))
		return (2);
	if (mgt_vcc_touchfile(vp->libfile, sb))
		return (2);

	subs = VSUB_run(sb, run_vcc, vp, "VCC-compiler", -1);
	if (subs)
		return (subs);

	if (C_flag) {
		csrc = VFIL_readfile(NULL, vp->srcfile, NULL);
		AN(csrc);
		VSB_cat(sb, csrc);
		free(csrc);
	}

	subs = VSUB_run(sb, run_cc, vp, "C-compiler", 10);
	if (subs)
		return (subs);

	subs = VSUB_run(sb, run_dlopen, vp, "dlopen", 10);
	return (subs);
}

/*--------------------------------------------------------------------*/

static void
mgt_VccCompile(struct cli *cli, const char *vclname, const char *vclsrc,
    int C_flag)
{
	struct vcc_priv vp;
	struct vsb *sb;
	unsigned status;
	char *p;

	AN(cli);
	sb = VSB_new_auto();
	XXXAN(sb);

	INIT_OBJ(&vp, VCC_PRIV_MAGIC);
	vp.src = vclsrc;

	VSB_printf(sb, "./vcl_%s.c", vclname);
	AZ(VSB_finish(sb));
	vp.srcfile = strdup(VSB_data(sb));
	AN(vp.srcfile);
	VSB_clear(sb);

	VSB_printf(sb, "./vcl_%s.so", vclname);
	AZ(VSB_finish(sb));
	vp.libfile = strdup(VSB_data(sb));
	AN(vp.srcfile);
	VSB_clear(sb);

	status = mgt_vcc_compile(&vp, sb, C_flag);

	AZ(VSB_finish(sb));
	if (VSB_len(sb) > 0)
		VCLI_Out(cli, "%s", VSB_data(sb));
	VSB_delete(sb);

	(void)unlink(vp.srcfile);
	free(vp.srcfile);

	if (status || C_flag) {
		(void)unlink(vp.libfile);
		free(vp.libfile);
		if (!C_flag) {
			VCLI_Out(cli, "VCL compilation failed");
			VCLI_SetResult(cli, CLIS_PARAM);
		}
		return;
	}

	VCLI_Out(cli, "VCL compiled.\n");

	if (child_pid < 0) {
		mgt_vcc_add(vclname, vp.libfile);
		free(vp.libfile);
		return;
	}

	if (!mgt_cli_askchild(&status, &p,
	    "vcl.load %s %s\n", vclname, vp.libfile)) {
		mgt_vcc_add(vclname, vp.libfile);
		free(vp.libfile);
		free(p);
		return;
	}

	VCLI_Out(cli, "%s", p);
	free(p);
	VCLI_SetResult(cli, CLIS_PARAM);
	(void)unlink(vp.libfile);
	free(vp.libfile);
}

/*--------------------------------------------------------------------*/

void
mgt_vcc_default(struct cli *cli, const char *b_arg, const char *vclsrc,
    int C_flag)
{
	char buf[BUFSIZ];

	if (b_arg == NULL) {
		AN(vclsrc);
		mgt_VccCompile(cli, "boot", vclsrc, C_flag);
		return;
	}

	AZ(vclsrc);
	bprintf(buf,
	    "vcl 4.0;\n"
	    "backend default {\n"
	    "    .host = \"%s\";\n"
	    "}\n", b_arg);
	mgt_VccCompile(cli, "boot", buf, C_flag);
}

/*--------------------------------------------------------------------*/

int
mgt_push_vcls_and_start(unsigned *status, char **p)
{
	struct vclprog *vp;

	VTAILQ_FOREACH(vp, &vclhead, list) {
		if (mgt_cli_askchild(status, p,
		    "vcl.load \"%s\" %s\n", vp->name, vp->fname))
			return (1);
		free(*p);
		if (!vp->active)
			continue;
		if (mgt_cli_askchild(status, p,
		    "vcl.use \"%s\"\n", vp->name))
			return (1);
		free(*p);
	}
	if (mgt_cli_askchild(status, p, "start\n"))
		return (1);
	free(*p);
	*p = NULL;
	return (0);
}

/*--------------------------------------------------------------------*/

static void
mgt_vcc_atexit(void)
{
	struct vclprog *vp;

	if (getpid() != mgt_pid)
		return;
	while (1) {
		vp = VTAILQ_FIRST(&vclhead);
		if (vp == NULL)
			break;
		(void)unlink(vp->fname);
		VTAILQ_REMOVE(&vclhead, vp, list);
	}
}

void
mgt_vcc_init(void)
{

	vcc = VCC_New();
	AN(vcc);
	VCC_Builtin_VCL(vcc, builtin_vcl);
	AZ(atexit(mgt_vcc_atexit));
}

/*--------------------------------------------------------------------*/

void
mcf_vcl_inline(struct cli *cli, const char * const *av, void *priv)
{
	struct vclprog *vp;

	(void)priv;

	vp = mgt_vcc_byname(av[2]);
	if (vp != NULL) {
		VCLI_Out(cli, "Already a VCL program named %s", av[2]);
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	}

	mgt_VccCompile(cli, av[2], av[3], 0);
}

void
mcf_vcl_load(struct cli *cli, const char * const *av, void *priv)
{
	char *vcl;
	struct vclprog *vp;

	(void)priv;
	vp = mgt_vcc_byname(av[2]);
	if (vp != NULL) {
		VCLI_Out(cli, "Already a VCL program named %s", av[2]);
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	}

	vcl = VFIL_readfile(mgt_vcl_dir, av[3], NULL);
	if (vcl == NULL) {
		VCLI_Out(cli, "Cannot open '%s'", av[3]);
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	}

	mgt_VccCompile(cli, av[2], vcl, 0);
	free(vcl);
}

static struct vclprog *
mcf_find_vcl(struct cli *cli, const char *name)
{
	struct vclprog *vp;

	vp = mgt_vcc_byname(name);
	if (vp != NULL)
		return (vp);
	VCLI_SetResult(cli, CLIS_PARAM);
	VCLI_Out(cli, "No configuration named %s known.", name);
	return (NULL);
}

void
mcf_vcl_use(struct cli *cli, const char * const *av, void *priv)
{
	unsigned status;
	char *p = NULL;
	struct vclprog *vp;

	(void)priv;
	vp = mcf_find_vcl(cli, av[2]);
	if (vp == NULL)
		return;
	if (vp->active != 0)
		return;
	if (child_pid >= 0 &&
	    mgt_cli_askchild(&status, &p, "vcl.use %s\n", av[2])) {
		VCLI_SetResult(cli, status);
		VCLI_Out(cli, "%s", p);
	} else {
		VCLI_Out(cli, "VCL '%s' now active", av[2]);
		vp->active = 2;
		VTAILQ_FOREACH(vp, &vclhead, list) {
			if (vp->active == 1)
				vp->active = 0;
			else if (vp->active == 2)
				vp->active = 1;
		}
	}
	free(p);
}

void
mcf_vcl_discard(struct cli *cli, const char * const *av, void *priv)
{
	unsigned status;
	char *p = NULL;
	struct vclprog *vp;

	(void)priv;
	vp = mcf_find_vcl(cli, av[2]);
	if (vp != NULL && vp->active) {
		VCLI_SetResult(cli, CLIS_PARAM);
		VCLI_Out(cli, "Cannot discard active VCL program\n");
	} else if (vp != NULL) {
		if (child_pid >= 0 &&
		    mgt_cli_askchild(&status, &p,
		    "vcl.discard %s\n", av[2])) {
			VCLI_SetResult(cli, status);
			VCLI_Out(cli, "%s", p);
		} else {
			AZ(mgt_vcc_delbyname(av[2]));
		}
	}
	free(p);
}

void
mcf_vcl_list(struct cli *cli, const char * const *av, void *priv)
{
	unsigned status;
	char *p;
	const char *flg;
	struct vclprog *vp;

	(void)av;
	(void)priv;
	if (child_pid >= 0) {
		if (!mgt_cli_askchild(&status, &p, "vcl.list\n")) {
			VCLI_SetResult(cli, status);
			VCLI_Out(cli, "%s", p);
		}
		free(p);
	} else {
		VTAILQ_FOREACH(vp, &vclhead, list) {
			if (vp->active) {
				flg = "active";
			} else
				flg = "available";
			VCLI_Out(cli, "%-10s %6s %s\n",
			    flg, "N/A", vp->name);
		}
	}
}
