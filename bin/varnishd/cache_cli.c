/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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
 * $Id$
 */

#include "config.h"

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
#include "cache.h"
#include "vlu.h"
#include "vsb.h"
#include "heritage.h"

pthread_t	cli_thread;

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
        SZOF(struct bereq);
        SZOF(struct storage);
        SZOF(struct object);
        SZOF(struct objhead);
        SZOF(struct sess);
        SZOF(struct vbe_conn);
}

/*--------------------------------------------------------------------*/

static void
cli_func_start(struct cli *cli, const char * const *av, void *priv)
{

	(void)cli;
	(void)av;
	(void)priv;
	VCA_Init();
	return;
}


/*--------------------------------------------------------------------*/

struct cli_proto CLI_cmds[] = {
	{ CLI_PING,		cli_func_ping },
	{ CLI_SERVER_START,	cli_func_start },
#if 0
	{ CLI_URL_QUERY,	cli_func_url_query },
#endif
	{ CLI_URL_PURGE,	cli_func_url_purge },
	{ CLI_HASH_PURGE,	cli_func_hash_purge },
	{ CLI_VCL_LOAD,		cli_func_config_load },
	{ CLI_VCL_LIST,		cli_func_config_list },
	{ CLI_VCL_DISCARD,	cli_func_config_discard },
	{ CLI_VCL_USE,		cli_func_config_use },

	/* Undocumented functions for debugging */
	{ "debug.sizeof", "debug.sizeof",
		"\tDump sizeof various data structures\n",
		0, 0, cli_debug_sizeof },
	{ NULL }
};

static int
cli_vlu(void *priv, const char *p)
{
	struct cli *cli;
	int i;

	cli = priv;
	VSL(SLT_CLI, 0, "Rd %s", p);
	vsb_clear(cli->sb);
	cli_dispatch(cli, CLI_cmds, p);
	vsb_finish(cli->sb);
	AZ(vsb_overflowed(cli->sb));
	i = cli_writeres(heritage.fds[1], cli);
	if (i)
		VSL(SLT_Error, 0, "CLI write failed (errno=%d)", errno);
	else
		VSL(SLT_CLI, 0, "Wr %d %d %s",
		    i, cli->result, vsb_data(cli->sb));
	return (0);
}

void
CLI_Init(void)
{
	struct pollfd pfd[1];
	struct cli *cli, clis;
	struct vlu *vlu;
	int i;

	cli = &clis;
	memset(cli, 0, sizeof *cli);

	cli_thread = pthread_self();
	cli->sb = vsb_new(NULL, NULL, 0, VSB_AUTOEXTEND);
	XXXAN(cli->sb);
	vlu = VLU_New(cli, cli_vlu, params->cli_buffer);
	XXXAN(vlu);
	printf("Ready\n");
	while (1) {
		pfd[0].fd = heritage.fds[2];
		pfd[0].events = POLLIN;
		i = poll(pfd, 1, 5000);
		if (i == 0) {
			VCL_Idle();
			continue;
		}
		if (pfd[0].revents & POLLHUP) {
			fprintf(stderr,
			    "EOF on CLI connection, exiting\n");
			break;
		}
		i = VLU_Fd(heritage.fds[2], vlu);
		if (i) {
			fprintf(stderr,
			    "Error on CLI connection, exiting "
			    "(VLU_Fd %d ev: %x)\n",
				i, pfd[0].revents);
			break;
		}
	}
}
