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
#include <stdio.h>
#include <stdlib.h>

#include "cache.h"

#include "vcl.h"
#include "vrt.h"

#include "cache_director.h"
#include "cache_backend.h"
#include "vcli.h"
#include "vcli_priv.h"

static const char * const vcl_temp_init = "init";
static const char * const vcl_temp_cold = "cold";
static const char * const vcl_temp_warm = "warm";
static const char * const vcl_temp_cooling = "cooling";

struct vcl {
	unsigned		magic;
#define VCL_MAGIC		0x214188f2
	VTAILQ_ENTRY(vcl)	list;
	void			*dlh;
	const struct VCL_conf	*conf;
	char			state[8];
	char			*loaded_name;
	unsigned		busy;
	unsigned		discard;
	const char		*temp;
	VTAILQ_HEAD(,backend)	backend_list;
};

/*
 * XXX: Presently all modifications to this list happen from the
 * CLI event-engine, so no locking is necessary
 */
static VTAILQ_HEAD(, vcl)	vcl_head =
    VTAILQ_HEAD_INITIALIZER(vcl_head);

static struct lock		vcl_mtx;
static struct vcl		*vcl_active; /* protected by vcl_mtx */

/*--------------------------------------------------------------------*/

void
VCL_Panic(struct vsb *vsb, const struct vcl *vcl)
{
	int i;

	AN(vsb);
	if (vcl == NULL)
		return;
	VSB_printf(vsb, "  vcl = {\n");
	VSB_printf(vsb, "    srcname = {\n");
	for (i = 0; i < vcl->conf->nsrc; ++i)
		VSB_printf(vsb, "      \"%s\",\n", vcl->conf->srcname[i]);
	VSB_printf(vsb, "    },\n");
	VSB_printf(vsb, "  },\n");
}

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
VCL_Get(struct vcl **vcc)
{
	while (vcl_active == NULL)
		(void)usleep(100000);

	CHECK_OBJ_NOTNULL(vcl_active, VCL_MAGIC);
	assert(vcl_active->temp == vcl_temp_warm);
	Lck_Lock(&vcl_mtx);
	AN(vcl_active);
	*vcc = vcl_active;
	AN(*vcc);
	AZ((*vcc)->discard);
	(*vcc)->busy++;
	Lck_Unlock(&vcl_mtx);
}

void
VCL_Refresh(struct vcl **vcc)
{
	CHECK_OBJ_NOTNULL(vcl_active, VCL_MAGIC);
	assert(vcl_active->temp == vcl_temp_warm);
	if (*vcc == vcl_active)
		return;
	if (*vcc != NULL)
		VCL_Rel(vcc);	/* XXX: optimize locking */
	VCL_Get(vcc);
}

void
VCL_Ref(struct vcl *vcl)
{

	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	assert(vcl->temp == vcl_temp_warm);
	Lck_Lock(&vcl_mtx);
	assert(vcl->busy > 0);
	vcl->busy++;
	Lck_Unlock(&vcl_mtx);
}

void
VCL_Rel(struct vcl **vcc)
{
	struct vcl *vcl;

	AN(*vcc);
	vcl = *vcc;
	*vcc = NULL;

	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	Lck_Lock(&vcl_mtx);
	assert(vcl->busy > 0);
	vcl->busy--;
	/*
	 * We do not garbage collect discarded VCL's here, that happens
	 * in VCL_Poll() which is called from the CLI thread.
	 */
	Lck_Unlock(&vcl_mtx);
}

/*--------------------------------------------------------------------*/

void
VCL_AddBackend(struct vcl *vcl, struct backend *be)
{

	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	CHECK_OBJ_NOTNULL(be, BACKEND_MAGIC);
	Lck_Lock(&vcl_mtx);
	VTAILQ_INSERT_TAIL(&vcl->backend_list, be, vcl_list);
	Lck_Unlock(&vcl_mtx);

	if (vcl->temp == vcl_temp_warm) {
		/* Only when adding backend to already warm VCL */
		VBE_Event(be, VCL_EVENT_WARM);
	} else if (vcl->temp != vcl_temp_init)
		WRONG("Dynamic Backends can only be added to warm VCLs");
}

void
VCL_DelBackend(const struct backend *be)
{
	struct vcl *vcl;

	CHECK_OBJ_NOTNULL(be, BACKEND_MAGIC);
	vcl = be->vcl;
	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	Lck_Lock(&vcl_mtx);
	VTAILQ_REMOVE(&vcl->backend_list, be, vcl_list);
	Lck_Unlock(&vcl_mtx);
}

static void
vcl_BackendEvent(const struct vcl *vcl, enum vcl_event_e e)
{
	struct backend *be;

	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	AZ(vcl->busy);

	VTAILQ_FOREACH(be, &vcl->backend_list, vcl_list)
		VBE_Event(be, e);
}

static void
vcl_KillBackends(struct vcl *vcl)
{
	struct backend *be;

	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	AZ(vcl->busy);
	while (1) {
		be = VTAILQ_FIRST(&vcl->backend_list);
		if (be == NULL)
			break;
		VTAILQ_REMOVE(&vcl->backend_list, be, vcl_list);
		VBE_Delete(be);
	}
}

/*--------------------------------------------------------------------*/

static struct vcl *
VCL_Open(const char *fn, struct vsb *msg)
{
	struct vcl *vcl;
	void *dlh;
	struct VCL_conf const *cnf;

	AN(fn);
	AN(msg);

	dlh = dlopen(fn, RTLD_NOW | RTLD_LOCAL);
	if (dlh == NULL) {
		VSB_printf(msg, "Could not load compiled VCL.\n");
		VSB_printf(msg, "\tdlopen(%s) = %s\n", fn, dlerror());
		return (NULL);
	}
	cnf = dlsym(dlh, "VCL_conf");
	if (cnf == NULL) {
		VSB_printf(msg, "Compiled VCL lacks metadata.\n");
		(void)dlclose(dlh);
		return (NULL);
	}
	if (cnf->magic != VCL_CONF_MAGIC) {
		VSB_printf(msg, "Compiled VCL has mangled metadata.\n");
		(void)dlclose(dlh);
		return (NULL);
	}
	ALLOC_OBJ(vcl, VCL_MAGIC);
	AN(vcl);
	vcl->dlh = dlh;
	vcl->conf = cnf;
	return (vcl);
}

static void
VCL_Close(struct vcl **vclp)
{
	struct vcl *vcl;

	CHECK_OBJ_NOTNULL(*vclp, VCL_MAGIC);
	vcl = *vclp;
	*vclp = NULL;
	AZ(dlclose(vcl->dlh));
	FREE_OBJ(vcl);
}

/*--------------------------------------------------------------------
 * NB: This function is called from the test-load subprocess.
 */

int
VCL_TestLoad(const char *fn)
{
	struct vsb *vsb;
	struct vcl *vcl;
	int retval = 0;

	AN(fn);
	vsb = VSB_new_auto();
	AN(vsb);
	vcl = VCL_Open(fn, vsb);
	if (vcl == NULL) {
		AZ(VSB_finish(vsb));
		fprintf(stderr, "%s", VSB_data(vsb));
		retval = -1;
	} else
		VCL_Close(&vcl);
	VSB_delete(vsb);
	return (retval);
}

/*--------------------------------------------------------------------*/

struct director *
VCL_DefaultDirector(const struct vcl *vcl)
{

	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	return (*vcl->conf->default_director);
}

const char *
VCL_Name(const struct vcl *vcl)
{

	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	return (vcl->loaded_name);
}

const struct vrt_backend_probe *
VCL_DefaultProbe(const struct vcl *vcl)
{

	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	return (vcl->conf->default_probe);
}

/*--------------------------------------------------------------------*/

void
VRT_count(VRT_CTX, unsigned u)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->vcl, VCL_MAGIC);
	assert(u < ctx->vcl->conf->nref);
	if (ctx->vsl != NULL)
		VSLb(ctx->vsl, SLT_VCL_trace, "%u %u.%u", u,
		    ctx->vcl->conf->ref[u].line, ctx->vcl->conf->ref[u].pos);
}

/*--------------------------------------------------------------------*/

static struct vcl *
vcl_find(const char *name)
{
	struct vcl *vcl;

	ASSERT_CLI();
	VTAILQ_FOREACH(vcl, &vcl_head, list) {
		if (vcl->discard)
			continue;
		if (!strcmp(vcl->loaded_name, name))
			return (vcl);
	}
	return (NULL);
}

static void
vcl_set_state(struct vcl *vcl, const char *state)
{
	struct vrt_ctx ctx;
	unsigned hand = 0;

	ASSERT_CLI();
	AN(vcl->temp);

	INIT_OBJ(&ctx, VRT_CTX_MAGIC);
	ctx.handling = &hand;

	switch(state[0]) {
	case '0':
		if (vcl->temp == vcl_temp_cold)
			break;
		if (vcl->busy == 0) {
			vcl->temp = vcl_temp_cold;
			(void)vcl->conf->event_vcl(&ctx, VCL_EVENT_COLD);
			vcl_BackendEvent(vcl, VCL_EVENT_COLD);
		} else {
			vcl->temp = vcl_temp_cooling;
		}
		break;
	case '1':
		if (vcl->temp == vcl_temp_cooling)
			vcl->temp = vcl_temp_warm;
		else {
			vcl->temp = vcl_temp_warm;
			(void)vcl->conf->event_vcl(&ctx, VCL_EVENT_WARM);
			vcl_BackendEvent(vcl, VCL_EVENT_WARM);
		}
		break;
	default:
		WRONG("Wrong enum state");
	}
}

static int
VCL_Load(struct cli *cli, const char *name, const char *fn, const char *state)
{
	struct vcl *vcl;
	struct vrt_ctx ctx;
	unsigned hand = 0;
	struct vsb *vsb;
	int i;

	ASSERT_CLI();

	vcl = vcl_find(name);
	if (vcl != NULL) {
		VCLI_Out(cli, "Config '%s' already loaded", name);
		return (1);
	}

	vsb = VSB_new_auto();
	AN(vsb);

	vcl = VCL_Open(fn, vsb);
	if (vcl == NULL) {
		AZ(VSB_finish(vsb));
		VCLI_Out(cli, "%s", VSB_data(vsb));
		VSB_delete(vsb);
		return (1);
	}

	vcl->loaded_name = strdup(name);
	XXXAN(vcl->loaded_name);
	VTAILQ_INIT(&vcl->backend_list);

	vcl->temp = vcl_temp_init;

	INIT_OBJ(&ctx, VRT_CTX_MAGIC);
	ctx.method = VCL_MET_INIT;
	ctx.handling = &hand;
	ctx.vcl = vcl;

	VSB_clear(vsb);
	ctx.msg = vsb;
	i = vcl->conf->event_vcl(&ctx, VCL_EVENT_LOAD);
	AZ(VSB_finish(vsb));
	if (i) {
		VCLI_Out(cli, "VCL \"%s\" Failed initialization", name);
		if (VSB_len(vsb))
			VCLI_Out(cli, "\nMessage:\n\t%s", VSB_data(vsb));
		AZ(vcl->conf->event_vcl(&ctx, VCL_EVENT_DISCARD));
		vcl_KillBackends(vcl);
		VCL_Close(&vcl);
		VSB_delete(vsb);
		return (1);
	}
	VSB_delete(vsb);
	vcl_set_state(vcl, state);
	bprintf(vcl->state, "%s", state + 1);
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
VCL_Nuke(struct vcl *vcl)
{
	struct vrt_ctx ctx;
	unsigned hand = 0;

	INIT_OBJ(&ctx, VRT_CTX_MAGIC);
	ASSERT_CLI();
	assert(vcl != vcl_active);
	assert(vcl->discard);
	AZ(vcl->busy);
	VTAILQ_REMOVE(&vcl_head, vcl, list);
	ctx.method = VCL_MET_FINI;
	ctx.handling = &hand;
	ctx.vcl = vcl;
	AZ(vcl->conf->event_vcl(&ctx, VCL_EVENT_DISCARD));
	vcl_KillBackends(vcl);
	free(vcl->loaded_name);
	VCL_Close(&vcl);
	VSC_C_main->n_vcl--;
	VSC_C_main->n_vcl_discard--;
}

/*--------------------------------------------------------------------*/

void
VCL_Poll(void)
{
	struct vcl *vcl, *vcl2;

	ASSERT_CLI();
	VTAILQ_FOREACH_SAFE(vcl, &vcl_head, list, vcl2) {
		if (vcl->temp == vcl_temp_cooling)
			vcl_set_state(vcl, "0");
		if (vcl->discard && vcl->busy == 0)
			VCL_Nuke(vcl);
	}
}

/*--------------------------------------------------------------------*/

static void __match_proto__(cli_func_t)
ccf_config_list(struct cli *cli, const char * const *av, void *priv)
{
	struct vcl *vcl;
	const char *flg;

	(void)av;
	(void)priv;
	ASSERT_CLI();
	VTAILQ_FOREACH(vcl, &vcl_head, list) {
		if (vcl == vcl_active) {
			flg = "active";
		} else if (vcl->discard) {
			flg = "discarded";
		} else
			flg = "available";
		VCLI_Out(cli, "%-10s %4s/%s  %6u %s\n",
		    flg, vcl->state, vcl->temp, vcl->busy, vcl->loaded_name);
	}
}

static void __match_proto__(cli_func_t)
ccf_config_load(struct cli *cli, const char * const *av, void *priv)
{

	AZ(priv);
	ASSERT_CLI();
	if (VCL_Load(cli, av[2], av[3], av[4]))
		VCLI_SetResult(cli, CLIS_PARAM);
	return;
}

static void __match_proto__(cli_func_t)
ccf_config_state(struct cli *cli, const char * const *av, void *priv)
{
	struct vcl *vcl;

	(void)cli;
	AZ(priv);
	ASSERT_CLI();
	AN(av[2]);
	AN(av[3]);
	vcl = vcl_find(av[2]);
	AN(vcl);			// MGT ensures this
	vcl_set_state(vcl, av[3]);
	bprintf(vcl->state, "%s", av[3] + 1);
}

static void __match_proto__(cli_func_t)
ccf_config_discard(struct cli *cli, const char * const *av, void *priv)
{
	struct vcl *vcl;

	ASSERT_CLI();
	(void)cli;
	AZ(priv);
	vcl = vcl_find(av[2]);
	AN(vcl);			// MGT ensures this
	Lck_Lock(&vcl_mtx);
	assert (vcl != vcl_active);	// MGT ensures this
	VSC_C_main->n_vcl_discard++;
	VSC_C_main->n_vcl_avail--;
	vcl->discard = 1;
	Lck_Unlock(&vcl_mtx);

	if (vcl->busy == 0)
		VCL_Nuke(vcl);
}

static void __match_proto__(cli_func_t)
ccf_config_use(struct cli *cli, const char * const *av, void *priv)
{
	struct vcl *vcl;
	struct vrt_ctx ctx;
	unsigned hand = 0;
	struct vsb *vsb;
	int i;

	ASSERT_CLI();
	AZ(priv);
	vcl = vcl_find(av[2]);
	AN(vcl);				// MGT ensures this
	assert(vcl->temp == vcl_temp_warm);	// MGT ensures this
	INIT_OBJ(&ctx, VRT_CTX_MAGIC);
	ctx.handling = &hand;
	vsb = VSB_new_auto();
	AN(vsb);
	ctx.msg = vsb;
	i = vcl->conf->event_vcl(&ctx, VCL_EVENT_USE);
	AZ(VSB_finish(vsb));
	if (i) {
		VCLI_Out(cli, "VCL \"%s\" Failed to activate", av[2]);
		if (VSB_len(vsb) > 0)
			VCLI_Out(cli, "\nMessage:\n\t%s", VSB_data(vsb));
		VCLI_SetResult(cli, CLIS_CANT);
	} else {
		Lck_Lock(&vcl_mtx);
		vcl_active = vcl;
		Lck_Unlock(&vcl_mtx);
	}
	VSB_delete(vsb);
	return;
}

static void __match_proto__(cli_func_t)
ccf_config_show(struct cli *cli, const char * const *av, void *priv)
{
	struct vcl *vcl;
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
    void *specific, unsigned method, vcl_func_f *func)
{
	char *aws;
	struct vsl_log *vsl = NULL;
	struct vrt_ctx ctx;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	INIT_OBJ(&ctx, VRT_CTX_MAGIC);
	if (req != NULL) {
		CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
		CHECK_OBJ_NOTNULL(req->sp, SESS_MAGIC);
		CHECK_OBJ_NOTNULL(req->vcl, VCL_MAGIC);
		vsl = req->vsl;
		ctx.vcl = req->vcl;
		ctx.http_req = req->http;
		ctx.http_req_top = req->top->http;
		ctx.http_resp = req->resp;
		ctx.req = req;
		ctx.now = req->t_prev;
		ctx.ws = req->ws;
	}
	if (bo != NULL) {
		CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
		CHECK_OBJ_NOTNULL(bo->vcl, VCL_MAGIC);
		vsl = bo->vsl;
		ctx.vcl = bo->vcl;
		ctx.http_bereq = bo->bereq;
		ctx.http_beresp = bo->beresp;
		ctx.bo = bo;
		ctx.now = bo->t_prev;
		ctx.ws = bo->ws;
	}
	assert(ctx.now != 0);
	ctx.vsl = vsl;
	ctx.specific = specific;
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
VCL_##func##_method(struct vcl *vcl, struct worker *wrk,		\
     struct req *req, struct busyobj *bo, void *specific)		\
{									\
									\
	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);				\
	CHECK_OBJ_NOTNULL(vcl->conf, VCL_CONF_MAGIC);			\
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);				\
	vcl_call_method(wrk, req, bo, specific,				\
	    VCL_MET_ ## upper, vcl->conf->func##_func);			\
	AN((1U << wrk->handling) & bitmap);				\
}

#include "tbl/vcl_returns.h"
#undef VCL_MET_MAC

/*--------------------------------------------------------------------*/

static struct cli_proto vcl_cmds[] = {
	{ CLI_VCL_LOAD,		"i", ccf_config_load },
	{ CLI_VCL_LIST,		"i", ccf_config_list },
	{ CLI_VCL_STATE,	"i", ccf_config_state },
	{ CLI_VCL_DISCARD,	"i", ccf_config_discard },
	{ CLI_VCL_USE,		"i", ccf_config_use },
	{ CLI_VCL_SHOW,		"", ccf_config_show },
	{ NULL }
};

void
VCL_Init(void)
{

	CLI_AddFuncs(vcl_cmds);
	Lck_New(&vcl_mtx, lck_vcl);
}
