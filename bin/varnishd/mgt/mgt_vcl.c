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
 * VCL management stuff
 */

#include "config.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "mgt/mgt.h"
#include "common/params.h"

#include "vcl.h"
#include "vcli.h"
#include "vcli_priv.h"
#include "vev.h"
#include "vfil.h"
#include "vtim.h"

#include "mgt_cli.h"

struct vclprog {
	VTAILQ_ENTRY(vclprog)	list;
	char			*name;
	char			*fname;
	int			active;
	int			warm;
	char			state[8];
	double			go_cold;
};

static VTAILQ_HEAD(, vclprog) vclhead = VTAILQ_HEAD_INITIALIZER(vclhead);


/*--------------------------------------------------------------------*/

static struct vclprog *
mgt_vcl_add(const char *name, const char *libfile, const char *state)
{
	struct vclprog *vp;

	vp = calloc(sizeof *vp, 1);
	XXXAN(vp);
	REPLACE(vp->name, name);
	REPLACE(vp->fname, libfile);
	if (strcmp(state, "cold"))
		vp->warm = 1;
	else
		state = "auto";

	bprintf(vp->state, "%s", state);

	if (VTAILQ_EMPTY(&vclhead))
		vp->active = 1;
	VTAILQ_INSERT_TAIL(&vclhead, vp, list);
	return (vp);
}

static void
mgt_vcl_del(struct vclprog *vp)
{
	VTAILQ_REMOVE(&vclhead, vp, list);
	printf("unlink %s\n", vp->fname);
	XXXAZ(unlink(vp->fname));
	free(vp->fname);
	free(vp->name);
	free(vp);
}

static struct vclprog *
mgt_vcl_byname(const char *name)
{
	struct vclprog *vp;

	VTAILQ_FOREACH(vp, &vclhead, list)
		if (!strcmp(name, vp->name))
			return (vp);
	return (NULL);
}


static int
mgt_vcl_delbyname(const char *name)
{
	struct vclprog *vp;

	vp = mgt_vcl_byname(name);
	if (vp != NULL) {
		mgt_vcl_del(vp);
		return (0);
	}
	return (1);
}

int
mgt_has_vcl(void)
{

	return (!VTAILQ_EMPTY(&vclhead));
}

static void
mgt_vcl_setstate(struct vclprog *vp, int warm)
{
	unsigned status;
	char *p;

	if (vp->warm == warm)
		return;

	vp->warm = warm;

	if (vp->warm == 0)
		vp->go_cold = 0;

	if (child_pid < 0)
		return;

	AZ(mgt_cli_askchild(&status, &p, "vcl.state %s %d%s\n",
	    vp->name, vp->warm, vp->state));

	free(p);
}

/*--------------------------------------------------------------------*/

static void
mgt_new_vcl(struct cli *cli, const char *vclname, const char *vclsrc,
    const char *state, int C_flag)
{
	unsigned status;
	char *lib, *p;
	struct vclprog *vp;

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

	vp = mgt_vcl_add(vclname, lib, state);
	free(lib);

	if (child_pid < 0)
		return;

	if (!mgt_cli_askchild(&status, &p, "vcl.load %s %s %d%s\n",
	    vp->name, vp->fname, vp->warm, vp->state)) {
		free(p);
		return;
	}

	mgt_vcl_del(vp);
	VCLI_Out(cli, "%s", p);
	free(p);
	VCLI_SetResult(cli, CLIS_PARAM);
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
		    "vcl.load \"%s\" %s %d%s\n",
		    vp->name, vp->fname, vp->warm, vp->state))
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

void
mcf_vcl_inline(struct cli *cli, const char * const *av, void *priv)
{
	struct vclprog *vp;

	(void)priv;

	vp = mgt_vcl_byname(av[2]);
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
	vp = mgt_vcl_byname(av[2]);
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

	vp = mgt_vcl_byname(name);
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
		mgt_vcl_setstate(vp, 0);
	} else if (!strcmp(av[3], "warm")) {
		bprintf(vp->state, "%s", av[3]);
		mgt_vcl_setstate(vp, 1);
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
	struct vclprog *vp, *vp2;

	(void)priv;
	vp = mcf_find_vcl(cli, av[2]);
	if (vp == NULL)
		return;
	if (vp->active != 0)
		return;
	mgt_vcl_setstate(vp, 1);
	if (child_pid >= 0 &&
	    mgt_cli_askchild(&status, &p, "vcl.use %s\n", av[2])) {
		VCLI_SetResult(cli, status);
		VCLI_Out(cli, "%s", p);
	} else {
		VCLI_Out(cli, "VCL '%s' now active", av[2]);
		VTAILQ_FOREACH(vp2, &vclhead, list) {
			if (vp2->active) {
				vp2->active = 0;
				vp2->go_cold = VTIM_mono();
				break;
			}
		}
		vp->active = 1;
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
			AZ(mgt_vcl_delbyname(av[2]));
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
			VCLI_Out(cli, "%-10s %4s/%s  %s\n", flg, vp->state,
			    vp->warm ? "warm" : "cold", vp->name);
		}
	}
}

/*--------------------------------------------------------------------*/

static int __match_proto__(vev_cb_f)
mgt_vcl_poker(const struct vev *e, int what)
{
	struct vclprog *vp;
	double now = VTIM_mono();

	(void)e;
	(void)what;
	VTAILQ_FOREACH(vp, &vclhead, list) {
		if (vp->go_cold == 0)
			continue;
		if (strcmp(vp->state, "auto"))
			continue;
		if (vp->go_cold + mgt_param.vcl_cooldown < now)
			mgt_vcl_setstate(vp, 0);
	}
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
	struct vev *e;

	e = vev_new();
	AN(e);
	e->timeout = 23;		// random, prime
	e->callback = mgt_vcl_poker;
	e->name = "vcl poker";
	AZ(vev_add(mgt_evb, e));

	AZ(atexit(mgt_vcl_atexit));
}
