/*-
 * Copyright (c) 2007-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Dag-Erling Smørgav <des@des.no>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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
#include "common/heritage.h"
#include "vcli_serve.h"

#include "storage/storage.h"

VTAILQ_HEAD(stevedore_head, stevedore);

static struct stevedore_head proto_stevedores =
    VTAILQ_HEAD_INITIALIZER(proto_stevedores);

static struct stevedore_head pre_stevedores =
    VTAILQ_HEAD_INITIALIZER(pre_stevedores);

static struct stevedore_head stevedores =
    VTAILQ_HEAD_INITIALIZER(stevedores);

/* Name of transient storage */
#define TRANSIENT_STORAGE	"Transient"

struct stevedore *stv_transient;

const char *mgt_stv_h2_rxbuf;

/*--------------------------------------------------------------------*/

int
STV__iter(struct stevedore ** const pp)
{

	AN(pp);
	CHECK_OBJ_ORNULL(*pp, STEVEDORE_MAGIC);
	if (*pp != NULL)
		*pp = VTAILQ_NEXT(*pp, list);
	else if (!VTAILQ_EMPTY(&stevedores))
		*pp = VTAILQ_FIRST(&stevedores);
	else
		*pp = VTAILQ_FIRST(&pre_stevedores);
	return (*pp != NULL);
}

/*--------------------------------------------------------------------*/

static void v_matchproto_(cli_func_t)
stv_cli_list(struct cli *cli, const char * const *av, void *priv)
{
	struct stevedore *stv;

	ASSERT_MGT();
	(void)av;
	(void)priv;
	VCLI_Out(cli, "Storage devices:\n");
	STV_Foreach(stv)
		VCLI_Out(cli, "\tstorage.%s = %s\n", stv->ident, stv->name);
}

static void v_matchproto_(cli_func_t)
stv_cli_list_json(struct cli *cli, const char * const *av, void *priv)
{
	struct stevedore *stv;
	int n = 0;

	(void)priv;
	ASSERT_MGT();
	VCLI_JSON_begin(cli, 2, av);
	VCLI_Out(cli, ",\n");
	STV_Foreach(stv) {
		VCLI_Out(cli, "%s", n ? ",\n" : "");
		n++;
		VCLI_Out(cli, "{\n");
		VSB_indent(cli->sb, 2);
		VCLI_Out(cli, "\"name\": ");
		VCLI_JSON_str(cli, stv->ident);
		VCLI_Out(cli, ",\n");
		VCLI_Out(cli, "\"storage\": ");
		VCLI_JSON_str(cli, stv->name);
		VSB_indent(cli->sb, -2);
		VCLI_Out(cli, "\n}");
	}
	VCLI_JSON_end(cli);
}

/*--------------------------------------------------------------------*/

static struct cli_proto cli_stv[] = {
	{ CLICMD_STORAGE_LIST,		stv_cli_list, stv_cli_list_json },
	{ NULL}
};

/*--------------------------------------------------------------------
 */

#ifdef WITH_PERSISTENT_STORAGE
static void v_noreturn_ v_matchproto_(storage_init_f)
smp_fake_init(struct stevedore *parent, int ac, char * const *av)
{

	(void)parent;
	(void)ac;
	(void)av;
	ARGV_ERR(
	    "-spersistent has been deprecated, please see:\n"
	    "  https://www.varnish-cache.org/docs/trunk/phk/persistent.html\n"
	    "for details.\n"
	);
}

static const struct stevedore smp_fake_stevedore = {
	.magic = STEVEDORE_MAGIC,
	.name = "persistent",
	.init = smp_fake_init,
};
#endif

/*--------------------------------------------------------------------
 * Register a stevedore implementation by name.
 * VEXTs get to do this first, and since the list is searched front to
 * back a VEXT stevedore which inadvisably wants to steal "default" or
 * the name of another stevedore implementation can do so.
 */

void
STV_Register(const struct stevedore *cstv, const char *altname)
{
	struct stevedore *stv;

	CHECK_OBJ_NOTNULL(cstv, STEVEDORE_MAGIC);
	ALLOC_OBJ(stv, STEVEDORE_MAGIC);
	AN(stv);
	*stv = *cstv;
	if (altname != NULL)
		stv->ident = altname;
	else
		stv->ident = stv->name;
	VTAILQ_INSERT_TAIL(&proto_stevedores, stv, list);
}

static void
STV_Register_The_Usual_Suspects(void)
{
	STV_Register(&smf_stevedore, NULL);
	STV_Register(&sma_stevedore, NULL);
	STV_Register(&smd_stevedore, NULL);
#ifdef WITH_PERSISTENT_STORAGE
	STV_Register(&smp_stevedore, NULL);
	STV_Register(&smp_fake_stevedore, NULL);
#endif
#if defined(HAVE_UMEM_H)
	STV_Register(&smu_stevedore, NULL);
	STV_Register(&smu_stevedore, "default");
#else
	STV_Register(&sma_stevedore, "default");
#endif
}

/*--------------------------------------------------------------------
 * Parse a stevedore argument on the form:
 *     [ name '=' ] strategy [ ',' arg ] *
 */

void
STV_Config(const char *spec)
{
	char **av, buf[8];
	const char *ident;
	struct stevedore *stv;
	static unsigned seq = 0;

	av = MGT_NamedArg(spec, &ident, "-s");
	AN(av);

	if (av[1] == NULL)
		ARGV_ERR("-s argument lacks strategy {malloc, file, ...}\n");

	/* Append strategy to ident string */
	VSB_printf(vident, ",-s%s", av[1]);

	if (ident == NULL) {
		bprintf(buf, "s%u", seq++);
		ident = strdup(buf);
	}

	VTAILQ_FOREACH(stv, &pre_stevedores, list)
		if (!strcmp(stv->ident, ident))
			ARGV_ERR("(-s %s) '%s' is already defined\n",
			    spec, ident);

	ALLOC_OBJ(stv, STEVEDORE_MAGIC);
	AN(stv);
	stv->av = av;
	stv->ident = ident;
	stv->name = av[1];
	VTAILQ_INSERT_TAIL(&pre_stevedores, stv, list);
}

/*--------------------------------------------------------------------*/

void
STV_Config_Final(void)
{
	struct stevedore *stv;
	ASSERT_MGT();

	VCLS_AddFunc(mgt_cls, MCF_AUTH, cli_stv);
	STV_Foreach(stv)
		if (!strcmp(stv->ident, TRANSIENT_STORAGE))
			return;
	STV_Config(TRANSIENT_STORAGE "=default");
}

/*--------------------------------------------------------------------
 * Initialize configured stevedores in the worker process
 */

void
STV_Init(void)
{
	char **av;
	const char *ident;
	struct stevedore *stv;
	const struct stevedore *stv2;
	int ac;

	STV_Register_The_Usual_Suspects();
	while (!VTAILQ_EMPTY(&pre_stevedores)) {
		stv = VTAILQ_FIRST(&pre_stevedores);
		VTAILQ_REMOVE(&pre_stevedores, stv, list);
		CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
		AN(stv->av);
		av = stv->av;
		AN(stv->ident);
		ident = stv->ident;

		for (ac = 0; av[ac + 2] != NULL; ac++)
			continue;

		VTAILQ_FOREACH(stv2, &proto_stevedores, list)
			if (!strcmp(stv2->ident, av[1]))
				break;
		if (stv2 == NULL)
			ARGV_ERR("Unknown stevedore method \"%s\"\n", av[1]);

		CHECK_OBJ_NOTNULL(stv2, STEVEDORE_MAGIC);
		*stv = *stv2;
		AN(stv->name);

		av += 2;

		stv->ident = ident;
		stv->av = av;

		if (stv->init != NULL)
			stv->init(stv, ac, av);
		else if (ac != 0)
			ARGV_ERR("(-s %s) too many arguments\n", stv->name);

		AN(stv->allocobj);
		AN(stv->methods);

		if (!strcmp(stv->ident, TRANSIENT_STORAGE)) {
			AZ(stv_transient);
			stv_transient = stv;
		} else
			VTAILQ_INSERT_TAIL(&stevedores, stv, list);
		/* NB: Do not free av, stevedore gets to keep it */
	}
	AN(stv_transient);
	VTAILQ_INSERT_TAIL(&stevedores, stv_transient, list);
}
