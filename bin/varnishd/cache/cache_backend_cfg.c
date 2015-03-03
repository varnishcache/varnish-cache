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
 * Handle configuration of backends from VCL programs.
 *
 */

#include "config.h"

#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache.h"

#include "cache_backend.h"
#include "vcli.h"
#include "vcli_priv.h"
#include "vcl.h"
#include "vsa.h"
#include "vrt.h"
#include "vtim.h"

/*
 * The list of backends is not locked, it is only ever accessed from
 * the CLI thread, so there is no need.
 */
static VTAILQ_HEAD(, backend) backends = VTAILQ_HEAD_INITIALIZER(backends);

/*--------------------------------------------------------------------
 */

void
VBE_DeleteBackend(struct backend *b)
{

	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);

	VTAILQ_REMOVE(&backends, b, list);
	free(b->ipv4);
	free(b->ipv6);
	free(b->display_name);
	VSM_Free(b->vsc);
	VBT_Rel(&b->tcp_pool);
	FREE_OBJ(b);
	VSC_C_main->n_backend--;
}

/*--------------------------------------------------------------------
 * Add a backend/director instance when loading a VCL.
 * If an existing backend is matched, grab a refcount and return.
 * Else create a new backend structure with reference initialized to one.
 */

struct backend *
VBE_AddBackend(const char *vcl, const struct vrt_backend *vb)
{
	struct backend *b;
	char buf[128];

	ASSERT_CLI();
	AN(vb->vcl_name);
	assert(vb->ipv4_suckaddr != NULL || vb->ipv6_suckaddr != NULL);

	/* Create new backend */
	ALLOC_OBJ(b, BACKEND_MAGIC);
	XXXAN(b);
	Lck_New(&b->mtx, lck_backend);

	bprintf(buf, "%s.%s", vcl, vb->vcl_name);
	REPLACE(b->display_name, buf);

	b->vsc = VSM_Alloc(sizeof *b->vsc, VSC_CLASS, VSC_type_vbe, buf);

	b->vcl_name =  vb->vcl_name;
	b->ipv4_addr = vb->ipv4_addr;
	b->ipv6_addr = vb->ipv6_addr;
	b->port = vb->port;

	b->tcp_pool = VBT_Ref(vb->vcl_name,
	    vb->ipv4_suckaddr, vb->ipv6_suckaddr);

	/*
	 * Copy over the sockaddrs
	 */
	if (vb->ipv4_suckaddr != NULL)
		b->ipv4 = VSA_Clone(vb->ipv4_suckaddr);
	if (vb->ipv6_suckaddr != NULL)
		b->ipv6 = VSA_Clone(vb->ipv6_suckaddr);

	assert(b->ipv4 != NULL || b->ipv6 != NULL);

	b->healthy = 1;
	b->health_changed = VTIM_real();
	b->admin_health = ah_probe;

	VTAILQ_INSERT_TAIL(&backends, b, list);
	VSC_C_main->n_backend++;
	return (b);
}

/*---------------------------------------------------------------------
 * String to admin_health
 */

static enum admin_health
vbe_str2adminhealth(const char *wstate)
{

	if (strcasecmp(wstate, "healthy") == 0)
		return (ah_healthy);
	if (strcasecmp(wstate, "sick") == 0)
		return (ah_sick);
	if (strcmp(wstate, "auto") == 0)
		return (ah_probe);
	return (ah_invalid);
}

/*---------------------------------------------------------------------
 * A general function for finding backends and doing things with them.
 *
 * Return -1 on match-argument parse errors.
 *
 * If the call-back function returns negative, the search is terminated
 * and we relay that return value.
 *
 * Otherwise we return the number of matches.
 */

typedef int bf_func(struct cli *cli, struct backend *b, void *priv);

static int
backend_find(struct cli *cli, const char *matcher, bf_func *func, void *priv)
{
	int i, found = 0;
	struct vsb *vsb;
	struct VCL_conf *vcc = NULL;
	struct backend *b;

	VCL_Refresh(&vcc);
	AN(vcc);
	vsb = VSB_new_auto();
	AN(vsb);
	if (matcher == NULL || *matcher == '\0' || !strcmp(matcher, "*")) {
		// all backends in active VCL
		VSB_printf(vsb, "%s.*", vcc->loaded_name);
	} else if (strchr(matcher, '.') != NULL) {
		// use pattern as is
		VSB_cat(vsb, matcher);
	} else {
		// pattern applies to active vcl
		VSB_printf(vsb, "%s.%s", vcc->loaded_name, matcher);
	}
	AZ(VSB_finish(vsb));
	VCLI_Out(cli, "Using pattern \"%s\"\n", VSB_data(vsb));
	VTAILQ_FOREACH(b, &backends, list) {
		if (fnmatch(VSB_data(vsb), b->display_name, 0))
			continue;
		found++;
		i = func(cli, b, priv);
		if (i < 0) {
			found = i;
			break;
		}
	}
	VSB_delete(vsb);
	return (found);
}

/*---------------------------------------------------------------------*/

static int __match_proto__()
do_list(struct cli *cli, struct backend *b, void *priv)
{
	int *hdr;

	AN(priv);
	hdr = priv;
	if (!*hdr) {
		VCLI_Out(cli, "%-30s %-10s %s",
		    "Backend name", "Admin", "Probe");
		*hdr = 1;
	}
	CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);

	VCLI_Out(cli, "\n%-30s", b->display_name);

	if (b->admin_health == ah_probe)
		VCLI_Out(cli, " %-10s", "probe");
	else if (b->admin_health == ah_sick)
		VCLI_Out(cli, " %-10s", "sick");
	else if (b->admin_health == ah_healthy)
		VCLI_Out(cli, " %-10s", "healthy");
	else
		VCLI_Out(cli, " %-10s", "invalid");

	if (b->probe == NULL)
		VCLI_Out(cli, " %s", "Healthy (no probe)");
	else {
		if (b->healthy)
			VCLI_Out(cli, " %s", "Healthy ");
		else
			VCLI_Out(cli, " %s", "Sick ");
		VBP_Summary(cli, b->probe);
	}

	/* XXX: report b->health_changed */

	return (0);
}

static void
cli_backend_list(struct cli *cli, const char * const *av, void *priv)
{
	int hdr = 0;

	(void)av;
	(void)priv;
	ASSERT_CLI();
	(void)backend_find(cli, av[2], do_list, &hdr);
}

/*---------------------------------------------------------------------*/

static int __match_proto__()
do_set_health(struct cli *cli, struct backend *b, void *priv)
{
	enum admin_health state;
	unsigned prev;

	(void)cli;
	state = *(enum admin_health*)priv;
	CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);
	prev = VBE_Healthy(b, NULL);
	b->admin_health = state;
	if (prev != VBE_Healthy(b, NULL))
		b->health_changed = VTIM_real();

	return (0);
}

static void
cli_backend_set_health(struct cli *cli, const char * const *av, void *priv)
{
	enum admin_health state;
	int n;

	(void)av;
	(void)priv;
	ASSERT_CLI();
	AN(av[2]);
	AN(av[3]);
	state = vbe_str2adminhealth(av[3]);
	if (state == ah_invalid) {
		VCLI_Out(cli, "Invalid state %s", av[3]);
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	}
	n = backend_find(cli, av[2], do_set_health, &state);
	if (n == 0) {
		VCLI_Out(cli, "No Backends matches");
		VCLI_SetResult(cli, CLIS_PARAM);
	}
}

/*---------------------------------------------------------------------*/

static struct cli_proto backend_cmds[] = {
	{ "backend.list", "backend.list [<backend_expression>]",
	    "\tList backends.",
	    0, 1, "", cli_backend_list },
	{ "backend.set_health",
	    "backend.set_health <backend_expression> <state>",
	    "\tSet health status on the backends.",
	    2, 2, "", cli_backend_set_health },
	{ NULL }
};

/*---------------------------------------------------------------------*/

void
VBE_InitCfg(void)
{

	CLI_AddFuncs(backend_cmds);
}
