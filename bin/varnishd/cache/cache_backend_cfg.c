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

#include "cache_varnishd.h"

#include "vcli_serve.h"
#include "vtim.h"
#include "waiter/waiter.h"

#include "cache_director.h"
#include "cache_backend.h"
#include "cache_tcp_pool.h"

#include "VSC_vbe.h"

static const char * const vbe_proto_ident = "HTTP Backend";

static VTAILQ_HEAD(, backend) backends = VTAILQ_HEAD_INITIALIZER(backends);
static VTAILQ_HEAD(, backend) cool_backends =
    VTAILQ_HEAD_INITIALIZER(cool_backends);
static struct lock backends_mtx;

/*--------------------------------------------------------------------*/

#define AHEALTH_LIST		\
	AHEALTH(healthy)	\
	AHEALTH(sick)		\
	AHEALTH(probe)		\
	AHEALTH(deleted)

struct vbe_ahealth {
	const char		*state;
};

#define AHEALTH(x) static const struct vbe_ahealth vbe_ah_##x[1] = {{ #x }};
AHEALTH_LIST
#undef AHEALTH

/*---------------------------------------------------------------------
 * String to admin_health
 */

static const struct vbe_ahealth *
vbe_str2adminhealth(const char *wstate)
{

#define FOO(x, y) if (strcasecmp(wstate, #x) == 0) return (vbe_ah_##y)
	FOO(healthy,	healthy);
	FOO(sick,	sick);
	FOO(probe,	probe);
	FOO(auto,	probe);
	return (NULL);
#undef FOO
}

const char *
VBE_AdminHealth(const struct vbe_ahealth *ah)
{
#define AHEALTH(x) if (ah == vbe_ah_##x) return(ah->state);
AHEALTH_LIST
#undef AHEALTH
	WRONG("Wrong Admin Health State");
}

#undef AHEALTH_LIST

/*--------------------------------------------------------------------
 * Create a new static or dynamic director::backend instance.
 */

struct director *
VRT_new_backend(VRT_CTX, const struct vrt_backend *vrt)
{
	struct backend *b;
	struct director *d;
	struct vcl *vcl;
	const struct vrt_backend_probe *vbp;
	int retval;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vrt, VRT_BACKEND_MAGIC);
	assert(vrt->ipv4_suckaddr != NULL || vrt->ipv6_suckaddr != NULL);

	vcl = ctx->vcl;
	AN(vcl);
	AN(vrt->vcl_name);

	/* Create new backend */
	ALLOC_OBJ(b, BACKEND_MAGIC);
	XXXAN(b);
	Lck_New(&b->mtx, lck_backend);

#define DA(x)	do { if (vrt->x != NULL) REPLACE((b->x), (vrt->x)); } while (0)
#define DN(x)	do { b->x = vrt->x; } while (0)
	VRT_BACKEND_HANDLE();
#undef DA
#undef DN

	VBE_fill_director(b);

	b->director->health = 1;
	b->director->health_changed = VTIM_real();
	b->director->admin_health = vbe_ah_probe;

	vbp = vrt->probe;
	if (vbp == NULL)
		vbp = VCL_DefaultProbe(vcl);

	Lck_Lock(&backends_mtx);
	VTAILQ_INSERT_TAIL(&backends, b, list);
	VSC_C_main->n_backend++;
	b->tcp_pool = VTP_Ref(vrt->ipv4_suckaddr, vrt->ipv6_suckaddr,
	    vbe_proto_ident);
	Lck_Unlock(&backends_mtx);

	if (vbp != NULL) {
		VTP_AddRef(b->tcp_pool);
		VBP_Insert(b, vbp, b->tcp_pool);
	}

	retval = VCL_AddDirector(ctx->vcl, b->director, vrt->vcl_name);

	if (retval == 0)
		return (b->director);

	d = b->director;
	VRT_delete_backend(ctx, &d);
	AZ(d);
	return (NULL);
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
	TAKE_OBJ_NOTNULL(d, dp, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(be, d->priv, BACKEND_MAGIC);
	Lck_Lock(&be->mtx);
	be->director->admin_health = vbe_ah_deleted;
	be->director->health_changed = VTIM_real();
	be->cooled = VTIM_real() + 60.;
	Lck_Unlock(&be->mtx);
	Lck_Lock(&backends_mtx);
	VTAILQ_REMOVE(&backends, be, list);
	VTAILQ_INSERT_TAIL(&cool_backends, be, list);
	Lck_Unlock(&backends_mtx);

	// NB. The backend is still usable for the ongoing transactions,
	// this is why we don't bust the director's magic number.
}

void
VBE_SetHappy(const struct backend *be, uint64_t happy)
{

	Lck_Lock(&backends_mtx);
	if (be->vsc != NULL)
		be->vsc->happy = happy;
	Lck_Unlock(&backends_mtx);
}

/*---------------------------------------------------------------------
 * These are for cross-calls with cache_vcl.c only.
 */

void
VBE_Delete(const struct director *d)
{
	struct backend *be;

	ASSERT_CLI();
	CAST_OBJ_NOTNULL(be, d->priv, BACKEND_MAGIC);

	if (be->probe != NULL)
		VBP_Remove(be);

	Lck_Lock(&backends_mtx);
	if (be->cooled > 0)
		VTAILQ_REMOVE(&cool_backends, be, list);
	else
		VTAILQ_REMOVE(&backends, be, list);
	VSC_C_main->n_backend--;
	VTP_Rel(&be->tcp_pool);
	Lck_Unlock(&backends_mtx);

#define DA(x)	do { if (be->x != NULL) free(be->x); } while (0)
#define DN(x)	/**/
	VRT_BACKEND_HANDLE();
#undef DA
#undef DN

	AZ(be->vsc);
	Lck_Delete(&be->mtx);
	FREE_OBJ(be);
}

/*--------------------------------------------------------------------
 * Test if backend is healthy and report when it last changed
 */

unsigned
VDI_Healthy(const struct director *d, double *changed)
{
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);

	if (changed != NULL)
		*changed = d->health_changed;

	if (d->admin_health == vbe_ah_probe)
		return (d->health);

	if (d->admin_health == vbe_ah_sick)
		return (0);

	if (d->admin_health == vbe_ah_deleted)
		return (0);

	if (d->admin_health == vbe_ah_healthy)
		return (1);

	WRONG("Wrong admin health");
}

/*---------------------------------------------------------------------*/

static int __match_proto__(vcl_be_func)
do_list(struct cli *cli, struct director *d, void *priv)
{
	int *probes;
	char time_str[VTIM_FORMAT_SIZE];
	struct backend *be;

	AN(priv);
	probes = priv;
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(be, d->priv, BACKEND_MAGIC);
	if (d->admin_health == vbe_ah_deleted)
		return (0);

	VCLI_Out(cli, "\n%-30s", d->display_name);

	VCLI_Out(cli, " %-10s", VBE_AdminHealth(d->admin_health));

	if (be->probe == NULL)
		VCLI_Out(cli, " %-20s", "Healthy (no probe)");
	else {
		if (d->health)
			VCLI_Out(cli, " %-20s", "Healthy ");
		else
			VCLI_Out(cli, " %-20s", "Sick ");
		VBP_Status(cli, be, *probes);
	}

	VTIM_format(d->health_changed, time_str);
	VCLI_Out(cli, " %s", time_str);

	return (0);
}

static void __match_proto__(cli_func_t)
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
	VCLI_Out(cli, "%-30s %-10s %-20s %s", "Backend name", "Admin",
	    "Probe", "Last updated");
	(void)VCL_IterDirector(cli, av[2], do_list, &probes);
}

/*---------------------------------------------------------------------*/

static int __match_proto__(vcl_be_func)
do_set_health(struct cli *cli, struct director *d, void *priv)
{
	const struct vbe_ahealth *ah;
	unsigned prev;

	(void)cli;
	AN(priv);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	if (d->admin_health == vbe_ah_deleted)
		return (0);
	ah = *(const struct vbe_ahealth **)priv;
	AN(ah);
	prev = VDI_Healthy(d, NULL);
	if (d->admin_health != vbe_ah_deleted)
		d->admin_health = ah;
	if (prev != VDI_Healthy(d, NULL))
		d->health_changed = VTIM_real();

	return (0);
}

static void __match_proto__()
cli_backend_set_health(struct cli *cli, const char * const *av, void *priv)
{
	const struct vbe_ahealth *ah;
	int n;

	(void)av;
	(void)priv;
	ASSERT_CLI();
	AN(av[2]);
	AN(av[3]);
	ah = vbe_str2adminhealth(av[3]);
	if (ah == NULL) {
		VCLI_Out(cli, "Invalid state %s", av[3]);
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	}
	n = VCL_IterDirector(cli, av[2], do_set_health, &ah);
	if (n == 0) {
		VCLI_Out(cli, "No Backends matches");
		VCLI_SetResult(cli, CLIS_PARAM);
	}
}

/*---------------------------------------------------------------------*/

static struct cli_proto backend_cmds[] = {
	{ CLICMD_BACKEND_LIST,		"", cli_backend_list },
	{ CLICMD_BACKEND_SET_HEALTH,	"", cli_backend_set_health },
	{ NULL }
};

/*---------------------------------------------------------------------*/

void
VBE_Poll(void)
{
	struct backend *be, *be2;
	double now = VTIM_real();

	ASSERT_CLI();
	Lck_Lock(&backends_mtx);
	VTAILQ_FOREACH_SAFE(be, &cool_backends, list, be2) {
		CHECK_OBJ_NOTNULL(be, BACKEND_MAGIC);
		if (be->cooled > now)
			break;
		if (be->n_conn > 0)
			continue;
		Lck_Unlock(&backends_mtx);
		VCL_DelDirector(be->director);
		Lck_Lock(&backends_mtx);
	}
	Lck_Unlock(&backends_mtx);
}

/*---------------------------------------------------------------------*/

void
VBE_InitCfg(void)
{

	CLI_AddFuncs(backend_cmds);
	Lck_New(&backends_mtx, lck_vbe);
}
