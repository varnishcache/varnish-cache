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
 * Handle configuration of backends from VCL programs.
 *
 */

#include "config.h"

#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache.h"

#include "vcl.h"
#include "vcli.h"
#include "vcli_priv.h"
#include "vrt.h"
#include "vtim.h"

#include "cache_director.h"
#include "cache_backend.h"

static VTAILQ_HEAD(, backend) backends = VTAILQ_HEAD_INITIALIZER(backends);
static struct lock backends_mtx;

/*--------------------------------------------------------------------
 * Create a new static or dynamic director::backend instance.
 */

struct director *
VRT_new_backend(VRT_CTX, const struct vrt_backend *vrt)
{
	struct backend *b;
	char buf[128];
	struct vcl *vcl;
	struct tcp_pool *tp = NULL;
	const struct vrt_backend_probe *vbp;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vrt, VRT_BACKEND_MAGIC);

	assert(vrt->ipv4_suckaddr != NULL || vrt->ipv6_suckaddr != NULL);

	vcl = ctx->vcl;
	AN(vcl);
	AN(vrt->vcl_name);
	assert(vrt->ipv4_suckaddr != NULL || vrt->ipv6_suckaddr != NULL);

	/* Create new backend */
	ALLOC_OBJ(b, BACKEND_MAGIC);
	XXXAN(b);
	Lck_New(&b->mtx, lck_backend);

#define DA(x)	do { if (vrt->x != NULL) REPLACE((b->x), (vrt->x)); } while (0)
#define DN(x)	do { b->x = vrt->x; } while (0)
	VRT_BACKEND_HANDLE();
#undef DA
#undef DN

	bprintf(buf, "%s.%s", VCL_Name(vcl), vrt->vcl_name);
	REPLACE(b->display_name, buf);

	b->vcl = vcl;

	b->healthy = 1;
	b->health_changed = VTIM_real();
	b->admin_health = ah_probe;

	vbp = vrt->probe;
	if (vbp == NULL)
		vbp = VCL_DefaultProbe(vcl);

	Lck_Lock(&backends_mtx);
	VTAILQ_INSERT_TAIL(&backends, b, list);
	VSC_C_main->n_backend++;
	b->tcp_pool = VBT_Ref(vrt->ipv4_suckaddr, vrt->ipv6_suckaddr);
	if (vbp != NULL) {
		tp = VBT_Ref(vrt->ipv4_suckaddr, vrt->ipv6_suckaddr);
		assert(b->tcp_pool == tp);
	}
	Lck_Unlock(&backends_mtx);

	VBE_fill_director(b);

	if (vbp != NULL)
		VBP_Insert(b, vbp, tp);

	VCL_AddBackend(ctx->vcl, b);

	return (b->director);
}

/*--------------------------------------------------------------------
 * Delete a dynamic director::backend instance.  Undeleted dynamic and
 * static instances are GC'ed when the VCL is discarded (in cache_vcl.c)
 */

void
VRT_delete_backend(VRT_CTX, struct director **dp)
{
	struct director *d;
	struct backend *be;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(dp);
	d = *dp;
	*dp = NULL;
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(be, d->priv, BACKEND_MAGIC);
	VCL_DelBackend(ctx->vcl, be);
	VBE_Delete(be);
}

/*---------------------------------------------------------------------
 * These are for cross-calls with cache_vcl.c only.
 */

void
VBE_Event(struct backend *be, enum vcl_event_e ev)
{

	CHECK_OBJ_NOTNULL(be, BACKEND_MAGIC);

	if (ev == VCL_EVENT_WARM) {
		be->vsc = VSM_Alloc(sizeof *be->vsc,
		    VSC_CLASS, VSC_type_vbe, be->display_name);
		AN(be->vsc);
	}

	if (be->probe != NULL && ev == VCL_EVENT_WARM)
		VBP_Control(be, 1);

	if (be->probe != NULL && ev == VCL_EVENT_COLD)
		VBP_Control(be, 0);

	if (ev == VCL_EVENT_COLD) {
		VSM_Free(be->vsc);
		be->vsc = NULL;
	}
}

void
VBE_Delete(struct backend *be)
{
	CHECK_OBJ_NOTNULL(be, BACKEND_MAGIC);

	if (be->probe != NULL)
		VBP_Remove(be);

	Lck_Lock(&backends_mtx);
	VTAILQ_REMOVE(&backends, be, list);
	VSC_C_main->n_backend--;
	VBT_Rel(&be->tcp_pool);
	Lck_Unlock(&backends_mtx);

#define DA(x)	do { if (be->x != NULL) free(be->x); } while (0)
#define DN(x)	/**/
	VRT_BACKEND_HANDLE();
#undef DA
#undef DN

	free(be->display_name);
	AZ(be->vsc);
	Lck_Delete(&be->mtx);
	FREE_OBJ(be);
}

/*---------------------------------------------------------------------
 * String to admin_health
 */

static enum admin_health
vbe_str2adminhealth(const char *wstate)
{

	if (strcasecmp(wstate, "healthy") == 0)
		return (ah_healthy);
	if (strcasecmp(wstate, "sick") == 0)
		return (ah_sick);
	if (strcmp(wstate, "auto") == 0)
		return (ah_probe);
	return (ah_invalid);
}

/*---------------------------------------------------------------------
 * A general function for finding backends and doing things with them.
 *
 * Return -1 on match-argument parse errors.
 *
 * If the call-back function returns negative, the search is terminated
 * and we relay that return value.
 *
 * Otherwise we return the number of matches.
 */

typedef int bf_func(struct cli *cli, struct backend *b, void *priv);

static int
backend_find(struct cli *cli, const char *matcher, bf_func *func, void *priv)
{
	int i, found = 0;
	struct vsb *vsb;
	struct vcl *vcc = NULL;
	struct backend *b;

	VCL_Refresh(&vcc);
	AN(vcc);
	vsb = VSB_new_auto();
	AN(vsb);
	if (matcher == NULL || *matcher == '\0' || !strcmp(matcher, "*")) {
		// all backends in active VCL
		VSB_printf(vsb, "%s.*", VCL_Name(vcc));
	} else if (strchr(matcher, '.') != NULL) {
		// use pattern as is
		VSB_cat(vsb, matcher);
	} else {
		// pattern applies to active vcl
		VSB_printf(vsb, "%s.%s", VCL_Name(vcc), matcher);
	}
	AZ(VSB_finish(vsb));
	Lck_Lock(&backends_mtx);
	VTAILQ_FOREACH(b, &backends, list) {
		if (fnmatch(VSB_data(vsb), b->display_name, 0))
			continue;
		found++;
		i = func(cli, b, priv);
		if (i < 0) {
			found = i;
			break;
		}
	}
	Lck_Unlock(&backends_mtx);
	VSB_delete(vsb);
	VCL_Rel(&vcc);
	return (found);
}

/*---------------------------------------------------------------------*/

static int __match_proto__()
do_list(struct cli *cli, struct backend *b, void *priv)
{
	int *probes;

	AN(priv);
	probes = priv;
	CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);

	VCLI_Out(cli, "\n%-30s", b->display_name);

	if (b->admin_health == ah_probe)
		VCLI_Out(cli, " %-10s", "probe");
	else if (b->admin_health == ah_sick)
		VCLI_Out(cli, " %-10s", "sick");
	else if (b->admin_health == ah_healthy)
		VCLI_Out(cli, " %-10s", "healthy");
	else
		VCLI_Out(cli, " %-10s", "invalid");

	if (b->probe == NULL)
		VCLI_Out(cli, " %s", "Healthy (no probe)");
	else {
		if (b->healthy)
			VCLI_Out(cli, " %s", "Healthy ");
		else
			VCLI_Out(cli, " %s", "Sick ");
		VBP_Status(cli, b, *probes);
	}

	/* XXX: report b->health_changed */

	return (0);
}

static void
cli_backend_list(struct cli *cli, const char * const *av, void *priv)
{
	int probes = 0;

	(void)priv;
	ASSERT_CLI();
	if (av[2] != NULL && !strcmp(av[2], "-p")) {
		av++;
		probes = 1;
	} else if (av[2] != NULL && av[2][0] == '-') {
		VCLI_Out(cli, "Invalid flags %s", av[2]);
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	} else if (av[3] != NULL) {
		VCLI_Out(cli, "Too many arguments");
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	}
	VCLI_Out(cli, "%-30s %-10s %s", "Backend name", "Admin", "Probe");
	(void)backend_find(cli, av[2], do_list, &probes);
}

/*---------------------------------------------------------------------*/

static int __match_proto__()
do_set_health(struct cli *cli, struct backend *b, void *priv)
{
	enum admin_health state;
	unsigned prev;

	(void)cli;
	state = *(enum admin_health*)priv;
	CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);
	prev = VBE_Healthy(b, NULL);
	b->admin_health = state;
	if (prev != VBE_Healthy(b, NULL))
		b->health_changed = VTIM_real();

	return (0);
}

static void
cli_backend_set_health(struct cli *cli, const char * const *av, void *priv)
{
	enum admin_health state;
	int n;

	(void)av;
	(void)priv;
	ASSERT_CLI();
	AN(av[2]);
	AN(av[3]);
	state = vbe_str2adminhealth(av[3]);
	if (state == ah_invalid) {
		VCLI_Out(cli, "Invalid state %s", av[3]);
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	}
	n = backend_find(cli, av[2], do_set_health, &state);
	if (n == 0) {
		VCLI_Out(cli, "No Backends matches");
		VCLI_SetResult(cli, CLIS_PARAM);
	}
}

/*---------------------------------------------------------------------*/

static struct cli_proto backend_cmds[] = {
	{ "backend.list", "backend.list [-p] [<backend_expression>]",
	    "\tList backends.",
	    0, 2, "", cli_backend_list },
	{ "backend.set_health",
	    "backend.set_health <backend_expression> <state>",
	    "\tSet health status on the backends.",
	    2, 2, "", cli_backend_set_health },
	{ NULL }
};

/*---------------------------------------------------------------------*/

void
VBE_InitCfg(void)
{

	CLI_AddFuncs(backend_cmds);
	Lck_New(&backends_mtx, lck_vbe);
}
