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
#include "vcl.h"
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

const char *
VDI_Ahealth(const struct director *d)
{

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	AN(d->admin_health);
	return (d->admin_health->name);
}

/* Resolve director --------------------------------------------------*/

static const struct director *
vdi_resolve(struct busyobj *bo)
{
	const struct director *d;
	const struct director *d2;
	struct vrt_ctx ctx;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_ORNULL(bo->director_req, DIRECTOR_MAGIC);

	INIT_OBJ(&ctx, VRT_CTX_MAGIC);
	ctx.vcl = bo->vcl;
	ctx.vsl = bo->vsl;
	ctx.http_bereq = bo->bereq;
	ctx.http_beresp = bo->beresp;
	ctx.bo = bo;
	ctx.sp = bo->sp;
	ctx.now = bo->t_prev;
	ctx.ws = bo->ws;
	ctx.method = 0;

	for (d = bo->director_req; d != NULL &&
	    d->methods->resolve != NULL; d = d2) {
		CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
		AN(d->vdir);
		d2 = d->methods->resolve(&ctx, d);
		if (d2 == NULL)
			VSLb(bo->vsl, SLT_FetchError,
			    "Director %s returned no backend", d->vcl_name);
	}
	CHECK_OBJ_ORNULL(d, DIRECTOR_MAGIC);
	if (d == NULL)
		VSLb(bo->vsl, SLT_FetchError, "No backend");
	else
		AN(d->vdir);
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

	d = vdi_resolve(bo);
	if (d != NULL) {
		AN(d->methods->gethdrs);
		bo->director_state = DIR_S_HDRS;
		i = d->methods->gethdrs(d, wrk, bo);
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
	AZ(d->methods->resolve);

	assert(bo->director_state == DIR_S_HDRS);
	bo->director_state = DIR_S_BODY;
	if (d->methods->getbody == NULL)
		return (0);
	return (d->methods->getbody(d, wrk, bo));
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
	AZ(d->methods->resolve);
	if (d->methods->getip == NULL)
		return (NULL);
	return (d->methods->getip(d, wrk, bo));
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

	AZ(d->methods->resolve);
	AN(d->methods->finish);

	assert(bo->director_state != DIR_S_NULL);
	d->methods->finish(d, wrk, bo);
	bo->director_state = DIR_S_NULL;
}

/* Get a connection --------------------------------------------------*/

enum sess_close
VDI_Http1Pipe(struct req *req, struct busyobj *bo)
{
	const struct director *d;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	d = vdi_resolve(bo);
	if (d == NULL || d->methods->http1pipe == NULL) {
		VSLb(bo->vsl, SLT_VCL_Error, "Backend does not support pipe");
		return (SC_TX_ERROR);
	}
	return (d->methods->http1pipe(d, req, bo));
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

	if (d->admin_health->health >= 0) {
		if (changed != NULL)
			*changed = d->health_changed;
		return (d->admin_health->health);
	}

	if (d->methods->healthy == NULL) {
		if (changed != NULL)
			*changed = d->health_changed;
		return (d->health);
	}

	return (d->methods->healthy(ctx, d, changed));
}

/* Send Event ----------------------------------------------------------
 */

void
VDI_Event(const struct director *d, enum vcl_event_e ev)
{

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	if (d->methods->event != NULL)
		d->methods->event(d, ev);
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
	VSB_printf(vsb, "cli_name = %s,\n", d->cli_name);
	VSB_printf(vsb, "health = %s,\n", d->health ?  "healthy" : "sick");
	VSB_printf(vsb, "admin_health = %s, changed = %f,\n",
	    VDI_Ahealth(d), d->health_changed);
	VSB_printf(vsb, "type = %s {\n", d->methods->type);
	VSB_indent(vsb, 2);
	if (d->methods->panic != NULL)
		d->methods->panic(d, vsb);
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
};

static int v_matchproto_(vcl_be_func)
do_list(struct cli *cli, struct director *d, void *priv)
{
	char time_str[VTIM_FORMAT_SIZE];
	struct list_args *la;

	CAST_OBJ_NOTNULL(la, priv, LIST_ARGS_MAGIC);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);

	if (d->admin_health == VDI_AH_DELETED)
		return (0);

	VCLI_Out(cli, "\n%-30s %-7s ", d->cli_name, VDI_Ahealth(d));

	if (d->methods->list != NULL)
		d->methods->list(d, cli->sb, 0, 0);
	else
		VCLI_Out(cli, "%-10s", d->health ? "healthy" : "sick");

	VTIM_format(d->health_changed, time_str);
	VCLI_Out(cli, " %s", time_str);
	if ((la->p || la->v) && d->methods->list != NULL)
		d->methods->list(d, cli->sb, la->p, la->v);
	return (0);
}

static void v_matchproto_(cli_func_t)
cli_backend_list(struct cli *cli, const char * const *av, void *priv)
{
	const char *p;
	struct list_args la[1];

	(void)priv;
	ASSERT_CLI();
	INIT_OBJ(la, LIST_ARGS_MAGIC);
	while (av[2] != NULL && av[2][0] == '-') {
		for(p = av[2] + 1; *p; p++) {
			switch(*p) {
			case 'p': la->p = !la->p; break;
			case 'v': la->p = !la->p; break;
			default:
				VCLI_Out(cli, "Invalid flag %c", *p);
				VCLI_SetResult(cli, CLIS_PARAM);
				return;
			}
		}
		av++;
	}
	if (av[3] != NULL) {
		VCLI_Out(cli, "Too many arguments");
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	}
	VCLI_Out(cli, "%-30s %-7s %-10s %s",
	    "Backend name", "Admin", "Probe", "Last change");
	(void)VCL_IterDirector(cli, av[2], do_list, la);
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
	if (d->admin_health == VDI_AH_DELETED)
		return (0);
	if (d->admin_health != sh->ah) {
		d->health_changed = VTIM_real();
		d->admin_health = sh->ah;
		d->health = sh->ah->health ? 1 : 0;
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
