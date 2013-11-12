/*-
 * Copyright (c) 2007-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Dag-Erling Smørgav <des@des.no>
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
 * STEVEDORE: one who works at or is responsible for loading and
 * unloading ships in port.  Example: "on the wharves, stevedores were
 * unloading cargo from the far corners of the world." Origin: Spanish
 * estibador, from estibar to pack.  First Known Use: 1788
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mgt/mgt.h"
#include "vcli_priv.h"
#include "mgt/mgt_cli.h"

#include "storage/storage.h"
#include "vav.h"

struct stevedore_head stv_stevedores =
    VTAILQ_HEAD_INITIALIZER(stv_stevedores);

struct stevedore *stv_transient;

/*--------------------------------------------------------------------*/

static void
stv_cli_list(struct cli *cli, const char * const *av, void *priv)
{
	struct stevedore *stv;

	ASSERT_MGT();
	(void)av;
	(void)priv;
	VCLI_Out(cli, "Storage devices:\n");
	stv = stv_transient;
		VCLI_Out(cli, "\tstorage.%s = %s\n", stv->ident, stv->name);
	VTAILQ_FOREACH(stv, &stv_stevedores, list)
		VCLI_Out(cli, "\tstorage.%s = %s\n", stv->ident, stv->name);
}

/*--------------------------------------------------------------------*/

struct cli_proto cli_stv[] = {
	{ "storage.list", "storage.list", "List storage devices\n",
	    0, 0, "", stv_cli_list },
	{ NULL}
};
/*--------------------------------------------------------------------
 * Parse a stevedore argument on the form:
 *	[ name '=' ] strategy [ ',' arg ] *
 */

static const struct choice STV_choice[] = {
	{ "file",	&smf_stevedore },
	{ "malloc",	&sma_stevedore },
	{ "persistent",	&smp_stevedore },
#ifdef HAVE_LIBUMEM
	{ "umem",	&smu_stevedore },
#endif
	{ NULL,		NULL }
};

void
STV_Config(const char *spec)
{
	char **av;
	const char *p, *q;
	struct stevedore *stv;
	const struct stevedore *stv2;
	int ac, l;
	static unsigned seq = 0;

	ASSERT_MGT();
	p = strchr(spec, '=');
	q = strchr(spec, ',');
	if (p != NULL && (q == NULL || q > p)) {
		av = VAV_Parse(p + 1, NULL, ARGV_COMMA);
	} else {
		av = VAV_Parse(spec, NULL, ARGV_COMMA);
		p = NULL;
	}
	AN(av);

	if (av[0] != NULL)
		ARGV_ERR("%s\n", av[0]);

	if (av[1] == NULL)
		ARGV_ERR("-s argument lacks strategy {malloc, file, ...}\n");

	for (ac = 0; av[ac + 2] != NULL; ac++)
		continue;

	stv2 = pick(STV_choice, av[1], "storage");
	AN(stv2);

	/* Append strategy to ident string */
	VSB_printf(vident, ",-s%s", av[1]);

	av += 2;

	CHECK_OBJ_NOTNULL(stv2, STEVEDORE_MAGIC);
	ALLOC_OBJ(stv, STEVEDORE_MAGIC);
	AN(stv);

	*stv = *stv2;
	AN(stv->name);
	AN(stv->alloc);
	if (stv->allocobj == NULL)
		stv->allocobj = stv_default_allocobj;

	if (p == NULL)
		bprintf(stv->ident, "s%u", seq++);
	else {
		l = p - spec;
		if (l > sizeof stv->ident - 1)
			l = sizeof stv->ident - 1;
		bprintf(stv->ident, "%.*s", l, spec);
	}

	VTAILQ_FOREACH(stv2, &stv_stevedores, list) {
		if (strcmp(stv2->ident, stv->ident))
			continue;
		ARGV_ERR("(-s%s=%s) already defined once\n",
		    stv->ident, stv->name);
	}

	if (stv->init != NULL)
		stv->init(stv, ac, av);
	else if (ac != 0)
		ARGV_ERR("(-s%s) too many arguments\n", stv->name);

	if (!strcmp(stv->ident, TRANSIENT_STORAGE)) {
		stv->transient = 1;
		AZ(stv_transient);
		stv_transient = stv;
	} else {
		VTAILQ_INSERT_TAIL(&stv_stevedores, stv, list);
	}
	/* NB: Do not free av, stevedore gets to keep it */
}

/*--------------------------------------------------------------------*/

void
STV_Config_Transient(void)
{

	ASSERT_MGT();

	if (stv_transient == NULL)
		STV_Config(TRANSIENT_STORAGE "=malloc");
}

/*--------------------------------------------------------------------*/
