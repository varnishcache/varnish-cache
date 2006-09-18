/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006 Linpro AS
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
 * $Id$
 *
 * Interface *to* compiled VCL code:  Loading, unloading, calling into etc.
 *
 * The interface *from* the compiled VCL code is in cache_vrt.c.
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>

#include "cli.h"
#include "cli_priv.h"
#include "shmlog.h"
#include "vcl.h"
#include "cache.h"

struct vcls {
	TAILQ_ENTRY(vcls)	list;
	const char		*name;
	void			*dlh;
	struct VCL_conf		*conf;
	int			discard;
};

/*
 * XXX: Presently all modifications to this list happen from the
 * CLI event-engine, so no locking is necessary
 */
static TAILQ_HEAD(, vcls)	vcl_head =
    TAILQ_HEAD_INITIALIZER(vcl_head);


static struct vcls		*vcl_active; /* protected by vcl_mtx */

static MTX			vcl_mtx;

/*--------------------------------------------------------------------*/

void
VCL_Refresh(struct VCL_conf **vcc)
{
	if (*vcc == vcl_active->conf)
		return;
	if (*vcc != NULL)
		VCL_Rel(vcc);
	VCL_Get(vcc);
}

void
VCL_Get(struct VCL_conf **vcc)
{

	LOCK(&vcl_mtx);
	AN(vcl_active);
	*vcc = vcl_active->conf;
	AN(*vcc);
	(*vcc)->busy++;
	UNLOCK(&vcl_mtx);
}

void
VCL_Rel(struct VCL_conf **vcc)
{
	struct vcls *vcl;
	struct VCL_conf *vc;

	vc = *vcc;
	*vcc = NULL;

	LOCK(&vcl_mtx);
	assert(vc->busy > 0);
	vc->busy--;
	vcl = vc->priv;	/* XXX miniobj */
	if (vc->busy == 0 && vcl_active != vcl) {
		/* XXX: purge backends */
	}
	if (vc->busy == 0 && vcl->discard) {
		TAILQ_REMOVE(&vcl_head, vcl, list);
	} else {
		vcl = NULL;
	}
	UNLOCK(&vcl_mtx);
	if (vcl != NULL) {
		/* XXX: dispose of vcl */
	}
}

/*--------------------------------------------------------------------*/

static struct vcls *
vcl_find(const char *name)
{
	struct vcls *vcl;

	TAILQ_FOREACH(vcl, &vcl_head, list)
		if (!strcmp(vcl->name, name))
			return (vcl);
	return (NULL);
}

static int
VCL_Load(const char *fn, const char *name, struct cli *cli)
{
	struct vcls *vcl;

	vcl = vcl_find(name);
	if (vcl != NULL) {
		if (cli == NULL)
			fprintf(stderr, "Config '%s' already loaded", name);
		else 
			cli_out(cli, "Config '%s' already loaded", name);
		return (1);
	}

	vcl = calloc(sizeof *vcl, 1);
	XXXAN(vcl);

	vcl->dlh = dlopen(fn, RTLD_NOW | RTLD_LOCAL);

	if (vcl->dlh == NULL) {
		if (cli == NULL)
			fprintf(stderr, "dlopen(%s): %s\n", fn, dlerror());
		else
			cli_out(cli, "dlopen(%s): %s\n", fn, dlerror());
		free(vcl);
		return (1);
	}
	vcl->conf = dlsym(vcl->dlh, "VCL_conf");
	if (vcl->conf == NULL) {
		if (cli == NULL)
			fprintf(stderr, "No VCL_conf symbol\n");
		else 
			cli_out(cli, "No VCL_conf symbol\n");
		(void)dlclose(vcl->dlh);
		free(vcl);
		return (1);
	}

	if (vcl->conf->magic != VCL_CONF_MAGIC) {
		if (cli == NULL) 
			fprintf(stderr, "Wrong VCL_CONF_MAGIC\n");
		else
			cli_out(cli, "Wrong VCL_CONF_MAGIC\n");
		(void)dlclose(vcl->dlh);
		free(vcl);
		return (1);
	}
	vcl->conf->priv = vcl;
	vcl->name = strdup(name);
	XXXAN(vcl->name);
	TAILQ_INSERT_TAIL(&vcl_head, vcl, list);
	LOCK(&vcl_mtx);
	if (vcl_active == NULL)
		vcl_active = vcl;
	UNLOCK(&vcl_mtx);
	if (cli == NULL)
		fprintf(stderr, "Loaded \"%s\" as \"%s\"\n", fn , name);
	else 
		cli_out(cli, "Loaded \"%s\" as \"%s\"\n", fn , name);
	vcl->conf->init_func();
	return (0);
}

/*--------------------------------------------------------------------*/

void
cli_func_config_list(struct cli *cli, char **av, void *priv)
{
	struct vcls *vcl;

	(void)av;
	(void)priv;
	TAILQ_FOREACH(vcl, &vcl_head, list) {
		cli_out(cli, "%s %6u %s\n",
		    vcl == vcl_active ? "* " : "  ",
		    vcl->conf->busy,
		    vcl->name);
	}
}

void
cli_func_config_load(struct cli *cli, char **av, void *priv)
{

	(void)av;
	(void)priv;
	if (VCL_Load(av[3], av[2], cli))
		cli_result(cli, CLIS_PARAM);
	return;
}

void
cli_func_config_discard(struct cli *cli, char **av, void *priv)
{
	struct vcls *vcl;

	(void)av;
	(void)priv;
	vcl = vcl_find(av[2]);
	if (vcl == NULL) {
		cli_result(cli, CLIS_PARAM);
		cli_out(cli, "VCL '%s' unknown", av[2]);
		return;
	}
	if (vcl->discard) {
		cli_result(cli, CLIS_PARAM);
		cli_out(cli, "VCL %s already discarded", av[2]);
		return;
	}
	LOCK(&vcl_mtx);
	if (vcl == vcl_active) {
		UNLOCK(&vcl_mtx);
		cli_result(cli, CLIS_PARAM);
		cli_out(cli, "VCL %s is the active VCL", av[2]);
		return;
	}
	vcl->discard = 1;
	if (vcl->conf->busy == 0)
		TAILQ_REMOVE(&vcl_head, vcl, list);
	else
		vcl = NULL;
	UNLOCK(&vcl_mtx);
	if (vcl != NULL) {
		/* XXX dispose of vcl */
	}
}

void
cli_func_config_use(struct cli *cli, char **av, void *priv)
{
	struct vcls *vcl;

	(void)av;
	(void)priv;
	vcl = vcl_find(av[2]);
	if (vcl == NULL) {
		cli_out(cli, "No VCL named '%s'", av[2]);
		cli_result(cli, CLIS_PARAM);
		return;
	}
	if (vcl->discard) {
		cli_out(cli, "VCL '%s' has been discarded", av[2]);
		cli_result(cli, CLIS_PARAM);
		return;
	}
	LOCK(&vcl_mtx);
	vcl_active = vcl;
	UNLOCK(&vcl_mtx);
}

/*--------------------------------------------------------------------*/

static const char *
vcl_handlingname(unsigned u)
{

	switch (u) {
#define VCL_RET_MAC(a, b, c,d)	case VCL_RET_##b: return(#a);
#define VCL_RET_MAC_E(a, b, c,d)	case VCL_RET_##b: return(#a);
#include "vcl_returns.h"
#undef VCL_RET_MAC
#undef VCL_RET_MAC_E
	default:
		return (NULL);
	}
}

#define VCL_RET_MAC(l,u,b,n)

#define VCL_MET_MAC(func, xxx, bitmap) 					\
void									\
VCL_##func##_method(struct sess *sp)					\
{									\
									\
	sp->handling = 0;						\
	WSL(sp->wrk, SLT_VCL_call, sp->fd, "%s", #func); 		\
	sp->vcl->func##_func(sp);					\
	WSL(sp->wrk, SLT_VCL_return, sp->fd, "%s",			\
	     vcl_handlingname(sp->handling));				\
	assert(sp->handling & bitmap);					\
	assert(!(sp->handling & ~bitmap));				\
}

#include "vcl_returns.h"
#undef VCL_MET_MAC
#undef VCL_RET_MAC

/*--------------------------------------------------------------------*/

void
VCL_Init()
{

	MTX_INIT(&vcl_mtx);
}
