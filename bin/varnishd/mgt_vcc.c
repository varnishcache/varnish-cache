/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2007 Linpro AS
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

#include <sys/types.h>

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef HAVE_ASPRINTF
#include "compat/asprintf.h"
#endif
#include "vsb.h"
#include "queue.h"

#include "libvcl.h"
#include "cli.h"
#include "cli_priv.h"
#include "cli_common.h"

#include "mgt.h"
#include "mgt_cli.h"
#include "heritage.h"

#include "vss.h"

struct vclprog {
	TAILQ_ENTRY(vclprog)	list;
	char 			*name;
	char			*fname;
	int			active;
};

static TAILQ_HEAD(, vclprog) vclhead = TAILQ_HEAD_INITIALIZER(vclhead);

/*--------------------------------------------------------------------*/

/* keep this in synch with man/vcl.7 */
static const char *default_vcl =
    "sub vcl_recv {\n"
    "    if (req.request != \"GET\" && req.request != \"HEAD\") {\n"
    "        pipe;\n"
    "    }\n"
    "    if (req.http.Expect) {\n"
    "        pipe;\n"
    "    }\n"
    "    if (req.http.Authenticate || req.http.Cookie) {\n"
    "        pass;\n"
    "    }\n"
    "    lookup;\n"
    "}\n"
    "\n"
    "sub vcl_pipe {\n"
    "    pipe;\n"
    "}\n"
    "\n"
    "sub vcl_pass {\n"
    "    pass;\n"
    "}\n"
    "\n"
    "sub vcl_hash {\n"
    "    set req.hash += req.url;\n"
#if 1
    "    set req.hash += req.http.host;\n"
#else
    "    if (req.http.host) {\n"
    "        set req.hash += req.http.host;\n"
    "    } else {\n"
    "        set req.hash += server.ip;\n"	/* XXX: see ticket 137 */
    "    }\n"
#endif
    "    hash;\n"
    "}\n"
    "\n"
    "sub vcl_hit {\n"
    "    if (!obj.cacheable) {\n"
    "        pass;\n"
    "    }\n"
    "    deliver;\n"
    "}\n"
    "\n"
    "sub vcl_miss {\n"
    "    fetch;\n"
    "}\n"
    "\n"
    "sub vcl_fetch {\n"
    "    if (!obj.valid) {\n"
    "        error;\n"
    "    }\n"
    "    if (!obj.cacheable) {\n"
    "        pass;\n"
    "    }\n"
    "    if (obj.http.Set-Cookie) {\n"
    "        pass;\n"
    "    }\n"
    "    insert;\n"
    "}\n"
    "sub vcl_deliver {\n"
    "    deliver;\n"
    "}\n"
    "sub vcl_discard {\n"
    "    discard;\n"
    "}\n"
    "sub vcl_timeout {\n"
    "    discard;\n"
    "}\n";

/*--------------------------------------------------------------------
 * Invoke system C compiler on source and return resulting dlfile.
 * Errors goes in sb;
 */

static char *
mgt_CallCc(const char *source, struct vsb *sb)
{
	FILE *fo, *fs;
	char sf[] = "./vcl.XXXXXXXX";
	char *of;
	char buf[BUFSIZ];
	int i, j, len, sfd;
	void *p;

	/* Create temporary C source file */
	sfd = mkstemp(sf);
	if (sfd < 0) {
		vsb_printf(sb,
		    "Cannot open temporary source file \"%s\": %s\n",
		    sf, strerror(errno));
		return (NULL);
	}
	fs = fdopen(sfd, "r+");
	AN(fs);

	if (fputs(source, fs) < 0 || fflush(fs)) {
		vsb_printf(sb,
		    "Write error to C source file: %s\n",
		    strerror(errno));
		unlink(sf);
		fclose(fs);
		return (NULL);
	}
	rewind(fs);

	/* Name the output shared library */
	of = strdup(sf);
	AN(of);
	memcpy(of, "./bin", 5);

	/* Attempt to open a pipe to the system C-compiler */
	len = snprintf(buf, sizeof buf,
            "ln -f %s _.c ;"			/* XXX: for debugging */
#ifdef __APPLE__
	    "exec cc -dynamiclib -Wl,-undefined,dynamic_lookup -o %s -x c - < %s 2>&1",
#else
	    "exec cc -fpic -shared -Wl,-x -o %s -x c - < %s 2>&1",
#endif
	    sf, of, sf);
	xxxassert(len < sizeof buf);

	fo = popen(buf, "r");
	if (fo == NULL) {
		vsb_printf(sb,
		    "Internal error: Cannot execute cc(1): %s\n",
		    strerror(errno));
		free(of);
		unlink(sf);
		fclose(fs);
		return (NULL);
	}

	/* If we get any output, it's bad */
	j = 0;
	while (1) {
		if (fgets(buf, sizeof buf, fo) == NULL)
			break;
		if (!j) {
			vsb_printf(sb, "Internal error: cc(1) complained:\n");
			j++;
		}
		vsb_cat(sb, buf);
	}

	i = pclose(fo);
	if (j == 0 && i != 0)
		vsb_printf(sb,
		    "Internal error: cc(1) exit status 0x%04x\n", i);

	/* If the compiler complained, or exited non-zero, fail */
	if (i || j) {
		unlink(of);
		free(of);
		of = NULL;
	}

	/* Next, try to load the object into the management process */
	p = dlopen(of, RTLD_NOW | RTLD_LOCAL);
	if (p == NULL) {
		vsb_printf(sb, "Problem loading compiled VCL program:\n\t%s\n",
		    dlerror());
		unlink(of);
		free(of);
		of = NULL;
	} else
		(void)dlclose(p);

	/* clean up and return */
	unlink(sf);
	fclose(fs);
	return (of);
}

/*--------------------------------------------------------------------*/

static char *
mgt_VccCompile(struct vsb *sb, const char *b, const char *e, int C_flag)
{
	char *csrc, *vf = NULL;

	csrc = VCC_Compile(sb, b, e);
	if (csrc != NULL) {
		if (C_flag)
			fputs(csrc, stdout);
		vf = mgt_CallCc(csrc, sb);
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
			fputs(csrc, stdout);
		vf = mgt_CallCc(csrc, sb);
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
	vp->fname = file;
	TAILQ_INSERT_TAIL(&vclhead, vp, list);
	return (vp);
}

static void
mgt_vcc_del(struct vclprog *vp)
{
	TAILQ_REMOVE(&vclhead, vp, list);
	printf("unlink %s\n", vp->fname);
	XXXAZ(unlink(vp->fname));
	free(vp->fname);
	free(vp->name);
	free(vp);
}

static int
mgt_vcc_delbyname(const char *name)
{
	struct vclprog *vp;

	TAILQ_FOREACH(vp, &vclhead, list) {
		if (!strcmp(name, vp->name)) {
			mgt_vcc_del(vp);
			return (0);
		}
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

	sb = vsb_new(NULL, NULL, 0, VSB_AUTOEXTEND);
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
			fprintf(stderr, "invalid backend address\n");
			return (1);
		}

		buf = NULL;
		asprintf(&buf,
		    "backend default {\n"
		    "    set backend.host = \"%s\";\n"
		    "    set backend.port = \"%s\";\n"
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
	if (vsb_len(sb) > 0) {
		fprintf(stderr, "%s", vsb_data(sb));
		vsb_delete(sb);
		return (1);
	}
	vsb_delete(sb);
	if (C_flag)
		return (0);
	vp = mgt_vcc_add("boot", vf);
	vp->active = 1;
	return (0);
}

/*--------------------------------------------------------------------*/

int
mgt_push_vcls_and_start(unsigned *status, char **p)
{
	struct vclprog *vp;

	TAILQ_FOREACH(vp, &vclhead, list) {
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
		vp = TAILQ_FIRST(&vclhead);
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
mcf_config_inline(struct cli *cli, char **av, void *priv)
{
	char *vf, *p;
	struct vsb *sb;
	unsigned status;

	(void)priv;

	sb = vsb_new(NULL, NULL, 0, VSB_AUTOEXTEND);
	XXXAN(sb);
	vf = mgt_VccCompile(sb, av[3], NULL, 0);
	vsb_finish(sb);
	if (vsb_len(sb) > 0) {
		cli_out(cli, "%s", vsb_data(sb));
		vsb_delete(sb);
		cli_result(cli, CLIS_PARAM);
		return;
	}
	vsb_delete(sb);
	if (child_pid >= 0 &&
	    mgt_cli_askchild(&status, &p, "vcl.load %s %s\n", av[2], vf)) {
		cli_result(cli, status);
		cli_out(cli, "%s", p);
		free(p);
		return;
	}
	mgt_vcc_add(av[2], vf);
}

void
mcf_config_load(struct cli *cli, char **av, void *priv)
{
	char *vf;
	struct vsb *sb;
	unsigned status;
	char *p;

	(void)priv;

	sb = vsb_new(NULL, NULL, 0, VSB_AUTOEXTEND);
	XXXAN(sb);
	vf = mgt_VccCompileFile(sb, av[3], 0, -1);
	vsb_finish(sb);
	if (vsb_len(sb) > 0) {
		cli_out(cli, "%s", vsb_data(sb));
		vsb_delete(sb);
		cli_result(cli, CLIS_PARAM);
		return;
	}
	vsb_delete(sb);
	if (child_pid >= 0 &&
	    mgt_cli_askchild(&status, &p, "vcl.load %s %s\n", av[2], vf)) {
		cli_result(cli, status);
		cli_out(cli, "%s", p);
		free(p);
		return;
	}
	mgt_vcc_add(av[2], vf);
}

static struct vclprog *
mcf_find_vcl(struct cli *cli, const char *name)
{
	struct vclprog *vp;

	TAILQ_FOREACH(vp, &vclhead, list)
		if (!strcmp(vp->name, name))
			break;
	if (vp == NULL) {
		cli_result(cli, CLIS_PARAM);
		cli_out(cli, "No configuration named %s known.", name);
	}
	return (vp);
}

void
mcf_config_use(struct cli *cli, char **av, void *priv)
{
	unsigned status;
	char *p;
	struct vclprog *vp;

	(void)priv;
	vp = mcf_find_vcl(cli, av[2]);
	if (vp != NULL && vp->active == 0) {
		if (child_pid >= 0 &&
		    mgt_cli_askchild(&status, &p, "vcl.use %s\n", av[2])) {
			cli_result(cli, status);
			cli_out(cli, "%s", p);
			free(p);
		} else {
			vp->active = 2;
			TAILQ_FOREACH(vp, &vclhead, list) {
				if (vp->active == 1)
					vp->active = 0;
				else if (vp->active == 2)
					vp->active = 1;
			}
		}
	}
}

void
mcf_config_discard(struct cli *cli, char **av, void *priv)
{
	unsigned status;
	char *p;
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
			free(p);
		} else {
			AZ(mgt_vcc_delbyname(av[2]));
		}
	}
}

void
mcf_config_list(struct cli *cli, char **av, void *priv)
{
	unsigned status;
	char *p;
	struct vclprog *vp;

	(void)av;
	(void)priv;
	if (child_pid >= 0) {
		mgt_cli_askchild(&status, &p, "vcl.list\n");
		cli_result(cli, status);
		cli_out(cli, "%s", p);
		free(p);
	} else {
		TAILQ_FOREACH(vp, &vclhead, list) {
			cli_out(cli, "%s %6s %s\n",
			    vp->active ? "*" : " ",
			    "N/A", vp->name);
		}
	}
}

void
mcf_config_show(struct cli *cli, char **av, void *priv)
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
			dlclose(dlh);
		} else {
			src = sym;
			cli_out(cli, src[0]);
			/* cli_out(cli, src[1]); */
			dlclose(dlh);
		}
	}
}
