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

#include "vcli_serve.h"
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
	if (!strcasecmp(t, "auto")) return (VDI_AH_PROBE);
	return (NULL);
}

static const char *
VDI_Ahealth(const struct director *d)
{

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	AN(d->vdir->admin_health);
	return (d->vdir->admin_health->name);
}

/* Resolve director --------------------------------------------------*/

static VCL_BACKEND
VDI_Resolve(VRT_CTX)
{
	const struct director *d;
	const struct director *d2;
	struct busyobj *bo;

	bo = ctx->bo;
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_ORNULL(bo->director_req, DIRECTOR_MAGIC);

	for (d = bo->director_req; d != NULL &&
	    d->vdir->methods->resolve != NULL; d = d2) {
		CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
		AN(d->vdir);
		d2 = d->vdir->methods->resolve(ctx, d);
		if (d2 == NULL)
			VSLb(bo->vsl, SLT_FetchError,
			    "Director %s returned no backend", d->vcl_name);
	}
	CHECK_OBJ_ORNULL(d, DIRECTOR_MAGIC);
	if (d == NULL)
		VSLb(bo->vsl, SLT_FetchError, "No backend");
	else
		AN(d->vdir);
	return (d);
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
		bo->director_resp = d;
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

enum sess_close
VDI_Http1Pipe(struct req *req, struct busyobj *bo)
{
	const struct director *d;
	struct vrt_ctx ctx[1];

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	INIT_OBJ(ctx, VRT_CTX_MAGIC);
	VCL_Req2Ctx(ctx, req);
	VCL_Bo2Ctx(ctx, bo);

	d = VDI_Resolve(ctx);
	if (d == NULL || d->vdir->methods->http1pipe == NULL) {
		VSLb(bo->vsl, SLT_VCL_Error, "Backend does not support pipe");
		return (SC_TX_ERROR);
	}
	bo->director_resp = d;
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
		return (!d->sick);
	}

	return (d->vdir->methods->healthy(ctx, d, changed));
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
	VSB_printf(vsb, "health = %s,\n", d->sick ?  "sick" : "healthy");
	VSB_printf(vsb, "admin_health = %s, changed = %f,\n",
	    VDI_Ahealth(d), d->vdir->health_changed);
	VSB_printf(vsb, "type = %s {\n", d->vdir->methods->type);
	VSB_indent(vsb, 2);
	if (d->vdir->methods->panic != NULL)
		d->vdir->methods->panic(d, vsb);
	VSB_indent(vsb, -2);
	VSB_printf(vsb, "},\n");
	VSB_indent(vsb, -2);
	VSB_printf(vsb, "},\n");
}


/*---------------------------------------------------------------------*/

struct list_args {
	unsigned	magic;
#define LIST_ARGS_MAGIC	0x7e7cefeb
	int		p;
	int		v;
	int		j;
	const char	*jsep;
};

static int v_matchproto_(vcl_be_func)
do_list(struct cli *cli, struct director *d, void *priv)
{
	char time_str[VTIM_FORMAT_SIZE];
	struct list_args *la;

	CAST_OBJ_NOTNULL(la, priv, LIST_ARGS_MAGIC);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);

	if (d->vdir->admin_health == VDI_AH_DELETED)
		return (0);

	// XXX admin health "probe" for the no-probe case is confusing
	VCLI_Out(cli, "\n%-30s %-7s ", d->vdir->cli_name, VDI_Ahealth(d));

	if (d->vdir->methods->list != NULL)
		d->vdir->methods->list(d, cli->sb, 0, 0, 0);
	else
		VCLI_Out(cli, "%-10s", d->sick ? "sick" : "healthy");

	VTIM_format(d->vdir->health_changed, time_str);
	VCLI_Out(cli, " %s", time_str);
	if ((la->p || la->v) && d->vdir->methods->list != NULL)
		d->vdir->methods->list(d, cli->sb, la->p, la->v, 0);
	return (0);
}

static int v_matchproto_(vcl_be_func)
do_list_json(struct cli *cli, struct director *d, void *priv)
{
	struct list_args *la;

	CAST_OBJ_NOTNULL(la, priv, LIST_ARGS_MAGIC);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);

	if (d->vdir->admin_health == VDI_AH_DELETED)
		return (0);

	VCLI_Out(cli, "%s", la->jsep);
	la->jsep = ",\n";
	// XXX admin health "probe" for the no-probe case is confusing
	VCLI_JSON_str(cli, d->vdir->cli_name);
	VCLI_Out(cli, ": {\n");
	VSB_indent(cli->sb, 2);
	VCLI_Out(cli, "\"type\": \"%s\",\n", d->vdir->methods->type);
	VCLI_Out(cli, "\"admin_health\": \"%s\",\n", VDI_Ahealth(d));
	VCLI_Out(cli, "\"probe_message\": ");
	if (d->vdir->methods->list != NULL)
		d->vdir->methods->list(d, cli->sb, 0, 0, 1);
	else
		VCLI_Out(cli, "\"%s\"", d->sick ? "sick" : "healthy");
	VCLI_Out(cli, ",\n");

	if ((la->p || la->v) && d->vdir->methods->list != NULL) {
		VCLI_Out(cli, "\"probe_details\": ");
		d->vdir->methods->list(d, cli->sb, la->p, la->v, 1);
	}
	VCLI_Out(cli, "\"last_change\": %.3f\n", d->vdir->health_changed);
	VSB_indent(cli->sb, -2);
	VCLI_Out(cli, "}");
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
			case 'v': la->p = !la->p; break;
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
		VCLI_JSON_begin(cli, 2, av);
		VCLI_Out(cli, ",\n");
		VCLI_Out(cli, "{\n");
		VSB_indent(cli->sb, 2);
		(void)VCL_IterDirector(cli, av[i], do_list_json, la);
		VSB_indent(cli->sb, -2);
		VCLI_Out(cli, "\n");
		VCLI_Out(cli, "}");
		VCLI_JSON_end(cli);
	} else {
		VCLI_Out(cli, "%-30s %-7s %-10s %s",
		    "Backend name", "Admin", "Probe", "Last change");
		(void)VCL_IterDirector(cli, av[i], do_list, la);
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

	(void)cli;
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(sh, priv, SET_HEALTH_MAGIC);
	if (d->vdir->admin_health == VDI_AH_DELETED)
		return (0);
	if (d->vdir->admin_health != sh->ah) {
		d->vdir->health_changed = VTIM_real();
		d->vdir->admin_health = sh->ah;
		d->sick &= ~0x02;
		d->sick |= sh->ah->health ? 0 : 0x02;
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
