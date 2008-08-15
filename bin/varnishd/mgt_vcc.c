/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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
 * $Id$
 *
 * VCL compiler stuff
 */

#include "config.h"

#include <sys/types.h>
#include <sys/wait.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#ifndef HAVE_ASPRINTF
#include "compat/asprintf.h"
#endif
#include "vsb.h"
#include "vlu.h"

#include "vqueue.h"

#include "libvcl.h"
#include "cli.h"
#include "cli_priv.h"
#include "cli_common.h"

#include "mgt.h"
#include "mgt_cli.h"
#include "heritage.h"

#include "vss.h"

struct vclprog {
	VTAILQ_ENTRY(vclprog)	list;
	char 			*name;
	char			*fname;
	int			active;
};

static VTAILQ_HEAD(, vclprog) vclhead = VTAILQ_HEAD_INITIALIZER(vclhead);

char *mgt_cc_cmd;

/*--------------------------------------------------------------------*/

/*
 * Keep this in synch with man/vcl.7 and etc/default.vcl!
 */
static const char *default_vcl =
#include "default_vcl.h"
    ""	;

/*
 * Prepare the compiler command line
 */
static void
mgt_make_cc_cmd(struct vsb *sb, const char *sf, const char *of)
{
	int pct;
	char *p;

	for (p = mgt_cc_cmd, pct = 0; *p; ++p) {
		if (pct) {
			switch (*p) {
			case 's':
				vsb_cat(sb, sf);
				break;
			case 'o':
				vsb_cat(sb, of);
				break;
			case '%':
				vsb_putc(sb, '%');
				break;
			default:
				vsb_putc(sb, '%');
				vsb_putc(sb, *p);
				break;
			}
			pct = 0;
		} else if (*p == '%') {
			pct = 1;
		} else {
			vsb_putc(sb, *p);
		}
	}
	if (pct)
		vsb_putc(sb, '%');
}

/*--------------------------------------------------------------------
 * Invoke system C compiler on source and return resulting dlfile.
 * Errors goes in sb;
 */

static int
mgt_cc_vlu(void *priv, const char *str)
{
	struct vsb *vsb;

	vsb = priv;
	vsb_printf(vsb, "C-compiler said: %s\n", str);
	return (0);
}

static char *
mgt_run_cc(const char *source, struct vsb *sb)
{
	char cmdline[1024];
	struct vsb cmdsb;
	char sf[] = "./vcl.########.c";
	char of[sizeof sf + 1];
	char *retval;
	int rv, p[2], sfd, srclen, status;
	pid_t pid;
	void *dlh;
	struct vlu *vlu;

	/* Create temporary C source file */
	sfd = vtmpfile(sf);
	if (sfd < 0) {
		vsb_printf(sb,
		    "%s(): failed to create %s: %s",
		    __func__, sf, strerror(errno));
		return (NULL);
	}
	srclen = strlen(source);
	if (write(sfd, source, srclen) != srclen) {
		vsb_printf(sb,
		    "Failed to write C source to file: %s",
		    strerror(errno));
		AZ(unlink(sf));
		AZ(close(sfd));
		return (NULL);
	}
	AZ(close(sfd));

	/* Name the output shared library by overwriting the final 'c' */
	memcpy(of, sf, sizeof sf);
	assert(sf[sizeof sf - 2] == 'c');
	of[sizeof sf - 2] = 's';
	of[sizeof sf - 1] = 'o';
	of[sizeof sf] = '\0';
	AN(vsb_new(&cmdsb, cmdline, sizeof cmdline, 0));
	mgt_make_cc_cmd(&cmdsb, sf, of);
	vsb_finish(&cmdsb);
	AZ(vsb_overflowed(&cmdsb));
	/* XXX check vsb state */

	if (pipe(p) < 0) {
		vsb_printf(sb, "%s(): pipe() failed: %s",
		    __func__, strerror(errno));
		(void)unlink(sf);
		return (NULL);
	}
	assert(p[0] > STDERR_FILENO);
	assert(p[1] > STDERR_FILENO);
	if ((pid = fork()) < 0) {
		vsb_printf(sb, "%s(): fork() failed: %s",
		    __func__, strerror(errno));
		AZ(close(p[0]));
		AZ(close(p[1]));
		(void)unlink(sf);
		return (NULL);
	}
	if (pid == 0) {
		AZ(close(STDIN_FILENO));
		assert(open("/dev/null", O_RDONLY) == STDIN_FILENO);
		assert(dup2(p[1], STDOUT_FILENO) == STDOUT_FILENO);
		assert(dup2(p[1], STDERR_FILENO) == STDERR_FILENO);
		/* Close all other fds */
		for (sfd = STDERR_FILENO + 1; sfd < 100; sfd++)
			(void)close(sfd);
		(void)execl("/bin/sh", "/bin/sh", "-c", cmdline, NULL);
		_exit(1);
	}
	AZ(close(p[1]));
	vlu = VLU_New(sb, mgt_cc_vlu, 0);
	while (!VLU_Fd(p[0], vlu))
		continue;
	AZ(close(p[0]));
	VLU_Destroy(vlu);
	(void)unlink(sf);
	do {
		rv = waitpid(pid, &status, 0);
		if (rv < 0 && errno != EINTR) {
			vsb_printf(sb, "%s(): waitpid() failed: %s",
			    __func__, strerror(errno));
			(void)unlink(of);
			return (NULL);
		}
	} while (rv < 0);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		vsb_printf(sb, "%s(): Compiler failed", __func__);
		if (WIFEXITED(status))
			vsb_printf(sb, ", exit %d", WEXITSTATUS(status));
		if (WIFSIGNALED(status))
			vsb_printf(sb, ", signal %d", WTERMSIG(status));
		if (WCOREDUMP(status))
			vsb_printf(sb, ", core dumped");
		(void)unlink(of);
		return (NULL);
	}

	/* Next, try to load the object into the management process */
	if ((dlh = dlopen(of, RTLD_NOW | RTLD_LOCAL)) == NULL) {
		vsb_printf(sb,
		    "%s(): failed to load compiled VCL program:\n  %s",
		    __func__, dlerror());
		(void)unlink(of);
		return (NULL);
	}

	/*
	 * XXX: we should look up and check the handle in the loaded
	 * object
	 */

	AZ(dlclose(dlh));
	retval = strdup(of);
	XXXAN(retval);
	return (retval);
}

/*--------------------------------------------------------------------*/

static char *
mgt_VccCompile(struct vsb *sb, const char *b, const char *e, int C_flag)
{
	char *csrc, *vf = NULL;

	csrc = VCC_Compile(sb, b, e);
	if (csrc != NULL) {
		if (C_flag)
			(void)fputs(csrc, stdout);
		vf = mgt_run_cc(csrc, sb);
		if (C_flag && vf != NULL)
			AZ(unlink(vf));
		free(csrc);
	}
	return (vf);
}

static char *
mgt_VccCompileFile(struct vsb *sb, const char *fn, int C_flag, int fd)
{
	char *csrc, *vf = NULL;

	csrc = VCC_CompileFile(sb, fn, fd);
	if (csrc != NULL) {
		if (C_flag)
			(void)fputs(csrc, stdout);
		vf = mgt_run_cc(csrc, sb);
		if (C_flag && vf != NULL)
			AZ(unlink(vf));
		free(csrc);
	}
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
mgt_vcc_default(const char *b_arg, const char *f_arg, int f_fd, int C_flag)
{
	char *addr, *port;
	char *buf, *vf;
	struct vsb *sb;
	struct vclprog *vp;

	sb = vsb_newauto();
	XXXAN(sb);
	if (b_arg != NULL) {
		/*
		 * XXX: should do a "HEAD /" on the -b argument to see that
		 * XXX: it even works.  On the other hand, we should do that
		 * XXX: for all backends in the cache process whenever we
		 * XXX: change config, but for a complex VCL, it might not be
		 * XXX: a bug for a backend to not reply at that time, so then
		 * XXX: again: we should check it here in the "trivial" case.
		 */
		if (VSS_parse(b_arg, &addr, &port) != 0 || addr == NULL) {
			/*
			 * (addr == NULL && port != NULL) is possible if
			 * the user incorrectly specified an address such
			 * as ":80", which is a valid listening address.
			 * In the future, we may want to interpret this as
			 * a shortcut for "localhost:80".
			 */
			free(port);
			fprintf(stderr, "invalid backend address\n");
			vsb_delete(sb);
			return (1);
		}

		buf = NULL;
		asprintf(&buf,
		    "backend default {\n"
		    "    .host = \"%s\";\n"
		    "    .port = \"%s\";\n"
		    "}\n", addr, port ? port : "http");
		free(addr);
		free(port);
		AN(buf);
		vf = mgt_VccCompile(sb, buf, NULL, C_flag);
		free(buf);
	} else {
		vf = mgt_VccCompileFile(sb, f_arg, C_flag, f_fd);
	}
	vsb_finish(sb);
	AZ(vsb_overflowed(sb));
	if (vsb_len(sb) > 0)
		fprintf(stderr, "%s", vsb_data(sb));
	vsb_delete(sb);
	if (C_flag)
		return (0);
	if (vf == NULL) {
		fprintf(stderr, "\nVCL compilation failed\n");
		return (1);
	}
	vp = mgt_vcc_add("boot", vf);
	vp->active = 1;
	return (0);
}

/*--------------------------------------------------------------------*/

int
mgt_has_vcl()
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
		    "vcl.load %s %s\n", vp->name, vp->fname))
			return (1);
		free(*p);
		if (!vp->active)
			continue;
		if (mgt_cli_askchild(status, p,
		    "vcl.use %s\n", vp->name))
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
		mgt_vcc_del(vp);
	}
}

void
mgt_vcc_init(void)
{

	VCC_InitCompile(default_vcl);
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
		cli_out(cli, "Already a VCL program named %s", av[2]);
		cli_result(cli, CLIS_PARAM);
		return;
	}
	
	sb = vsb_newauto();
	XXXAN(sb);
	vf = mgt_VccCompile(sb, av[3], NULL, 0);
	vsb_finish(sb);
	AZ(vsb_overflowed(sb));
	if (vsb_len(sb) > 0)
		cli_out(cli, "%s", vsb_data(sb));
	vsb_delete(sb);
	if (vf == NULL) {
		cli_out(cli, "VCL compilation failed");
		cli_result(cli, CLIS_PARAM);
		return;
	}
	cli_out(cli, "VCL compiled.");
	if (child_pid >= 0 &&
	    mgt_cli_askchild(&status, &p, "vcl.load %s %s\n", av[2], vf)) {
		cli_result(cli, status);
		cli_out(cli, "%s", p);
	} else {
		(void)mgt_vcc_add(av[2], vf);
	}
	free(p);
}

void
mcf_config_load(struct cli *cli, const char * const *av, void *priv)
{
	char *vf;
	struct vsb *sb;
	unsigned status;
	char *p = NULL;
	struct vclprog *vp;

	(void)priv;
	vp = mgt_vcc_byname(av[2]);
	if (vp != NULL) {
		cli_out(cli, "Already a VCL program named %s", av[2]);
		cli_result(cli, CLIS_PARAM);
		return;
	}

	sb = vsb_newauto();
	XXXAN(sb);
	vf = mgt_VccCompileFile(sb, av[3], 0, -1);
	vsb_finish(sb);
	AZ(vsb_overflowed(sb));
	if (vsb_len(sb) > 0)
		cli_out(cli, "%s", vsb_data(sb));
	vsb_delete(sb);
	if (vf == NULL) {
		cli_out(cli, "VCL compilation failed");
		cli_result(cli, CLIS_PARAM);
		return;
	}
	cli_out(cli, "VCL compiled.");
	if (child_pid >= 0 &&
	    mgt_cli_askchild(&status, &p, "vcl.load %s %s\n", av[2], vf)) {
		cli_result(cli, status);
		cli_out(cli, "%s", p);
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
	cli_result(cli, CLIS_PARAM);
	cli_out(cli, "No configuration named %s known.", name);
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
		cli_result(cli, status);
		cli_out(cli, "%s", p);
	} else {
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
		cli_result(cli, CLIS_PARAM);
		cli_out(cli, "Cannot discard active VCL program\n");
	} else if (vp != NULL) {
		if (child_pid >= 0 &&
		    mgt_cli_askchild(&status, &p,
		    "vcl.discard %s\n", av[2])) {
			cli_result(cli, status);
			cli_out(cli, "%s", p);
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
			cli_result(cli, status);
			cli_out(cli, "%s", p);
		}
		free(p);
	} else {
		VTAILQ_FOREACH(vp, &vclhead, list) {
			if (vp->active) {
				flg = "active";
			} else
				flg = "available";
			cli_out(cli, "%-10s %6s %s\n",
			    flg, "N/A", vp->name);
		}
	}
}

/*
 * XXX: This should take an option argument to show all (include) files
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
			cli_out(cli, "failed to load %s: %s\n",
			    vp->name, dlerror());
			cli_result(cli, CLIS_CANT);
		} else if ((sym = dlsym(dlh, "srcbody")) == NULL) {
			cli_out(cli, "failed to locate source for %s: %s\n",
			    vp->name, dlerror());
			cli_result(cli, CLIS_CANT);
			AZ(dlclose(dlh));
		} else {
			src = sym;
			cli_out(cli, src[0]);
			/* cli_out(cli, src[1]); */
			AZ(dlclose(dlh));
		}
	}
}
