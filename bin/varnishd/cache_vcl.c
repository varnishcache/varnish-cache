/*
 * $Id$
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <sys/queue.h>

#include "cli.h"
#include "cli_priv.h"
#include "vcl_lang.h"
#include "cache.h"

struct vcls {
	TAILQ_ENTRY(vcls)	list;
	const char		*name;
	void			*dlh;
	struct VCL_conf		*conf;
};

static TAILQ_HEAD(, vcls)	vcl_head =
    TAILQ_HEAD_INITIALIZER(vcl_head);

static struct vcls		*active_vcl;

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
	if (active_vcl == NULL)
		active_vcl = vcl;
	fprintf(stderr, "Loaded \"%s\" as \"%s\"\n", fn , name);
	return (0);
}

void
cli_func_config_list(struct cli *cli, char **av, void *priv)
{
	struct vcls *vcl;

	TAILQ_FOREACH(vcl, &vcl_head, list) {
		cli_out(cli, "%s%s\n",
		    vcl == active_vcl ? "* " : "  ",
		    vcl->name);
	}
}


void
cli_func_config_load(struct cli *cli, char **av, void *priv)
{
	struct vcls *vcl;

	TAILQ_FOREACH(vcl, &vcl_head, list) {
		if (!strcmp(vcl->name, av[2])) {
			cli_out(cli, "Config '%s' already loaded", av[2]);
			cli_result(cli, CLIS_PARAM);
			return;
		}
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
	if (active_vcl == NULL)
		active_vcl = vcl;
	cli_out(cli, "Loaded \"%s\" from \"%s\"\n", vcl->name , av[3]);
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

	TAILQ_FOREACH(vcl, &vcl_head, list) {
		if (!strcmp(vcl->name, av[2]))
			break;
	}
	if (vcl == NULL) {
		cli_out(cli, "No config named '%s' loaded", av[2]);
		cli_result(cli, CLIS_PARAM);
		return;
	}
	active_vcl = vcl;
}

/*--------------------------------------------------------------------*/

void
VCL_pass(VCL_FARGS)
{

	sess->handling = HND_Pass;
	sess->done++;
}
