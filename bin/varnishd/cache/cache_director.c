/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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

#include "vcli_serve.h"
#include "vte.h"
#include "vtim.h"

/* -------------------------------------------------------------------*/

struct vdi_ahealth {
	const char		*name;
	int			health;
};

#define VBE_AHEALTH(l,u,h)						\
	static const struct vdi_ahealth vdi_ah_##l[1] = {{#l,h}};	\
	const struct vdi_ahealth * const VDI_AH_##u = vdi_ah_##l;
VBE_AHEALTH_LIST
#undef VBE_AHEALTH

static const struct vdi_ahealth *
vdi_str2ahealth(const char *t)
{
#define VBE_AHEALTH(l,u,h) if (!strcasecmp(t, #l)) return (VDI_AH_##u);
VBE_AHEALTH_LIST
#undef VBE_AHEALTH
	return (NULL);
}

static const char *
VDI_Ahealth(const struct director *d)
{

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	AN(d->vdir->admin_health);
	if (d->vdir->admin_health != VDI_AH_AUTO)
		return (d->vdir->admin_health->name);
	if (d->vdir->methods->healthy != NULL)
		return ("probe");
	return ("healthy");
}

/* Resolve director --------------------------------------------------*/

VCL_BACKEND
VRT_DirectorResolve(VRT_CTX, VCL_BACKEND d)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	while (d != NULL) {
		CHECK_OBJ(d, DIRECTOR_MAGIC);
		AN(d->vdir);
		if (d->vdir->methods->resolve == NULL)
			break;
		d = d->vdir->methods->resolve(ctx, d);
	}
	if (d == NULL)
		return (NULL);

	CHECK_OBJ(d, DIRECTOR_MAGIC);
	AN(d->vdir);
	return (d);
}

static VCL_BACKEND
VDI_Resolve(VRT_CTX)
{
	VCL_BACKEND d;
	struct busyobj *bo;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	bo = ctx->bo;
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	if (bo->director_req == NULL) {
		VSLb(bo->vsl, SLT_FetchError, "No backend");
		return (NULL);
	}

	CHECK_OBJ(bo->director_req, DIRECTOR_MAGIC);
	d = VRT_DirectorResolve(ctx, bo->director_req);
	if (d != NULL)
		return (d);

	VSLb(bo->vsl, SLT_FetchError,
	    "Director %s returned no backend", bo->director_req->vcl_name);
	return (NULL);
}

/* Get a set of response headers -------------------------------------*/

int
VDI_GetHdr(struct busyobj *bo)
{
	const struct director *d;
	struct vrt_ctx ctx[1];
	int i = -1;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	INIT_OBJ(ctx, VRT_CTX_MAGIC);
	VCL_Bo2Ctx(ctx, bo);

	d = VDI_Resolve(ctx);
	if (d != NULL) {
		VRT_Assign_Backend(&bo->director_resp, d);
		AN(d->vdir->methods->gethdrs);
		bo->director_state = DIR_S_HDRS;
		i = d->vdir->methods->gethdrs(ctx, d);
	}
	if (i)
		bo->director_state = DIR_S_NULL;
	return (i);
}

/* Get IP number (if any ) -------------------------------------------*/

VCL_IP
VDI_GetIP(struct busyobj *bo)
{
	const struct director *d;
	struct vrt_ctx ctx[1];

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	INIT_OBJ(ctx, VRT_CTX_MAGIC);
	VCL_Bo2Ctx(ctx, bo);

	d = bo->director_resp;
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	assert(bo->director_state == DIR_S_HDRS ||
	   bo->director_state == DIR_S_BODY);
	AZ(d->vdir->methods->resolve);
	if (d->vdir->methods->getip == NULL)
		return (NULL);
	return (d->vdir->methods->getip(ctx, d));
}

/* Finish fetch ------------------------------------------------------*/

void
VDI_Finish(struct busyobj *bo)
{
	const struct director *d;
	struct vrt_ctx ctx[1];

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	INIT_OBJ(ctx, VRT_CTX_MAGIC);
	VCL_Bo2Ctx(ctx, bo);

	d = bo->director_resp;
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);

	AZ(d->vdir->methods->resolve);
	AN(d->vdir->methods->finish);

	assert(bo->director_state != DIR_S_NULL);
	d->vdir->methods->finish(ctx, d);
	bo->director_state = DIR_S_NULL;
}

/* Get a connection --------------------------------------------------*/

stream_close_t
VDI_Http1Pipe(struct req *req, struct busyobj *bo)
{
	const struct director *d;
	struct vrt_ctx ctx[1];

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	INIT_OBJ(ctx, VRT_CTX_MAGIC);
	VCL_Bo2Ctx(ctx, bo);
	VCL_Req2Ctx(ctx, req);

	d = VDI_Resolve(ctx);
	if (d == NULL || d->vdir->methods->http1pipe == NULL) {
		VSLb(bo->vsl, SLT_VCL_Error, "Backend does not support pipe");
		return (SC_TX_ERROR);
	}
	VRT_Assign_Backend(&bo->director_resp, d);
	return (d->vdir->methods->http1pipe(ctx, d));
}

/* Check health --------------------------------------------------------
 *
 * If director has no healthy method, we just assume it is healthy.
 */

/*--------------------------------------------------------------------
 * Test if backend is healthy and report when that last changed
 */

VCL_BOOL
VRT_Healthy(VRT_CTX, VCL_BACKEND d, VCL_TIME *changed)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (d == NULL)
		return (0);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);

	if (d->vdir->admin_health->health >= 0) {
		if (changed != NULL)
			*changed = d->vdir->health_changed;
		return (d->vdir->admin_health->health);
	}

	if (d->vdir->methods->healthy == NULL) {
		if (changed != NULL)
			*changed = d->vdir->health_changed;
		return (1);
	}

	return (d->vdir->methods->healthy(ctx, d, changed));
}

/*--------------------------------------------------------------------
 * Update health_changed.
 */
VCL_VOID
VRT_SetChanged(VCL_BACKEND d, VCL_TIME changed)
{

	CHECK_OBJ_ORNULL(d, DIRECTOR_MAGIC);

	if (d != NULL && changed > d->vdir->health_changed)
		d->vdir->health_changed = changed;
}

/* Send Event ----------------------------------------------------------
 */

void
VDI_Event(const struct director *d, enum vcl_event_e ev)
{

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	if (d->vdir->methods->event != NULL)
		d->vdir->methods->event(d, ev);
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
	VSB_printf(vsb, "cli_name = %s,\n", d->vdir->cli_name);
	VSB_printf(vsb, "admin_health = %s, changed = %f,\n",
	    VDI_Ahealth(d), d->vdir->health_changed);
	VSB_printf(vsb, "type = %s {\n", d->vdir->methods->type);
	VSB_indent(vsb, 2);
	if (d->vdir->methods->panic != NULL)
		d->vdir->methods->panic(d, vsb);
	VSB_indent(vsb, -2);
	VSB_cat(vsb, "},\n");
	VSB_indent(vsb, -2);
	VSB_cat(vsb, "},\n");
}


/*---------------------------------------------------------------------*/

struct list_args {
	unsigned	magic;
#define LIST_ARGS_MAGIC	0x7e7cefeb
	int		p;
	int		j;
	const char	*jsep;
	struct vsb	*vsb;
	struct vte	*vte;
};

static const char *
cli_health(VRT_CTX, const struct director *d)
{
	VCL_BOOL healthy = VRT_Healthy(ctx, d, NULL);

	return (healthy ? "healthy" : "sick");
}

static int v_matchproto_(vcl_be_func)
do_list(struct cli *cli, struct director *d, void *priv)
{
	char time_str[VTIM_FORMAT_SIZE];
	struct list_args *la;
	struct vrt_ctx *ctx;

	AN(cli);
	CAST_OBJ_NOTNULL(la, priv, LIST_ARGS_MAGIC);
	AN(la->vsb);
	AN(la->vte);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);

	if (d->vdir->admin_health == VDI_AH_DELETED)
		return (0);

	ctx = VCL_Get_CliCtx(0);

	VTE_printf(la->vte, "%s\t%s\t", d->vdir->cli_name, VDI_Ahealth(d));

	if (d->vdir->methods->list != NULL) {
		VSB_clear(la->vsb);
		d->vdir->methods->list(ctx, d, la->vsb, 0, 0);
		AZ(VSB_finish(la->vsb));
		VTE_cat(la->vte, VSB_data(la->vsb));
	} else if (d->vdir->methods->healthy != NULL)
		VTE_printf(la->vte, "0/0\t%s", cli_health(ctx, d));
	else
		VTE_cat(la->vte, "0/0\thealthy");

	VTIM_format(d->vdir->health_changed, time_str);
	VTE_printf(la->vte, "\t%s", time_str);
	if (la->p && d->vdir->methods->list != NULL) {
		VSB_clear(la->vsb);
		d->vdir->methods->list(ctx, d, la->vsb, la->p, 0);
		AZ(VSB_finish(la->vsb));
		VTE_cat(la->vte, VSB_data(la->vsb));
	}

	VTE_cat(la->vte, "\n");
	AZ(VCL_Rel_CliCtx(&ctx));
	AZ(ctx);

	return (0);
}

static int v_matchproto_(vcl_be_func)
do_list_json(struct cli *cli, struct director *d, void *priv)
{
	struct list_args *la;
	struct vrt_ctx *ctx;

	CAST_OBJ_NOTNULL(la, priv, LIST_ARGS_MAGIC);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);

	if (d->vdir->admin_health == VDI_AH_DELETED)
		return (0);

	ctx = VCL_Get_CliCtx(0);

	VCLI_Out(cli, "%s", la->jsep);
	la->jsep = ",\n";
	VCLI_JSON_str(cli, d->vdir->cli_name);
	VCLI_Out(cli, ": {\n");
	VSB_indent(cli->sb, 2);
	VCLI_Out(cli, "\"type\": \"%s\",\n", d->vdir->methods->type);
	VCLI_Out(cli, "\"admin_health\": \"%s\",\n", VDI_Ahealth(d));
	VCLI_Out(cli, "\"probe_message\": ");
	if (d->vdir->methods->list != NULL)
		d->vdir->methods->list(ctx, d, cli->sb, 0, 1);
	else if (d->vdir->methods->healthy != NULL)
		VCLI_Out(cli, "[0, 0, \"%s\"]", cli_health(ctx, d));
	else
		VCLI_Out(cli, "[0, 0, \"healthy\"]");

	VCLI_Out(cli, ",\n");

	if (la->p && d->vdir->methods->list != NULL) {
		VCLI_Out(cli, "\"probe_details\": ");
		d->vdir->methods->list(ctx, d, cli->sb, la->p, 1);
	}
	VCLI_Out(cli, "\"last_change\": %.3f\n", d->vdir->health_changed);
	VSB_indent(cli->sb, -2);
	VCLI_Out(cli, "}");

	AZ(VCL_Rel_CliCtx(&ctx));
	AZ(ctx);

	return (0);
}

static void v_matchproto_(cli_func_t)
cli_backend_list(struct cli *cli, const char * const *av, void *priv)
{
	const char *p;
	struct list_args la[1];
	int i;

	(void)priv;
	ASSERT_CLI();
	INIT_OBJ(la, LIST_ARGS_MAGIC);
	la->jsep = "";
	for (i = 2; av[i] != NULL && av[i][0] == '-'; i++) {
		for(p = av[i] + 1; *p; p++) {
			switch(*p) {
			case 'j': la->j = 1; break;
			case 'p': la->p = !la->p; break;
			default:
				VCLI_Out(cli, "Invalid flag %c", *p);
				VCLI_SetResult(cli, CLIS_PARAM);
				return;
			}
		}
	}
	if (av[i] != NULL && av[i+1] != NULL) {
		VCLI_Out(cli, "Too many arguments");
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	}
	if (la->j) {
		VCLI_JSON_begin(cli, 3, av);
		VCLI_Out(cli, ",\n");
		VCLI_Out(cli, "{\n");
		VSB_indent(cli->sb, 2);
		(void)VCL_IterDirector(cli, av[i], do_list_json, la);
		VSB_indent(cli->sb, -2);
		VCLI_Out(cli, "\n");
		VCLI_Out(cli, "}");
		VCLI_JSON_end(cli);
	} else {
		la->vsb = VSB_new_auto();
		AN(la->vsb);
		la->vte = VTE_new(5, 80);
		AN(la->vte);
		VTE_printf(la->vte, "%s\t%s\t%s\t%s\t%s\n",
		    "Backend name", "Admin", "Probe", "Health", "Last change");
		(void)VCL_IterDirector(cli, av[i], do_list, la);
		AZ(VTE_finish(la->vte));
		AZ(VTE_format(la->vte, VCLI_VTE_format, cli));
		VTE_destroy(&la->vte);
		VSB_destroy(&la->vsb);
	}
}

/*---------------------------------------------------------------------*/

struct set_health {
	unsigned			magic;
#define SET_HEALTH_MAGIC		0x0c46b9fb
	const struct vdi_ahealth	*ah;
};

static int v_matchproto_(vcl_be_func)
do_set_health(struct cli *cli, struct director *d, void *priv)
{
	struct set_health *sh;
	struct vrt_ctx *ctx;

	(void)cli;
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(sh, priv, SET_HEALTH_MAGIC);

	if (d->vdir->admin_health == VDI_AH_DELETED)
		return (0);
	if (d->vdir->admin_health != sh->ah) {
		d->vdir->health_changed = VTIM_real();
		d->vdir->admin_health = sh->ah;
		ctx = VCL_Get_CliCtx(0);
		if (sh->ah == VDI_AH_SICK || (sh->ah == VDI_AH_AUTO &&
		    d->vdir->methods->healthy != NULL &&
		    !d->vdir->methods->healthy(ctx, d, NULL))) {
			VBE_connwait_signal_all(d->priv);
		    }
	}
	return (0);
}

static void v_matchproto_()
cli_backend_set_health(struct cli *cli, const char * const *av, void *priv)
{
	int n;
	struct set_health sh[1];

	(void)av;
	(void)priv;
	ASSERT_CLI();
	AN(av[2]);
	AN(av[3]);
	INIT_OBJ(sh, SET_HEALTH_MAGIC);
	sh->ah = vdi_str2ahealth(av[3]);
	if (sh->ah == NULL || sh->ah == VDI_AH_DELETED) {
		VCLI_Out(cli, "Invalid state %s", av[3]);
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	}
	n = VCL_IterDirector(cli, av[2], do_set_health, sh);
	if (n == 0) {
		VCLI_Out(cli, "No Backends matches");
		VCLI_SetResult(cli, CLIS_PARAM);
	}
}

/*---------------------------------------------------------------------*/

static struct cli_proto backend_cmds[] = {
	{ CLICMD_BACKEND_LIST,		"",
	     cli_backend_list, cli_backend_list },
	{ CLICMD_BACKEND_SET_HEALTH,	"", cli_backend_set_health },
	{ NULL }
};

/*---------------------------------------------------------------------*/

void
VDI_Init(void)
{

	CLI_AddFuncs(backend_cmds);
}
