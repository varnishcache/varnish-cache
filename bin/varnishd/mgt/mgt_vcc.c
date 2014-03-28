/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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

/*--------------------------------------------------------------------
 * Prepare the compiler command line
 */
static struct vsb *
mgt_make_cc_cmd(const char *sf, const char *of)
{
	struct vsb *sb;
	int pct;
	char *p;

	sb = VSB_new_auto();
	XXXAN(sb);
	for (p = mgt_cc_cmd, pct = 0; *p; ++p) {
		if (pct) {
			switch (*p) {
			case 's':
				VSB_cat(sb, sf);
				break;
			case 'o':
				VSB_cat(sb, of);
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
	return (sb);
}

/*--------------------------------------------------------------------
 * Invoke system VCC compiler in a sub-process
 */

struct vcc_priv {
	unsigned	magic;
#define VCC_PRIV_MAGIC	0x70080cb8
	char		*sf;
	const char	*vcl;
};

static void
run_vcc(void *priv)
{
	char *csrc;
	struct vsb *sb;
	struct vcc_priv *vp;
	int fd, i, l;

	CAST_OBJ_NOTNULL(vp, priv, VCC_PRIV_MAGIC);
	mgt_sandbox(SANDBOX_VCC);
	sb = VSB_new_auto();
	XXXAN(sb);
	VCC_VCL_dir(vcc, mgt_vcl_dir);
	VCC_VMOD_dir(vcc, mgt_vmod_dir);
	VCC_Err_Unref(vcc, mgt_vcc_err_unref);
	VCC_Allow_InlineC(vcc, mgt_vcc_allow_inline_c);
	VCC_Unsafe_Path(vcc, mgt_vcc_unsafe_path);
	csrc = VCC_Compile(vcc, sb, vp->vcl);
	AZ(VSB_finish(sb));
	if (VSB_len(sb))
		printf("%s", VSB_data(sb));
	VSB_delete(sb);
	if (csrc == NULL)
		exit (1);

	fd = open(vp->sf, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "Cannot open %s", vp->sf);
		exit (1);
	}
	l = strlen(csrc);
	i = write(fd, csrc, l);
	if (i != l) {
		fprintf(stderr, "Cannot write %s", vp->sf);
		exit (1);
	}
	AZ(close(fd));
	free(csrc);
	exit (0);
}

/*--------------------------------------------------------------------
 * Invoke system C compiler in a sub-process
 */

static void
run_cc(void *priv)
{
	mgt_sandbox(SANDBOX_CC);
	(void)execl("/bin/sh", "/bin/sh", "-c", priv, (char*)0);
}

/*--------------------------------------------------------------------
 * Attempt to open compiled VCL in a sub-process
 */

static void __match_proto__(sub_func_f)
run_dlopen(void *priv)
{
	const char *of;
	void *dlh;
	struct VCL_conf const *cnf;

	of = priv;

	mgt_sandbox(SANDBOX_VCLLOAD);

	/* Try to load the object into this sub-process */
	if ((dlh = dlopen(of, RTLD_NOW | RTLD_LOCAL)) == NULL) {
		fprintf(stderr,
		    "Compiled VCL program failed to load:\n  %s\n",
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
 * Compile a VCL program, return shared object, errors in sb.
 */

static char *
mgt_run_cc(const char *vcl, struct vsb *sb, int C_flag)
{
	char *csrc;
	struct vsb *cmdsb;
	char sf[] = "./vcl.########.c";
	char of[sizeof sf + 1];
	char *retval;
	int sfd, i;
	struct vcc_priv vp;

	/* Create temporary C source file */
	sfd = VFIL_tmpfile(sf);
	if (sfd < 0) {
		VSB_printf(sb, "Failed to create %s: %s", sf, strerror(errno));
		return (NULL);
	}
	(void)fchown(sfd, mgt_param.uid, mgt_param.gid);
	AZ(close(sfd));


	/* Run the VCC compiler in a sub-process */
	memset(&vp, 0, sizeof vp);
	vp.magic = VCC_PRIV_MAGIC;
	vp.sf = sf;
	vp.vcl = vcl;
	if (VSUB_run(sb, run_vcc, &vp, "VCC-compiler", -1)) {
		(void)unlink(sf);
		return (NULL);
	}

	if (C_flag) {
		csrc = VFIL_readfile(NULL, sf, NULL);
		XXXAN(csrc);
		(void)fputs(csrc, stdout);
		free(csrc);
	}

	/* Name the output shared library by "s/[.]c$/[.]so/" */
	memcpy(of, sf, sizeof sf);
	assert(sf[sizeof sf - 2] == 'c');
	of[sizeof sf - 2] = 's';
	of[sizeof sf - 1] = 'o';
	of[sizeof sf] = '\0';

	i = open(of, O_WRONLY|O_CREAT|O_TRUNC, 0600);
	if (i < 0) {
		VSB_printf(sb, "Failed to create %s: %s",
		    of, strerror(errno));
		(void)unlink(sf);
		return (NULL);
	}
	(void)fchown(i, mgt_param.uid, mgt_param.gid);
	AZ(close(i));

	/* Build the C-compiler command line */
	cmdsb = mgt_make_cc_cmd(sf, of);

	/* Run the C-compiler in a sub-shell */
	i = VSUB_run(sb, run_cc, VSB_data(cmdsb), "C-compiler", 10);

	(void)unlink(sf);
	VSB_delete(cmdsb);

	if (!i)
		i = VSUB_run(sb, run_dlopen, of, "dlopen", 10);

	/* Ensure the file is readable to the unprivileged user */
	if (!i) {
		i = chmod(of, 0755);
		if (i)
			VSB_printf(sb, "Failed to set permissions on %s: %s",
			    of, strerror(errno));
	}

	if (i) {
		(void)unlink(of);
		return (NULL);
	}

	retval = strdup(of);
	XXXAN(retval);
	return (retval);
}

/*--------------------------------------------------------------------*/

static char *
mgt_VccCompile(struct vsb **sb, const char *b, int C_flag)
{
	char *vf;

	*sb = VSB_new_auto();
	XXXAN(*sb);
	vf = mgt_run_cc(b, *sb, C_flag);
	AZ(VSB_finish(*sb));
	return (vf);
}

/*--------------------------------------------------------------------*/

static struct vclprog *
mgt_vcc_add(const char *name, char *file)
{
	struct vclprog *vp;

	vp = calloc(sizeof *vp, 1);
	XXXAN(vp);
	vp->name = strdup(name);
	XXXAN(vp->name);
	vp->fname = file;
	VTAILQ_INSERT_TAIL(&vclhead, vp, list);
	return (vp);
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

/*--------------------------------------------------------------------*/

int
mgt_vcc_default(const char *b_arg, const char *f_arg, char *vcl, int C_flag)
{
	char *vf;
	struct vsb *sb;
	struct vclprog *vp;
	char buf[BUFSIZ];

	/* XXX: annotate vcl with -b/-f arg so people know where it came from */
	(void)f_arg;

	if (b_arg != NULL) {
		AZ(vcl);
		/*
		 * XXX: should do a "HEAD /" on the -b argument to see that
		 * XXX: it even works.  On the other hand, we should do that
		 * XXX: for all backends in the cache process whenever we
		 * XXX: change config, but for a complex VCL, it might not be
		 * XXX: a bug for a backend to not reply at that time, so then
		 * XXX: again: we should check it here in the "trivial" case.
		 */
		bprintf(buf,
		    "vcl 4.0;\n"
		    "backend default {\n"
		    "    .host = \"%s\";\n"
		    "}\n", b_arg);
		vcl = strdup(buf);
		AN(vcl);
	}
	strcpy(buf, "boot");

	vf = mgt_VccCompile(&sb, vcl, C_flag);
	free(vcl);
	if (VSB_len(sb) > 0)
		fprintf(stderr, "%s", VSB_data(sb));
	VSB_delete(sb);
	if (C_flag && vf != NULL)
		AZ(unlink(vf));
	if (vf == NULL) {
		fprintf(stderr, "\nVCL compilation failed\n");
		return (1);
	} else {
		vp = mgt_vcc_add(buf, vf);
		vp->active = 1;
		return (0);
	}
}

/*--------------------------------------------------------------------*/

int
mgt_has_vcl(void)
{

	return (!VTAILQ_EMPTY(&vclhead));
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

static
void
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
mcf_config_inline(struct cli *cli, const char * const *av, void *priv)
{
	char *vf, *p = NULL;
	struct vsb *sb;
	unsigned status;
	struct vclprog *vp;

	(void)priv;

	vp = mgt_vcc_byname(av[2]);
	if (vp != NULL) {
		VCLI_Out(cli, "Already a VCL program named %s", av[2]);
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	}

	vf = mgt_VccCompile(&sb, av[3], 0);
	if (VSB_len(sb) > 0)
		VCLI_Out(cli, "%s\n", VSB_data(sb));
	VSB_delete(sb);
	if (vf == NULL) {
		VCLI_Out(cli, "VCL compilation failed");
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	}
	VCLI_Out(cli, "VCL compiled.");
	if (child_pid >= 0 &&
	    mgt_cli_askchild(&status, &p, "vcl.load %s %s\n", av[2], vf)) {
		VCLI_SetResult(cli, status);
		VCLI_Out(cli, "%s", p);
	} else {
		(void)mgt_vcc_add(av[2], vf);
	}
	free(p);
}

void
mcf_config_load(struct cli *cli, const char * const *av, void *priv)
{
	char *vf, *vcl;
	struct vsb *sb;
	unsigned status;
	char *p = NULL;
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

	vf = mgt_VccCompile(&sb, vcl, 0);
	free(vcl);

	if (VSB_len(sb) > 0)
		VCLI_Out(cli, "%s", VSB_data(sb));
	VSB_delete(sb);
	if (vf == NULL) {
		VCLI_Out(cli, "VCL compilation failed");
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	}
	VCLI_Out(cli, "VCL compiled.");
	if (child_pid >= 0 &&
	    mgt_cli_askchild(&status, &p, "vcl.load %s %s\n", av[2], vf)) {
		VCLI_SetResult(cli, status);
		VCLI_Out(cli, "%s", p);
	} else {
		(void)mgt_vcc_add(av[2], vf);
	}
	free(p);
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
mcf_config_use(struct cli *cli, const char * const *av, void *priv)
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
mcf_config_discard(struct cli *cli, const char * const *av, void *priv)
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
mcf_config_list(struct cli *cli, const char * const *av, void *priv)
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

/*
 * XXX: This should take an option argument to show all (include) files
 * XXX: This violates the principle of not loading VCL's in the master
 * XXX: process.
 */
void
mcf_config_show(struct cli *cli, const char * const *av, void *priv)
{
	struct vclprog *vp;
	void *dlh, *sym;
	const char **src;

	(void)priv;
	if ((vp = mcf_find_vcl(cli, av[2])) != NULL) {
		if ((dlh = dlopen(vp->fname, RTLD_NOW | RTLD_LOCAL)) == NULL) {
			VCLI_Out(cli, "failed to load %s: %s\n",
			    vp->name, dlerror());
			VCLI_SetResult(cli, CLIS_CANT);
		} else if ((sym = dlsym(dlh, "srcbody")) == NULL) {
			VCLI_Out(cli, "failed to locate source for %s: %s\n",
			    vp->name, dlerror());
			VCLI_SetResult(cli, CLIS_CANT);
			AZ(dlclose(dlh));
		} else {
			src = sym;
			VCLI_Out(cli, "%s", src[0]);
			/* VCLI_Out(cli, src[1]); */
			AZ(dlclose(dlh));
		}
	}
}
