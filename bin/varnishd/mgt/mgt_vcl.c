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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "mgt/mgt.h"

#include "vcl.h"
#include "vcli.h"
#include "vcli_priv.h"
#include "vfil.h"

#include "mgt_cli.h"

struct vclprog {
	VTAILQ_ENTRY(vclprog)	list;
	char			*name;
	char			*fname;
	int			active;
	char			state[8];
};

static VTAILQ_HEAD(, vclprog) vclhead = VTAILQ_HEAD_INITIALIZER(vclhead);


/*--------------------------------------------------------------------*/

static void
mgt_vcc_add(const char *name, const char *libfile, const char *state)
{
	struct vclprog *vp;

	vp = calloc(sizeof *vp, 1);
	XXXAN(vp);
	REPLACE(vp->name, name);
	REPLACE(vp->fname, libfile);
	bprintf(vp->state, "%s", state);
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

/*--------------------------------------------------------------------*/

static void
mgt_new_vcl(struct cli *cli, const char *vclname, const char *vclsrc,
    const char *state, int C_flag)
{
	unsigned status;
	char *lib, *p;

	AN(cli);

	if (state == NULL)
		state = "auto";

	if (strcmp(state, "auto") &&
	    strcmp(state, "cold") && strcmp(state, "warm")) {
		VCLI_Out(cli, "State must be one of auto, cold or warm.");
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	}

	lib = mgt_VccCompile(cli, vclname, vclsrc, C_flag);
	if (lib == NULL)
		return;

	VCLI_Out(cli, "VCL compiled.\n");

	if (child_pid < 0) {
		mgt_vcc_add(vclname, lib, state);
		free(lib);
		return;
	}

	if (!mgt_cli_askchild(&status, &p,
	    "vcl.load %s %s %s\n", vclname, lib, state)) {
		mgt_vcc_add(vclname, lib, state);
		free(lib);
		free(p);
		return;
	}

	VCLI_Out(cli, "%s", p);
	free(p);
	VCLI_SetResult(cli, CLIS_PARAM);
	(void)unlink(lib);
	free(lib);
}

/*--------------------------------------------------------------------*/

void
mgt_vcc_default(struct cli *cli, const char *b_arg, const char *vclsrc,
    int C_flag)
{
	char buf[BUFSIZ];

	if (b_arg == NULL) {
		AN(vclsrc);
		mgt_new_vcl(cli, "boot", vclsrc, NULL, C_flag);
		return;
	}

	AZ(vclsrc);
	bprintf(buf,
	    "vcl 4.0;\n"
	    "backend default {\n"
	    "    .host = \"%s\";\n"
	    "}\n", b_arg);
	mgt_new_vcl(cli, "boot", buf, NULL, C_flag);
}

/*--------------------------------------------------------------------*/

int
mgt_push_vcls_and_start(unsigned *status, char **p)
{
	struct vclprog *vp;

	VTAILQ_FOREACH(vp, &vclhead, list) {
		if (mgt_cli_askchild(status, p,
		    "vcl.load \"%s\" %s %s\n", vp->name, vp->fname, vp->state))
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
mgt_vcl_atexit(void)
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
mgt_vcl_init(void)
{

	AZ(atexit(mgt_vcl_atexit));
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

	mgt_new_vcl(cli, av[2], av[3], av[4], 0);
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

	mgt_new_vcl(cli, av[2], vcl, av[4], 0);
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
mcf_vcl_state(struct cli *cli, const char * const *av, void *priv)
{
	struct vclprog *vp;

	(void)priv;
	vp = mcf_find_vcl(cli, av[2]);
	if (vp == NULL)
		return;

	if (!strcmp(vp->state, av[3]))
		return;

	if (!strcmp(av[3], "auto")) {
		bprintf(vp->state, "%s", "auto");
	} else if (!strcmp(av[3], "cold")) {
		if (vp->active) {
			VCLI_Out(cli, "Cannot set active VCL cold.");
			VCLI_SetResult(cli, CLIS_PARAM);
			return;
		}
		bprintf(vp->state, "%s", "auto");
	} else if (!strcmp(av[3], "warm")) {
		bprintf(vp->state, "%s", av[3]);
	} else {
		VCLI_Out(cli, "State must be one of auto, cold or warm.");
		VCLI_SetResult(cli, CLIS_PARAM);
	}
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
			VCLI_Out(cli, "%-10s %4s/?    %6s %s\n",
			    flg, vp->state, "N/A", vp->name);
		}
	}
}
