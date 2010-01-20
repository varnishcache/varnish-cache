/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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
 * Caching process CLI handling.
 *
 * We only have one CLI source, the stdin/stdout pipes from the manager
 * process, but we complicate things by having undocumented commands that
 * we do not want to show in a plain help, and by having commands that the
 * manager has already shown in help before asking us.
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

#include "shmlog.h"
#include "cli.h"
#include "cli_priv.h"
#include "cli_common.h"
#include "cli_serve.h"
#include "cache.h"
#include "vsb.h"
#include "hash_slinger.h"

pthread_t		cli_thread;
static struct lock	cli_mtx;
static int		add_check;

/*
 * The CLI commandlist is split in three:
 *  - Commands we get from/share with the manager, we don't show these
 *	in help, as the manager already did that.
 *  - Cache process commands, show in help
 *  - Undocumented debug commands, show in undocumented "help -d"
 */

static struct cli_proto *ccf_master_cli, *ccf_public_cli, *ccf_debug_cli;

/*--------------------------------------------------------------------
 * Add CLI functions to the appropriate command set
 */

void
CLI_AddFuncs(enum cli_set_e which, struct cli_proto *p)
{
	struct cli_proto *c, **cp;

	AZ(add_check);
	switch (which) {
	case MASTER_CLI: cp = &ccf_master_cli; break;
	case PUBLIC_CLI: cp = &ccf_public_cli; break;
	case DEBUG_CLI:	 cp = &ccf_debug_cli;  break;
	default: INCOMPL();
	}
	Lck_Lock(&cli_mtx);
	c = cli_concat(*cp, p);
	AN(c);
	free(*cp);
	*cp = c;
	Lck_Unlock(&cli_mtx);
}

static void
cli_cb_before(const struct cli *cli)
{

	VSL(SLT_CLI, 0, "Rd %s", cli->cmd);
	VCL_Poll();
	VBE_Poll();
	Lck_Lock(&cli_mtx);
}

static void
cli_cb_after(const struct cli *cli)
{
	Lck_Unlock(&cli_mtx);
	VSL(SLT_CLI, 0, "Wr %03u %s", cli->result, vsb_data(cli->sb));
}

void
CLI_Run(void)
{
	struct cls	*cls;
	int i;

	add_check = 1;

	cls = CLS_New(cli_cb_before, cli_cb_after, params->cli_buffer);
	AN(cls);
	AN(CLS_AddFd(cls, heritage.cli_in, heritage.cli_out, NULL, NULL));
	AZ(CLS_AddFunc(cls, 0, ccf_master_cli));
	AZ(CLS_AddFunc(cls, 0, ccf_public_cli));
	AZ(CLS_AddFunc(cls, 0, ccf_debug_cli));

	do {
		i = CLS_Poll(cls, -1);
	} while(i > 0);
	VSL(SLT_CLI, 0, "EOF on CLI connection, worker stops");
}

/*--------------------------------------------------------------------*/

static void
cli_debug_sizeof(struct cli *cli, const char * const *av, void *priv)
{
	(void)av;
	(void)priv;

#define SZOF(foo)       cli_out(cli, \
    "sizeof(%s) = %zd = 0x%zx\n", #foo, sizeof(foo), sizeof(foo));
	SZOF(struct ws);
	SZOF(struct http);
	SZOF(struct http_conn);
	SZOF(struct acct);
	SZOF(struct worker);
	SZOF(struct workreq);
	SZOF(struct storage);
	SZOF(struct object);
	SZOF(struct objcore);
	SZOF(struct objhead);
	SZOF(struct sess);
	SZOF(struct vbe_conn);
	SZOF(struct varnish_stats);
	SZOF(struct lock);
}

/*--------------------------------------------------------------------*/

static void
ccf_help(struct cli *cli, const char * const *av, void *priv)
{

	(void)priv;
	cli_func_help(cli, av, ccf_public_cli);

	if (av[2] != NULL && !strcmp(av[2], "-d")) {
		/* Also list undocumented commands */
		cli_out(cli, "\nDebugging commands:\n");
		cli_func_help(cli, av, ccf_debug_cli);
	} else if (cli->result == CLIS_UNKNOWN) {
		/* Otherwise, try the undocumented list */
		vsb_clear(cli->sb);
		cli->result = CLIS_OK;
		cli_func_help(cli, av, ccf_debug_cli);
	}
}

/*--------------------------------------------------------------------*/

static void
ccf_panic(struct cli *cli, const char * const *av, void *priv)
{

	(void)cli;
	(void)av;
	(void)priv;
	assert(!strcmp("", "You asked for it"));
}

/*--------------------------------------------------------------------*/

static struct cli_proto master_cmds[] = {
	{ CLI_PING,		cli_func_ping },
	{ CLI_HELP,             ccf_help, NULL },
	{ NULL }
};

static struct cli_proto debug_cmds[] = {
	{ "debug.sizeof", "debug.sizeof",
		"\tDump sizeof various data structures\n",
		0, 0, cli_debug_sizeof },
	{ "debug.panic.worker", "debug.panic.worker",
		"\tPanic the worker process.\n",
		0, 0, ccf_panic },
	{ NULL }
};


/*--------------------------------------------------------------------
 * Initialize the CLI subsystem
 */

void
CLI_Init(void)
{

	Lck_New(&cli_mtx);
	cli_thread = pthread_self();

	CLI_AddFuncs(MASTER_CLI, master_cmds);
	CLI_AddFuncs(DEBUG_CLI, debug_cmds);
}

