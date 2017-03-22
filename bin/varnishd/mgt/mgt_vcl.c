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

#include "vcli.h"
#include "vcli_priv.h"
#include "vev.h"
#include "vtim.h"

#include "mgt_cli.h"

static const char * const VCL_STATE_COLD = "cold";
static const char * const VCL_STATE_WARM = "warm";
static const char * const VCL_STATE_AUTO = "auto";

struct vclprog {
	VTAILQ_ENTRY(vclprog)	list;
	char			*name;
	char			*fname;
	unsigned		warm;
	char			state[8];
	double			go_cold;
};

static VTAILQ_HEAD(, vclprog) vclhead = VTAILQ_HEAD_INITIALIZER(vclhead);
static struct vclprog		*active_vcl;
static struct vev *e_poker;

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

	VTAILQ_INSERT_TAIL(&vclhead, vp, list);
	return (vp);
}

static void
mgt_vcl_del(struct vclprog *vp)
{
	char *p;

	VTAILQ_REMOVE(&vclhead, vp, list);
	AZ(unlink(vp->fname));
	p = strrchr(vp->fname, '/');
	AN(p);
	*p = '\0';
	VJ_master(JAIL_MASTER_FILE);
	/*
	 * This will fail if any files are dropped next to the library
	 * without us knowing.  This happens for instance with GCOV.
	 * Assume developers know how to clean up after themselves
	 * (or alternatively:  How to run out of disk space).
	 */
	(void)rmdir(vp->fname);
	VJ_master(JAIL_MASTER_LOW);
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

int
mgt_has_vcl(void)
{

	return (!VTAILQ_EMPTY(&vclhead));
}

static int
mgt_vcl_setstate(struct cli *cli, struct vclprog *vp, const char *vs)
{
	unsigned status, warm;
	double now;
	char *p;
	int i;

	if (vs == VCL_STATE_AUTO) {
		assert(vp != active_vcl);
		now = VTIM_mono();
		vs = vp->warm ? VCL_STATE_WARM : VCL_STATE_COLD;
		if (vp->go_cold > 0 && !strcmp(vp->state, "auto") &&
		    vp->go_cold + mgt_param.vcl_cooldown < now)
			vs = VCL_STATE_COLD;
	}

	assert(vs != VCL_STATE_AUTO);
	warm = vs == VCL_STATE_WARM ? 1 : 0;

	if (vp->warm == warm)
		return (0);

	if (child_pid < 0) {
		vp->warm = warm;
		if (vp->warm == 0)
			vp->go_cold = 0;
		return (0);
	}

	i = mgt_cli_askchild(&status, &p, "vcl.state %s %d%s\n",
	    vp->name, warm, vp->state);
	if (i) {
		AN(cli);
		AN(warm);
		VCLI_SetResult(cli, status);
		VCLI_Out(cli, "%s", p);
	} else {
		/* Success, update mgt's VCL state to reflect child's
		   state */
		vp->warm = warm;
		if (vp->warm == 0)
			vp->go_cold = 0;
	}

	free(p);
	return (i);
}

/*--------------------------------------------------------------------*/

static void
mgt_new_vcl(struct cli *cli, const char *vclname, const char *vclsrc,
    const char *vclsrcfile, const char *state, int C_flag)
{
	unsigned status;
	char *lib, *p;
	struct vclprog *vp;
	char buf[32];

	AN(cli);

	if (C_flag) {
		bprintf(buf, ".CflagTest.%d", (int)getpid());
		vclname = buf;
	}

	if (state == NULL)
		state = "auto";

	if (strcmp(state, "auto") &&
	    strcmp(state, "cold") && strcmp(state, "warm")) {
		VCLI_Out(cli, "State must be one of auto, cold or warm.");
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	}

	lib = mgt_VccCompile(cli, vclname, vclsrc, vclsrcfile, C_flag);
	if (lib == NULL)
		return;

	AZ(C_flag);
	vp = mgt_vcl_add(vclname, lib, state);
	free(lib);

	if (active_vcl == NULL)
		active_vcl = vp;

	if (child_pid < 0)
		return;

	if (!mgt_cli_askchild(&status, &p, "vcl.load %s %s %d%s\n",
	    vp->name, vp->fname, vp->warm, vp->state)) {
		if (vp->warm && !strcmp(vp->state, "auto"))
			vp->go_cold = VTIM_mono();
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
mgt_vcc_startup(struct cli *cli, const char *b_arg, const char *f_arg,
    const char *vclsrc, int C_flag)
{
	char buf[BUFSIZ];

	if (b_arg == NULL) {
		AN(vclsrc);
		AN(f_arg);
		mgt_new_vcl(cli, "boot", vclsrc, f_arg, NULL, C_flag);
		return;
	}

	AZ(vclsrc);
	bprintf(buf,
	    "vcl 4.0;\n"
	    "backend default {\n"
	    "    .host = \"%s\";\n"
	    "}\n", b_arg);
	mgt_new_vcl(cli, "boot", buf, "<-b argument>", NULL, C_flag);
}

/*--------------------------------------------------------------------*/

int
mgt_push_vcls_and_start(struct cli *cli, unsigned *status, char **p)
{
	struct vclprog *vp;

	AN(active_vcl);

	/* The VCL has not been loaded yet, it cannot fail */
	AZ(mgt_vcl_setstate(cli, active_vcl, VCL_STATE_WARM));

	VTAILQ_FOREACH(vp, &vclhead, list) {
		if (mgt_cli_askchild(status, p, "vcl.load \"%s\" %s %d%s\n",
		    vp->name, vp->fname, vp->warm, vp->state))
			return (1);
		free(*p);
	}
	if (mgt_cli_askchild(status, p, "vcl.use \"%s\"\n", active_vcl->name))
		return (1);
	free(*p);
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

	mgt_new_vcl(cli, av[2], av[3], "<vcl.inline>", av[4], 0);
}

void
mcf_vcl_load(struct cli *cli, const char * const *av, void *priv)
{
	struct vclprog *vp;

	(void)priv;
	vp = mgt_vcl_byname(av[2]);
	if (vp != NULL) {
		VCLI_Out(cli, "Already a VCL program named %s", av[2]);
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	}

	mgt_new_vcl(cli, av[2], NULL, av[3], av[4], 0);
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
		if (vp != active_vcl) {
			vp->go_cold = VTIM_mono();
			(void)mgt_vcl_setstate(cli, vp, VCL_STATE_AUTO);
		}
	} else if (!strcmp(av[3], "cold")) {
		if (vp == active_vcl) {
			VCLI_Out(cli, "Cannot set the active VCL cold.");
			VCLI_SetResult(cli, CLIS_PARAM);
			return;
		}
		bprintf(vp->state, "%s", "auto");
		(void)mgt_vcl_setstate(cli, vp, VCL_STATE_COLD);
	} else if (!strcmp(av[3], "warm")) {
		if (mgt_vcl_setstate(cli, vp, VCL_STATE_WARM) == 0)
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
	struct vclprog *vp, *vp2;

	(void)priv;
	vp = mcf_find_vcl(cli, av[2]);
	if (vp == NULL)
		return;
	if (vp == active_vcl)
		return;
	if (mgt_vcl_setstate(cli, vp, VCL_STATE_WARM))
		return;
	if (child_pid >= 0 &&
	    mgt_cli_askchild(&status, &p, "vcl.use %s\n", av[2])) {
		VCLI_SetResult(cli, status);
		VCLI_Out(cli, "%s", p);
		vp->go_cold = VTIM_mono();
		(void)mgt_vcl_setstate(cli, vp, VCL_STATE_AUTO);
	} else {
		VCLI_Out(cli, "VCL '%s' now active", av[2]);
		vp2 = active_vcl;
		active_vcl = vp;
		if (vp2 != NULL) {
			vp2->go_cold = VTIM_mono();
			(void)mgt_vcl_setstate(cli, vp2, VCL_STATE_AUTO);
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
	if (vp == NULL)
		return;
	if (vp == active_vcl) {
		VCLI_SetResult(cli, CLIS_PARAM);
		VCLI_Out(cli, "Cannot discard active VCL program\n");
		return;
	}
	(void)mgt_vcl_setstate(cli, vp, VCL_STATE_COLD);
	if (child_pid >= 0) {
		/* XXX If this fails the child is crashing, figure that later */
		(void)mgt_cli_askchild(&status, &p, "vcl.discard %s\n", av[2]);
		free(p);
	}
	mgt_vcl_del(vp);
}

void
mcf_vcl_list(struct cli *cli, const char * const *av, void *priv)
{
	unsigned status;
	char *p;
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
			VCLI_Out(cli, "%-10s %4s/%-8s %6s %s\n",
			    vp == active_vcl ? "active" : "available",
			    vp->state, vp->warm ? "warm" : "cold", "",
			    vp->name);
		}
	}
}

/*--------------------------------------------------------------------*/

static int __match_proto__(vev_cb_f)
mgt_vcl_poker(const struct vev *e, int what)
{
	struct vclprog *vp;

	(void)e;
	(void)what;
	e_poker->timeout = mgt_param.vcl_cooldown * .45;
	VTAILQ_FOREACH(vp, &vclhead, list) {
		if (vp != active_vcl)
			(void)mgt_vcl_setstate(NULL, vp, VCL_STATE_AUTO);
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
	do {
		vp = VTAILQ_FIRST(&vclhead);
		if (vp != NULL)
			mgt_vcl_del(vp);
	} while (vp != NULL);
}

void
mgt_vcl_init(void)
{

	e_poker = vev_new();
	AN(e_poker);
	e_poker->timeout = 3;		// random, prime

	e_poker->callback = mgt_vcl_poker;
	e_poker->name = "vcl poker";
	AZ(vev_add(mgt_evb, e_poker));

	AZ(atexit(mgt_vcl_atexit));
}
