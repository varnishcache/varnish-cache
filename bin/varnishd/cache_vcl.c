/*
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
#include "libvarnish.h"
#include "cache.h"

struct vcls {
	TAILQ_ENTRY(vcls)	list;
	const char		*name;
	void			*dlh;
	struct VCL_conf		*conf;
};

/*
 * XXX: Presently all modifications to this list happen from the
 * CLI event-engine, so no locking is necessary
 */
static TAILQ_HEAD(, vcls)	vcl_head =
    TAILQ_HEAD_INITIALIZER(vcl_head);


static struct vcls		*vcl_active; /* protected by vcl_mtx */

static pthread_mutex_t		vcl_mtx;

/*--------------------------------------------------------------------*/

struct VCL_conf *
VCL_Get(void)
{
	struct VCL_conf *vc;

	AZ(pthread_mutex_lock(&vcl_mtx));
	assert(vcl_active != NULL);
	vc = vcl_active->conf;
	assert(vc != NULL);
	vc->busy++;
	AZ(pthread_mutex_unlock(&vcl_mtx));
	return (vc);
}

void
VCL_Rel(struct VCL_conf *vc)
{

	AZ(pthread_mutex_lock(&vcl_mtx));
	vc->busy--;
	AZ(pthread_mutex_unlock(&vcl_mtx));
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

int
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
	assert(vcl != NULL);

	vcl->dlh = dlopen(fn, RTLD_NOW | RTLD_LOCAL);

	/*
	 * Delete the file, either we got hold of it, or we couldn't care
	 * less about it anyway.
	 */
	(void)unlink(fn);

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
	vcl->name = strdup(name);
	assert(vcl->name != NULL);
	TAILQ_INSERT_TAIL(&vcl_head, vcl, list);
	AZ(pthread_mutex_lock(&vcl_mtx));
	if (vcl_active == NULL)
		vcl_active = vcl;
	AZ(pthread_mutex_unlock(&vcl_mtx));
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
cli_func_config_unload(struct cli *cli, char **av, void *priv)
{

	(void)av;
	(void)priv;
	cli_result(cli, CLIS_UNIMPL);
}

void
cli_func_config_use(struct cli *cli, char **av, void *priv)
{
	struct vcls *vcl;

	(void)av;
	(void)priv;
	vcl = vcl_find(av[2]);
	if (vcl != NULL) {
		AZ(pthread_mutex_lock(&vcl_mtx));
		vcl_active = vcl;
		AZ(pthread_mutex_unlock(&vcl_mtx));
	} else {
		cli_out(cli, "No config named '%s' loaded", av[2]);
		cli_result(cli, CLIS_PARAM);
	}
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
	VSL(SLT_VCL_call, sp->fd, "%s", #func); 			\
	sp->vcl->func##_func(sp);					\
	VSL(SLT_VCL_return, sp->fd, "%s",				\
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

	AZ(pthread_mutex_init(&vcl_mtx, NULL));
}
