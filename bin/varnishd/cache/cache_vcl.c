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
 * Interface *to* compiled VCL code:  Loading, unloading, calling into etc.
 *
 * The interface *from* the compiled VCL code is in cache_vrt.c.
 */

#include "config.h"

#include <dlfcn.h>
#include <stdlib.h>

#include "cache.h"

#include "vcl.h"
#include "vrt.h"
#include "vcli.h"
#include "vcli_priv.h"

struct vcls {
	unsigned		magic;
#define VVCLS_MAGIC		0x214188f2
	VTAILQ_ENTRY(vcls)	list;
	void			*dlh;
	struct VCL_conf		conf[1];
};

/*
 * XXX: Presently all modifications to this list happen from the
 * CLI event-engine, so no locking is necessary
 */
static VTAILQ_HEAD(, vcls)	vcl_head =
    VTAILQ_HEAD_INITIALIZER(vcl_head);


static struct lock		vcl_mtx;
static struct vcls		*vcl_active; /* protected by vcl_mtx */

/*--------------------------------------------------------------------*/

const char *
VCL_Return_Name(unsigned r)
{

	switch (r) {
#define VCL_RET_MAC(l, U, B) case VCL_RET_##U: return(#l);
#include "tbl/vcl_returns.h"
#undef VCL_RET_MAC
	default:
		return (NULL);
	}
}

const char *
VCL_Method_Name(unsigned m)
{

	switch (m) {
#define VCL_MET_MAC(func, upper, bitmap) case VCL_MET_##upper: return (#upper);
#include "tbl/vcl_returns.h"
#undef VCL_MET_MAC
	default:
		return (NULL);
	}
}

/*--------------------------------------------------------------------*/

static void
VCL_Get(struct VCL_conf **vcc)
{
	static int once = 0;

	while (!once && vcl_active == NULL) {
		(void)sleep(1);
	}
	once = 1;

	Lck_Lock(&vcl_mtx);
	AN(vcl_active);
	*vcc = vcl_active->conf;
	AN(*vcc);
	AZ((*vcc)->discard);
	(*vcc)->busy++;
	Lck_Unlock(&vcl_mtx);
}

void
VCL_Refresh(struct VCL_conf **vcc)
{
	if (*vcc == vcl_active->conf)
		return;
	if (*vcc != NULL)
		VCL_Rel(vcc);	/* XXX: optimize locking */
	VCL_Get(vcc);
}

void
VCL_Ref(struct VCL_conf *vc)
{

	Lck_Lock(&vcl_mtx);
	assert(vc->busy > 0);
	vc->busy++;
	Lck_Unlock(&vcl_mtx);
}

void
VCL_Rel(struct VCL_conf **vcc)
{
	struct VCL_conf *vc;

	AN(*vcc);
	vc = *vcc;
	*vcc = NULL;

	Lck_Lock(&vcl_mtx);
	assert(vc->busy > 0);
	vc->busy--;
	/*
	 * We do not garbage collect discarded VCL's here, that happens
	 * in VCL_Poll() which is called from the CLI thread.
	 */
	Lck_Unlock(&vcl_mtx);
}

/*--------------------------------------------------------------------*/

static struct vcls *
vcl_find(const char *name)
{
	struct vcls *vcl;

	ASSERT_CLI();
	VTAILQ_FOREACH(vcl, &vcl_head, list) {
		if (vcl->conf->discard)
			continue;
		if (!strcmp(vcl->conf->loaded_name, name))
			return (vcl);
	}
	return (NULL);
}

static int
VCL_Load(const char *fn, const char *name, struct cli *cli)
{
	struct vcls *vcl;
	struct VCL_conf const *cnf;
	struct vrt_ctx ctx;
	unsigned hand = 0;

	ASSERT_CLI();

	vcl = vcl_find(name);
	if (vcl != NULL) {
		VCLI_Out(cli, "Config '%s' already loaded", name);
		return (1);
	}

	ALLOC_OBJ(vcl, VVCLS_MAGIC);
	XXXAN(vcl);

	vcl->dlh = dlopen(fn, RTLD_NOW | RTLD_LOCAL);

	if (vcl->dlh == NULL) {
		VCLI_Out(cli, "dlopen(%s): %s\n", fn, dlerror());
		FREE_OBJ(vcl);
		return (1);
	}
	cnf = dlsym(vcl->dlh, "VCL_conf");
	if (cnf == NULL) {
		VCLI_Out(cli, "Internal error: No VCL_conf symbol\n");
		(void)dlclose(vcl->dlh);
		FREE_OBJ(vcl);
		return (1);
	}
	memcpy(vcl->conf, cnf, sizeof *cnf);
	vcl->conf->loaded_name = strdup(name);
	XXXAN(vcl->conf->loaded_name);

	if (vcl->conf->magic != VCL_CONF_MAGIC) {
		VCLI_Out(cli, "Wrong VCL_CONF_MAGIC\n");
		(void)dlclose(vcl->dlh);
		FREE_OBJ(vcl);
		return (1);
	}

	INIT_OBJ(&ctx, VRT_CTX_MAGIC);
	ctx.method = VCL_MET_INIT;
	ctx.handling = &hand;
	ctx.cli = cli;
	ctx.vcl = vcl->conf;

	if (vcl->conf->event_vcl(&ctx, VCL_EVENT_LOAD)) {
		VCLI_Out(cli, "VCL \"%s\" Failed to initialize", name);
		AZ(vcl->conf->event_vcl(&ctx, VCL_EVENT_DISCARD));
		(void)dlclose(vcl->dlh);
		FREE_OBJ(vcl);
		return (1);
	}
	(void)vcl->conf->init_func(&ctx);
	if (hand == VCL_RET_FAIL) {
		VCLI_Out(cli, "VCL \"%s\" vcl_init{} failed", name);
		ctx.method = VCL_MET_FINI;
		(void)vcl->conf->fini_func(&ctx);
		AZ(vcl->conf->event_vcl(&ctx, VCL_EVENT_DISCARD));
		(void)dlclose(vcl->dlh);
		FREE_OBJ(vcl);
		return (1);
	}
	assert(hand == VCL_RET_OK);
	VCLI_Out(cli, "Loaded \"%s\" as \"%s\"", fn , name);
	VTAILQ_INSERT_TAIL(&vcl_head, vcl, list);
	Lck_Lock(&vcl_mtx);
	if (vcl_active == NULL)
		vcl_active = vcl;
	Lck_Unlock(&vcl_mtx);
	VSC_C_main->n_vcl++;
	VSC_C_main->n_vcl_avail++;
	return (0);
}

/*--------------------------------------------------------------------
 * This function is polled from the CLI thread to dispose of any non-busy
 * VCLs which have been discarded.
 */

static void
VCL_Nuke(struct vcls *vcl)
{
	struct vrt_ctx ctx;
	unsigned hand = 0;

	INIT_OBJ(&ctx, VRT_CTX_MAGIC);
	ASSERT_CLI();
	assert(vcl != vcl_active);
	assert(vcl->conf->discard);
	AZ(vcl->conf->busy);
	VTAILQ_REMOVE(&vcl_head, vcl, list);
	ctx.method = VCL_MET_FINI;
	ctx.handling = &hand;
	ctx.vcl = vcl->conf;
	(void)vcl->conf->fini_func(&ctx);
	assert(hand == VCL_RET_OK);
	AZ(vcl->conf->event_vcl(&ctx, VCL_EVENT_DISCARD));
	free(vcl->conf->loaded_name);
	(void)dlclose(vcl->dlh);
	FREE_OBJ(vcl);
	VSC_C_main->n_vcl--;
	VSC_C_main->n_vcl_discard--;
}

/*--------------------------------------------------------------------*/

void
VCL_Poll(void)
{
	struct vcls *vcl, *vcl2;

	ASSERT_CLI();
	VTAILQ_FOREACH_SAFE(vcl, &vcl_head, list, vcl2)
		if (vcl->conf->discard && vcl->conf->busy == 0)
			VCL_Nuke(vcl);
}

/*--------------------------------------------------------------------*/

static void __match_proto__(cli_func_t)
ccf_config_list(struct cli *cli, const char * const *av, void *priv)
{
	struct vcls *vcl;
	const char *flg;

	(void)av;
	(void)priv;
	ASSERT_CLI();
	VTAILQ_FOREACH(vcl, &vcl_head, list) {
		if (vcl == vcl_active) {
			flg = "active";
		} else if (vcl->conf->discard) {
			flg = "discarded";
		} else
			flg = "available";
		VCLI_Out(cli, "%-10s %6u %s\n",
		    flg,
		    vcl->conf->busy,
		    vcl->conf->loaded_name);
	}
}

static void __match_proto__(cli_func_t)
ccf_config_load(struct cli *cli, const char * const *av, void *priv)
{

	AZ(priv);
	ASSERT_CLI();
	if (VCL_Load(av[3], av[2], cli))
		VCLI_SetResult(cli, CLIS_PARAM);
	return;
}

static void __match_proto__(cli_func_t)
ccf_config_discard(struct cli *cli, const char * const *av, void *priv)
{
	struct vcls *vcl;

	ASSERT_CLI();
	(void)cli;
	AZ(priv);
	vcl = vcl_find(av[2]);
	AN(vcl);			// MGT ensures this
	Lck_Lock(&vcl_mtx);
	assert (vcl != vcl_active);	// MGT ensures this
	VSC_C_main->n_vcl_discard++;
	VSC_C_main->n_vcl_avail--;
	vcl->conf->discard = 1;
	Lck_Unlock(&vcl_mtx);

	if (vcl->conf->busy == 0)
		VCL_Nuke(vcl);
}

static void __match_proto__(cli_func_t)
ccf_config_use(struct cli *cli, const char * const *av, void *priv)
{
	struct vcls *vcl;
	struct vrt_ctx ctx;
	unsigned hand = 0;

	ASSERT_CLI();
	AZ(priv);
	vcl = vcl_find(av[2]);
	AN(vcl);			// MGT ensures this
	INIT_OBJ(&ctx, VRT_CTX_MAGIC);
	ctx.handling = &hand;
	ctx.cli = cli;
	if (vcl->conf->event_vcl(&ctx, VCL_EVENT_USE)) {
		VCLI_Out(cli, "VCL \"%s\" Failed to activate", av[2]);
		VCLI_SetResult(cli, CLIS_CANT);
		return;
	}

	Lck_Lock(&vcl_mtx);
	vcl_active = vcl;
	Lck_Unlock(&vcl_mtx);
}

static void __match_proto__(cli_func_t)
ccf_config_show(struct cli *cli, const char * const *av, void *priv)
{
	struct vcls *vcl;
	int verbose = 0;
	int i;

	ASSERT_CLI();
	AZ(priv);
	if (!strcmp(av[2], "-v") && av[3] == NULL) {
		VCLI_Out(cli, "Too few parameters");
		VCLI_SetResult(cli, CLIS_TOOFEW);
		return;
	} else if (strcmp(av[2], "-v") && av[3] != NULL) {
		VCLI_Out(cli, "Unknown options '%s'", av[2]);
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	} else if (av[3] != NULL) {
		verbose = 1;
		vcl = vcl_find(av[3]);
	} else
		vcl = vcl_find(av[2]);

	if (vcl == NULL) {
		VCLI_Out(cli, "No VCL named '%s'",
		    av[3] == NULL ? av[2] : av[3]);
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	}
	if (verbose) {
		for (i = 0; i < vcl->conf->nsrc; i++)
			VCLI_Out(cli, "// VCL.SHOW %d %zd %s\n%s\n",
			    i, strlen(vcl->conf->srcbody[i]),
			    vcl->conf->srcname[i],
			    vcl->conf->srcbody[i]);
	} else {
		VCLI_Out(cli, "%s", vcl->conf->srcbody[0]);
	}
}

/*--------------------------------------------------------------------
 * Method functions to call into VCL programs.
 *
 * Either the request or busyobject must be specified, but not both.
 * The workspace argument is where random VCL stuff gets space from.
 */

static void
vcl_call_method(struct worker *wrk, struct req *req, struct busyobj *bo,
    struct ws *ws, unsigned method, vcl_func_f *func)
{
	char *aws;
	struct vsl_log *vsl = NULL;
	struct vrt_ctx ctx;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	INIT_OBJ(&ctx, VRT_CTX_MAGIC);
	if (req != NULL) {
		CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
		CHECK_OBJ_NOTNULL(req->sp, SESS_MAGIC);
		vsl = req->vsl;
		ctx.vcl = req->vcl;
		ctx.http_req = req->http;
		ctx.http_resp = req->resp;
		ctx.req = req;
		ctx.now = req->t_prev;
	}
	if (bo != NULL) {
		CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
		vsl = bo->vsl;
		ctx.vcl = bo->vcl;
		ctx.http_bereq = bo->bereq;
		ctx.http_beresp = bo->beresp;
		ctx.bo = bo;
		ctx.now = bo->t_prev;
	}
	assert(ctx.now != 0);
	ctx.ws = ws;
	ctx.vsl = vsl;
	ctx.method = method;
	ctx.handling = &wrk->handling;
	aws = WS_Snapshot(wrk->aws);
	wrk->handling = 0;
	wrk->cur_method = method;
	wrk->seen_methods |= method;
	AN(vsl);
	VSLb(vsl, SLT_VCL_call, "%s", VCL_Method_Name(method));
	(void)func(&ctx);
	VSLb(vsl, SLT_VCL_return, "%s", VCL_Return_Name(wrk->handling));
	wrk->cur_method |= 1;		// Magic marker

	/*
	 * VCL/Vmods are not allowed to make permanent allocations from
	 * wrk->aws, but they can reserve and return from it.
	 */
	assert(aws == WS_Snapshot(wrk->aws));
}

#define VCL_MET_MAC(func, upper, bitmap)				\
void									\
VCL_##func##_method(struct VCL_conf *vcl, struct worker *wrk,		\
     struct req *req, struct busyobj *bo, struct ws *ws)		\
{									\
									\
	CHECK_OBJ_NOTNULL(vcl, VCL_CONF_MAGIC);				\
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);				\
	vcl_call_method(wrk, req, bo, ws, VCL_MET_ ## upper,		\
	    vcl->func##_func);						\
	AN((1U << wrk->handling) & bitmap);				\
}

#include "tbl/vcl_returns.h"
#undef VCL_MET_MAC

/*--------------------------------------------------------------------*/

static struct cli_proto vcl_cmds[] = {
	{ CLI_VCL_LOAD,         "i", ccf_config_load },
	{ CLI_VCL_LIST,         "i", ccf_config_list },
	{ CLI_VCL_DISCARD,      "i", ccf_config_discard },
	{ CLI_VCL_USE,          "i", ccf_config_use },
	{ CLI_VCL_SHOW,		"", ccf_config_show },
	{ NULL }
};

void
VCL_Init(void)
{

	CLI_AddFuncs(vcl_cmds);
	Lck_New(&vcl_mtx, lck_vcl);
}
