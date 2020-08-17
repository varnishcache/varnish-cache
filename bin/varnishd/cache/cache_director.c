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
 * Abstract director API
 *
 * The abstract director API does not know how we talk to the backend or
 * if there even is one in the usual meaning of the word.
 *
 */

#include "config.h"

#include "cache_varnishd.h"

#include "cache_director.h"
#include "cache_backend.h"

#include "vcli_serve.h"
#include "vtim.h"

/* -------------------------------------------------------------------*/

struct vdi_ahealth {
	const char		*name;
};

#define VBE_AHEALTH(l,u)						\
	static const struct vdi_ahealth vdi_ah_##l[1] = {{#l}};		\
	const struct vdi_ahealth * const VDI_AH_##u = vdi_ah_##l;
VBE_AHEALTH_LIST
#undef VBE_AHEALTH

static const struct vdi_ahealth *
vdi_str2ahealth(const char *t)
{
#define VBE_AHEALTH(l,u) if (!strcasecmp(t, #l)) return (VDI_AH_##u);
VBE_AHEALTH_LIST
#undef VBE_AHEALTH
	if (!strcasecmp(t, "auto")) return (VDI_AH_PROBE);
	return (NULL);
}

const char *
VDI_Ahealth(const struct director *d)
{

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	AN(d->admin_health);
	return (d->admin_health->name);
}

/* Resolve director --------------------------------------------------*/

static const struct director *
vdi_resolve(struct worker *wrk, struct busyobj *bo)
{
	const struct director *d;
	const struct director *d2;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	for (d = bo->director_req; d != NULL && d->resolve != NULL; d = d2) {
		CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
		d2 = d->resolve(d, wrk, bo);
		if (d2 == NULL)
			VSLb(bo->vsl, SLT_FetchError,
			    "Director %s returned no backend", d->vcl_name);
	}
	CHECK_OBJ_ORNULL(d, DIRECTOR_MAGIC);
	if (d == NULL)
		VSLb(bo->vsl, SLT_FetchError, "No backend");
	bo->director_resp = d;
	return (d);
}

/* Get a set of response headers -------------------------------------*/

int
VDI_GetHdr(struct worker *wrk, struct busyobj *bo)
{
	const struct director *d;
	int i = -1;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	d = vdi_resolve(wrk, bo);
	if (d != NULL) {
		AN(d->gethdrs);
		bo->director_state = DIR_S_HDRS;
		i = d->gethdrs(d, wrk, bo);
	}
	if (i)
		bo->director_state = DIR_S_NULL;
	return (i);
}

/* Setup body fetch --------------------------------------------------*/

int
VDI_GetBody(struct worker *wrk, struct busyobj *bo)
{
	const struct director *d;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	d = bo->director_resp;
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	AZ(d->resolve);

	assert(bo->director_state == DIR_S_HDRS);
	bo->director_state = DIR_S_BODY;
	if (d->getbody == NULL)
		return (0);
	return (d->getbody(d, wrk, bo));
}

/* Get IP number (if any ) -------------------------------------------*/

const struct suckaddr *
VDI_GetIP(struct worker *wrk, struct busyobj *bo)
{
	const struct director *d;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	d = bo->director_resp;
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	assert(bo->director_state == DIR_S_HDRS ||
	   bo->director_state == DIR_S_BODY);
	AZ(d->resolve);
	if (d->getip == NULL)
		return (NULL);
	return (d->getip(d, wrk, bo));
}

/* Finish fetch ------------------------------------------------------*/

void
VDI_Finish(struct worker *wrk, struct busyobj *bo)
{
	const struct director *d;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	d = bo->director_resp;
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);

	AZ(d->resolve);
	AN(d->finish);

	assert(bo->director_state != DIR_S_NULL);
	d->finish(d, wrk, bo);
	bo->director_state = DIR_S_NULL;
}

/* Get a connection --------------------------------------------------*/

enum sess_close
VDI_Http1Pipe(struct req *req, struct busyobj *bo)
{
	const struct director *d;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	req->res_mode = RES_PIPE;

	d = vdi_resolve(req->wrk, bo);
	if (d == NULL || d->http1pipe == NULL) {
		req->res_mode = 0;
		VSLb(bo->vsl, SLT_VCL_Error, "Backend does not support pipe");
		return (SC_TX_ERROR);
	}
	return (d->http1pipe(d, req, bo));
}

/* Check health --------------------------------------------------------
 *
 * If director has no healthy method, we just assume it is healthy.
 */

int
VRT_Healthy(VRT_CTX, VCL_BACKEND d)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (d == NULL)
		return (0);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	if (!VDI_Healthy(d, NULL))
		return (0);
	if (d->healthy == NULL)
		return (1);
	return (d->healthy(d, ctx->bo, NULL));
}

/* Send Event ----------------------------------------------------------
 */

void
VDI_Event(const struct director *d, enum vcl_event_e ev)
{

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	if (d->event != NULL)
		d->event(d, ev);
}

/* Dump panic info -----------------------------------------------------
 */

void
VDI_Panic(const struct director *d, struct vsb *vsb, const char *nm)
{
	if (d == NULL)
		return;
	VSB_printf(vsb, "%s = %p {\n", nm, d);
	VSB_indent(vsb, 2);
	VSB_printf(vsb, "vcl_name = %s,\n", d->vcl_name);
	VSB_printf(vsb, "health = %s,\n", d->health ?  "healthy" : "sick");
	VSB_printf(vsb, "admin_health = %s, changed = %f,\n",
	    VDI_Ahealth(d), d->health_changed);
	VSB_printf(vsb, "type = %s {\n", d->name);
	VSB_indent(vsb, 2);
	if (d->panic != NULL)
		d->panic(d, vsb);
	VSB_indent(vsb, -2);
	VSB_printf(vsb, "},\n");
	VSB_indent(vsb, -2);
	VSB_printf(vsb, "},\n");
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

	if (d->admin_health == VDI_AH_PROBE)
		return (d->health);

	if (d->admin_health == VDI_AH_SICK)
		return (0);

	if (d->admin_health == VDI_AH_DELETED)
		return (0);

	if (d->admin_health == VDI_AH_HEALTHY)
		return (1);

	WRONG("Wrong admin health");
}

/*---------------------------------------------------------------------*/

static int v_matchproto_(vcl_be_func)
do_list(struct cli *cli, struct director *d, void *priv)
{
	int *probes;
	char time_str[VTIM_FORMAT_SIZE];
	struct backend *be;

	AN(priv);
	probes = priv;
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(be, d->priv, BACKEND_MAGIC);
	if (d->admin_health == VDI_AH_DELETED)
		return (0);

	VCLI_Out(cli, "\n%-30s", d->display_name);

	VCLI_Out(cli, " %-10s", VDI_Ahealth(d));

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

static void v_matchproto_(cli_func_t)
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

static int v_matchproto_(vcl_be_func)
do_set_health(struct cli *cli, struct director *d, void *priv)
{
	unsigned prev;

	(void)cli;
	AN(priv);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	if (d->admin_health == VDI_AH_DELETED)
		return (0);
	prev = VDI_Healthy(d, NULL);
	d->admin_health = *(const struct vdi_ahealth **)priv;
	(void)VDI_Ahealth(d);			// Acts like type-check
	if (prev != VDI_Healthy(d, NULL))
		d->health_changed = VTIM_real();

	return (0);
}

static void v_matchproto_()
cli_backend_set_health(struct cli *cli, const char * const *av, void *priv)
{
	const struct vdi_ahealth *ah;
	int n;

	(void)av;
	(void)priv;
	ASSERT_CLI();
	AN(av[2]);
	AN(av[3]);
	ah = vdi_str2ahealth(av[3]);
	if (ah == NULL || ah == VDI_AH_DELETED) {
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
VDI_Init(void)
{

	CLI_AddFuncs(backend_cmds);
}
