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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "mgt/mgt.h"
#include "common/heritage.h"
#include "storage/storage.h"

#include "libvcc.h"
#include "vcli_serve.h"
#include "vfil.h"
#include "vsub.h"
#include "vav.h"
#include "vtim.h"

struct vcc_priv {
	unsigned	magic;
#define VCC_PRIV_MAGIC	0x70080cb8
	char		*dir;
	const char	*vclsrc;
	const char	*vclsrcfile;
	char		*csrcfile;
	char		*libfile;
};

char *mgt_cc_cmd;
const char *mgt_vcl_path;
const char *mgt_vmod_path;
unsigned mgt_vcc_err_unref;
unsigned mgt_vcc_allow_inline_c;
unsigned mgt_vcc_unsafe_path;


#define VGC_SRC		"vgc.c"
#define VGC_LIB		"vgc.so"

/*--------------------------------------------------------------------*/

void
mgt_DumpBuiltin(void)
{
	printf("%s\n", builtin_vcl);
}

/*--------------------------------------------------------------------
 * Invoke system VCC compiler in a sub-process
 */

static void v_matchproto_(vsub_func_f)
run_vcc(void *priv)
{
	struct vsb *csrc;
	struct vsb *sb = NULL;
	struct vcc_priv *vp;
	int fd, i, l;
	struct vcc *vcc;
	struct stevedore *stv;

	VJ_subproc(JAIL_SUBPROC_VCC);
	CAST_OBJ_NOTNULL(vp, priv, VCC_PRIV_MAGIC);

	AZ(chdir(vp->dir));

	vcc = VCC_New();
	AN(vcc);
	VCC_Builtin_VCL(vcc, builtin_vcl);
	VCC_VCL_path(vcc, mgt_vcl_path);
	VCC_VMOD_path(vcc, mgt_vmod_path);
	VCC_Err_Unref(vcc, mgt_vcc_err_unref);
	VCC_Allow_InlineC(vcc, mgt_vcc_allow_inline_c);
	VCC_Unsafe_Path(vcc, mgt_vcc_unsafe_path);
	STV_Foreach(stv)
		VCC_Predef(vcc, "VCL_STEVEDORE", stv->ident);
	mgt_vcl_export_labels(vcc);
	csrc = VCC_Compile(vcc, &sb, vp->vclsrc, vp->vclsrcfile);
	AZ(VSB_finish(sb));
	if (VSB_len(sb))
		printf("%s", VSB_data(sb));
	VSB_destroy(&sb);
	if (csrc == NULL)
		exit(2);

	fd = open(VGC_SRC, O_WRONLY|O_TRUNC|O_CREAT, 0600);
	if (fd < 0) {
		fprintf(stderr, "VCC cannot open %s", vp->csrcfile);
		exit(2);
	}
	l = VSB_len(csrc);
	i = write(fd, VSB_data(csrc), l);
	if (i != l) {
		fprintf(stderr, "VCC cannot write %s", vp->csrcfile);
		exit(2);
	}
	closefd(&fd);
	VSB_destroy(&csrc);
	exit(0);
}

/*--------------------------------------------------------------------
 * Invoke system C compiler in a sub-process
 */

static void v_matchproto_(vsub_func_f)
run_cc(void *priv)
{
	struct vcc_priv *vp;
	struct vsb *sb;
	int pct;
	char *p;

	VJ_subproc(JAIL_SUBPROC_CC);
	CAST_OBJ_NOTNULL(vp, priv, VCC_PRIV_MAGIC);

	AZ(chdir(vp->dir));

	sb = VSB_new_auto();
	AN(sb);
	for (p = mgt_cc_cmd, pct = 0; *p; ++p) {
		if (pct) {
			switch (*p) {
			case 's':
				VSB_cat(sb, VGC_SRC);
				break;
			case 'o':
				VSB_cat(sb, VGC_LIB);
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

	(void)umask(027);
	(void)execl("/bin/sh", "/bin/sh", "-c", VSB_data(sb), (char*)0);
	VSB_destroy(&sb);				// For flexelint
}

/*--------------------------------------------------------------------
 * Attempt to open compiled VCL in a sub-process
 */

static void v_matchproto_(vsub_func_f)
run_dlopen(void *priv)
{
	struct vcc_priv *vp;

	VJ_subproc(JAIL_SUBPROC_VCLLOAD);
	CAST_OBJ_NOTNULL(vp, priv, VCC_PRIV_MAGIC);
	if (VCL_TestLoad(vp->libfile))
		exit(1);
	exit(0);
}

/*--------------------------------------------------------------------
 * Touch a filename and make it available to privsep-privs
 */

static int
mgt_vcc_touchfile(const char *fn, struct vsb *sb)
{
	int i;

	i = open(fn, O_WRONLY|O_CREAT|O_TRUNC, 0640);
	if (i < 0) {
		VSB_printf(sb, "Failed to create %s: %s", fn, strerror(errno));
		return (2);
	}
	if (fchown(i, mgt_param.uid, mgt_param.gid) != 0)
		if (geteuid() == 0)
			VSB_printf(sb, "Failed to change owner on %s: %s\n",
			    fn, strerror(errno));
	closefd(&i);
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

	if (mgt_vcc_touchfile(vp->csrcfile, sb))
		return (2);
	if (mgt_vcc_touchfile(vp->libfile, sb))
		return (2);

	subs = VSUB_run(sb, run_vcc, vp, "VCC-compiler", -1);
	if (subs)
		return (subs);

	if (C_flag) {
		csrc = VFIL_readfile(NULL, vp->csrcfile, NULL);
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

char *
mgt_VccCompile(struct cli *cli, struct vclprog *vcl, const char *vclname,
    const char *vclsrc, const char *vclsrcfile, int C_flag)
{
	struct vcc_priv vp;
	struct vsb *sb;
	unsigned status;
	char buf[1024];
	FILE *fcs;
	char **av;
	int ac;

	AN(cli);

	sb = VSB_new_auto();
	XXXAN(sb);

	INIT_OBJ(&vp, VCC_PRIV_MAGIC);
	vp.vclsrc = vclsrc;
	vp.vclsrcfile = vclsrcfile;

	/*
	 * The subdirectory must have a unique name to 100% certain evade
	 * the refcounting semantics of dlopen(3).
	 *
	 * Bad implementations of dlopen(3) think the shlib you are opening
	 * is the same, if the filename is the same as one already opened.
	 *
	 * Sensible implementations do a stat(2) and requires st_ino and
	 * st_dev to also match.
	 *
	 * A correct implementation would run on filesystems which tickle
	 * st_gen, and also insist that be the identical, before declaring
	 * a match.
	 *
	 * Since no correct implementations are known to exist, we are subject
	 * to really interesting races if you do something like:
	 *
	 *	(running on 'boot' vcl)
	 *	vcl.load foo /foo.vcl
	 *	vcl.use foo
	 *	few/slow requests
	 *	vcl.use boot
	 *	vcl.discard foo
	 *	vcl.load foo /foo.vcl	// dlopen(3) says "same-same"
	 *	vcl.use foo
	 *
	 * Because discard of the first 'foo' lingers on non-zero reference
	 * count, and when it finally runs, it trashes the second 'foo' because
	 * dlopen(3) decided they were really the same thing.
	 *
	 * The Best way to reproduce this is to have regexps in the VCL.
	 */
	VSB_printf(sb, "vcl_%s.%.9f", vclname, VTIM_real());
	AZ(VSB_finish(sb));
	vp.dir = strdup(VSB_data(sb));
	AN(vp.dir);

	if (VJ_make_subdir(vp.dir, "VCL", cli->sb)) {
		free(vp.dir);
		VSB_destroy(&sb);
		VCLI_Out(cli, "VCL compilation failed");
		VCLI_SetResult(cli, CLIS_PARAM);
		return (NULL);
	}

	VSB_clear(sb);
	VSB_printf(sb, "%s/%s", vp.dir, VGC_SRC);
	AZ(VSB_finish(sb));
	vp.csrcfile = strdup(VSB_data(sb));
	AN(vp.csrcfile);
	VSB_clear(sb);

	VSB_printf(sb, "%s/%s", vp.dir, VGC_LIB);
	AZ(VSB_finish(sb));
	vp.libfile = strdup(VSB_data(sb));
	AN(vp.csrcfile);
	VSB_clear(sb);

	status = mgt_vcc_compile(&vp, sb, C_flag);

	AZ(VSB_finish(sb));
	if (VSB_len(sb) > 0)
		VCLI_Out(cli, "%s", VSB_data(sb));
	VSB_destroy(&sb);

	if (status || C_flag) {
		(void)unlink(vp.csrcfile);
		free(vp.csrcfile);
		(void)unlink(vp.libfile);
		free(vp.libfile);
		(void)rmdir(vp.dir);
		free(vp.dir);
		if (status) {
			VCLI_Out(cli, "VCL compilation failed");
			VCLI_SetResult(cli, CLIS_PARAM);
		}
		return (NULL);
	}

	fcs = fopen(vp.csrcfile, "r");
	AN(fcs);
	while (1) {
		AN(fgets(buf, sizeof buf, fcs));
		if (memcmp(buf, VCC_INFO_PREFIX, strlen(VCC_INFO_PREFIX)))
			break;
		av = VAV_Parse(buf, &ac, 0);
		AN(av);
		AZ(av[0]);
		AZ(strcmp(av[1], "/*"));
		AZ(strcmp(av[ac-1], "*/"));
		if (!strcmp(av[3], "VCL"))
			mgt_vcl_depends(vcl, av[4]);
		else if (!strcmp(av[3], "VMOD"))
			mgt_vcl_vmod(vcl, av[4], av[5]);
		else
			WRONG("Wrong VCCINFO");
		VAV_Free(av);
	}
	AZ(fclose(fcs));

	(void)unlink(vp.csrcfile);
	free(vp.csrcfile);

	free(vp.dir);

	VCLI_Out(cli, "VCL compiled.\n");

	return (vp.libfile);
}
