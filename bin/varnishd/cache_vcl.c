/*
 * $Id$
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <pthread.h>
#include <dlfcn.h>
#include <sys/queue.h>

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


static struct vcls		*active_vcl; /* protected by sessmtx */


/*--------------------------------------------------------------------*/

struct VCL_conf *
GetVCL(void)
{
	struct VCL_conf *vc;

	/* XXX: assert sessmtx (procects active_vcl && ->busy) */
	assert(active_vcl != NULL);
	vc = active_vcl->conf;
	assert(vc != NULL);
	vc->busy++;
	return (vc);
}

void
RelVCL(struct VCL_conf *vc)
{

	/* XXX: assert sessmtx (procects ->busy) */
	vc->busy--;
}

/*--------------------------------------------------------------------*/

int
CVCL_Load(const char *fn, const char *name)
{
	struct vcls *vcl;

	vcl = calloc(sizeof *vcl, 1);
	assert(vcl != NULL);

	vcl->dlh = dlopen(fn, RTLD_NOW | RTLD_LOCAL);
	if (vcl->dlh == NULL) {
		fprintf(stderr, "dlopen(%s): %s\n", fn, dlerror());
		free(vcl);
		return (1);
	}
	vcl->conf = dlsym(vcl->dlh, "VCL_conf");
	if (vcl->conf == NULL) {
		fprintf(stderr, "No VCL_conf symbol\n");
		dlclose(vcl->dlh);
		free(vcl);
		return (1);
	}
	if (vcl->conf->magic != VCL_CONF_MAGIC) {
		fprintf(stderr, "Wrong VCL_CONF_MAGIC\n");
		dlclose(vcl->dlh);
		free(vcl);
		return (1);
	}
	vcl->name = strdup(name);
	assert(vcl->name != NULL);
	TAILQ_INSERT_TAIL(&vcl_head, vcl, list);
	AZ(pthread_mutex_lock(&sessmtx));
	if (active_vcl == NULL)
		active_vcl = vcl;
	AZ(pthread_mutex_unlock(&sessmtx));
	fprintf(stderr, "Loaded \"%s\" as \"%s\"\n", fn , name);
	vcl->conf->init_func();
	return (0);
}

void
cli_func_config_list(struct cli *cli, char **av, void *priv)
{
	struct vcls *vcl;

	TAILQ_FOREACH(vcl, &vcl_head, list) {
		cli_out(cli, "%s %6u %s\n",
		    vcl == active_vcl ? "* " : "  ",
		    vcl->conf->busy,
		    vcl->name);
	}
}

static struct vcls *
find_vcls(const char *name)
{
	struct vcls *vcl;

	TAILQ_FOREACH(vcl, &vcl_head, list)
		if (!strcmp(vcl->name, name))
			return (vcl);
	return (NULL);
}

void
cli_func_config_load(struct cli *cli, char **av, void *priv)
{
	struct vcls *vcl;

	vcl = find_vcls(av[2]);
	if (vcl != NULL) {
		cli_out(cli, "Config '%s' already loaded", av[2]);
		cli_result(cli, CLIS_PARAM);
		return;
	}
	vcl = calloc(sizeof *vcl, 1);
	assert(vcl != NULL);

	vcl->dlh = dlopen(av[3], RTLD_NOW | RTLD_LOCAL);
	if (vcl->dlh == NULL) {
		cli_out(cli, "dlopen(%s): %s\n", av[3], dlerror());
		cli_result(cli, CLIS_PARAM);
		free(vcl);
		return;
	}
	vcl->conf = dlsym(vcl->dlh, "VCL_conf");
	if (vcl->conf == NULL) {
		cli_out(cli, "No VCL_conf symbol\n");
		cli_result(cli, CLIS_PARAM);
		dlclose(vcl->dlh);
		free(vcl);
		return;
	}
	if (vcl->conf->magic != VCL_CONF_MAGIC) {
		cli_out(cli, "Wrong VCL_CONF_MAGIC\n");
		cli_result(cli, CLIS_PARAM);
		dlclose(vcl->dlh);
		free(vcl);
		return;
	}
	vcl->name = strdup(av[2]);
	assert(vcl->name != NULL);
	TAILQ_INSERT_TAIL(&vcl_head, vcl, list);
	cli_out(cli, "Loaded \"%s\" from \"%s\"\n", vcl->name , av[3]);
	return;
}

void
cli_func_config_unload(struct cli *cli, char **av, void *priv)
{
	cli_result(cli, CLIS_UNIMPL);
}

void
cli_func_config_use(struct cli *cli, char **av, void *priv)
{
	struct vcls *vcl;

	vcl = find_vcls(av[2]);
	if (vcl != NULL) {
		AZ(pthread_mutex_lock(&sessmtx));
		active_vcl = vcl;
		AZ(pthread_mutex_unlock(&sessmtx));
	} else {
		cli_out(cli, "No config named '%s' loaded", av[2]);
		cli_result(cli, CLIS_PARAM);
	}
}

/*--------------------------------------------------------------------*/

static const char *
HandlingName(unsigned u)
{

	switch (u) {
#define VCL_RET_MAC(a, b, c)	case VCL_RET_##b: return(#a);
#define VCL_RET_MAC_E(a, b, c)	case VCL_RET_##b: return(#a);
#include "vcl_returns.h"
#undef VCL_RET_MAC
#undef VCL_RET_MAC_E
	default:		return (NULL);
	}
}

static void
CheckHandling(struct sess *sp, const char *func, unsigned bitmap)
{
	unsigned u;
	const char *n;

	u = sp->handling;
	n = HandlingName(u);
	if (u & (u - 1))
		VSL(SLT_Error, sp->fd,
		    "Illegal handling after %s function: 0x%x", func, u);
	else if (!(u & bitmap))
		VSL(SLT_Error, sp->fd,
		    "Wrong handling after %s function: 0x%x", func, u);
	else
		return;
	sp->handling = VCL_RET_ERROR;
}

#define VCL_method(func, bitmap) 		\
void						\
VCL_##func##_method(struct sess *sp)		\
{						\
						\
	sp->handling = 0;			\
	sp->vcl->func##_func(sp);		\
	CheckHandling(sp, #func, (bitmap));	\
	VSL(SLT_vcl_##func, sp->fd, "%s", HandlingName(sp->handling)); \
}

#define VCL_RET_MAC(l,u,b)
#define VCL_MET_MAC(l,u,b) VCL_method(l, b)
#include "vcl_returns.h"
#undef VCL_MET_MAC
#undef VCL_RET_MAC
