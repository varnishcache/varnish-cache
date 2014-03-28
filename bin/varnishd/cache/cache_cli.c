/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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

#include <stddef.h>			// offsetof

#include "cache.h"
#include "common/heritage.h"

#include "cache_backend.h"		// struct vbc
#include "hash/hash_slinger.h"		// struct objhead
#include "vsa.h"
#include "vcli.h"
#include "vcli_common.h"
#include "vcli_priv.h"
#include "vcli_serve.h"

pthread_t		cli_thread;
static struct lock	cli_mtx;
static int		add_check;
static struct VCLS	*cls;

/*
 * The CLI commandlist is split in three:
 *  - Commands we get from/share with the manager, we don't show these
 *	in help, as the manager already did that.
 *  - Cache process commands, show in help
 *  - Undocumented debug commands, show in undocumented "help -d"
 */

/*--------------------------------------------------------------------
 * Add CLI functions to the appropriate command set
 */

void
CLI_AddFuncs(struct cli_proto *p)
{

	AZ(add_check);
	Lck_Lock(&cli_mtx);
	AZ(VCLS_AddFunc(cls, 0, p));
	Lck_Unlock(&cli_mtx);
}

static void
cli_cb_before(const struct cli *cli)
{

	ASSERT_CLI();
	VSL(SLT_CLI, 0, "Rd %s", cli->cmd);
	VCL_Poll();
	VBE_Poll();
	Lck_Lock(&cli_mtx);
}

static void
cli_cb_after(const struct cli *cli)
{

	ASSERT_CLI();
	Lck_Unlock(&cli_mtx);
	VSL(SLT_CLI, 0, "Wr %03u %zd %s",
	    cli->result, VSB_len(cli->sb), VSB_data(cli->sb));
}

void
CLI_Run(void)
{
	int i;

	add_check = 1;

	AN(VCLS_AddFd(cls, heritage.cli_in, heritage.cli_out, NULL, NULL));

	do {
		i = VCLS_Poll(cls, -1);
	} while(i > 0);
	VSL(SLT_CLI, 0, "EOF on CLI connection, worker stops");
	VCA_Shutdown();
}

/*--------------------------------------------------------------------*/

static void
cli_debug_sizeof(struct cli *cli, const char * const *av, void *priv)
{
	(void)av;
	(void)priv;

#define SZOF(foo)       VCLI_Out(cli, \
    "sizeof(%s) = %zd = 0x%zx\n", #foo, sizeof(foo), sizeof(foo))
	SZOF(struct ws);
	SZOF(struct http);
	SZOF(struct http_conn);
	SZOF(struct acct_req);
	SZOF(struct worker);
	SZOF(struct wrk_accept);
	SZOF(struct storage);
	SZOF(struct busyobj);
	SZOF(struct object);
	SZOF(struct objcore);
	SZOF(struct objhead);
	SZOF(struct sess);
	SZOF(struct req);
	SZOF(struct vbc);
	SZOF(struct VSC_C_main);
	SZOF(struct lock);
	SZOF(struct dstat);
	VCLI_Out(cli, "sizeof(struct suckaddr) = %d = 0x%x\n",
	    vsa_suckaddr_len, vsa_suckaddr_len);
#if 0
#define OFOF(foo, bar)	{ foo __foo; VCLI_Out(cli, \
    "%-30s = 0x%4zx @ 0x%4zx\n", \
	#foo "." #bar, sizeof(__foo.bar), offsetof(foo, bar)); }
#if 0
	OFOF(struct objhead, magic);
	OFOF(struct objhead, refcnt);
	OFOF(struct objhead, mtx);
	OFOF(struct objhead, objcs);
	OFOF(struct objhead, digest);
	OFOF(struct objhead, waitinglist);
	OFOF(struct objhead, _u);
#endif
#if 0
	OFOF(struct http, magic);
	OFOF(struct http, logtag);
	OFOF(struct http, ws);
	OFOF(struct http, hd);
	OFOF(struct http, hdf);
	OFOF(struct http, shd);
	OFOF(struct http, nhd);
	OFOF(struct http, status);
	OFOF(struct http, protover);
	OFOF(struct http, conds);
#endif
#if 0
	OFOF(struct storage, magic);
	OFOF(struct storage, fd);
	OFOF(struct storage, where);
	OFOF(struct storage, list);
	OFOF(struct storage, stevedore);
	OFOF(struct storage, priv);
	OFOF(struct storage, ptr);
	OFOF(struct storage, len);
	OFOF(struct storage, space);
#endif
#if 0
	OFOF(struct object, magic);
	OFOF(struct object, xid);
	OFOF(struct object, objstore);
	OFOF(struct object, objcore);
	OFOF(struct object, ws_o);
	OFOF(struct object, vary);
	OFOF(struct object, hits);
	OFOF(struct object, response);
	OFOF(struct object, gziped);
	OFOF(struct object, gzip_start);
	OFOF(struct object, gzip_last);
	OFOF(struct object, gzip_stop);
	OFOF(struct object, len);
	OFOF(struct object, age);
	OFOF(struct object, entered);
	OFOF(struct object, exp);
	OFOF(struct object, last_modified);
	OFOF(struct object, last_lru);
	OFOF(struct object, http);
	OFOF(struct object, store);
	OFOF(struct object, esidata);
#endif
#undef OFOF
#endif
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
	{ CLI_PING,		"i", VCLS_func_ping },
	{ CLI_HELP,             "i", VCLS_func_help },
	{ "debug.sizeof", "debug.sizeof",
		"\tDump sizeof various data structures\n",
		0, 0, "d", cli_debug_sizeof },
	{ "debug.panic.worker", "debug.panic.worker",
		"\tPanic the worker process.\n",
		0, 0, "d", ccf_panic },
	{ NULL }
};

/*--------------------------------------------------------------------
 * Initialize the CLI subsystem
 */

void
CLI_Init(void)
{

	Lck_New(&cli_mtx, lck_cli);
	cli_thread = pthread_self();

	cls = VCLS_New(cli_cb_before, cli_cb_after,
	    &cache_param->cli_buffer, &cache_param->cli_limit);
	AN(cls);

	CLI_AddFuncs(master_cmds);
}
