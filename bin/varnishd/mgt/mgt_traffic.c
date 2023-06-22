/*-
 * Copyright (c) 2023 Varnish Software AS
 * All rights reserved.
 *
 * Author: Dridi Boukelmoune <dridi.boukelmoune@gmail.com>
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
 */

#include "config.h"

#include "stdlib.h"

#include "mgt/mgt.h"
#include "common/heritage.h"

#include "vcli_serve.h"

static void v_matchproto_(cli_func_t)
mcf_traffic_accept(struct cli *cli, const char * const *av, void *priv)
{
	unsigned status;
	char *p;

	(void)cli;
	(void)av;
	(void)priv;
	if (mgt_param.accept_traffic)
		return;
	if (!MCH_Running()) {
		heritage.param->accept_traffic = mgt_param.accept_traffic = 1;
		return;
	}
	if (MAC_reopen_sockets() || MAC_smuggle_sockets()) {
		VCLI_Out(cli, "Could not accept traffic.\n");
		VCLI_SetResult(cli, CLIS_CANT);
		MAC_close_sockets();
		return;
	}
	if (!mgt_cli_askchild(&status, &p, "traffic.accept\n"))
		heritage.param->accept_traffic = mgt_param.accept_traffic = 1;
	VCLI_SetResult(cli, status);
	if (p != NULL) {
		VCLI_Out(cli, "%s", p);
		free(p);
	}
	MAC_close_sockets();
}

static void v_matchproto_(cli_func_t)
mcf_traffic_refuse(struct cli *cli, const char * const *av, void *priv)
{
	unsigned status;
	char *p;

	(void)cli;
	(void)av;
	(void)priv;
	if (!mgt_param.accept_traffic)
		return;
	heritage.param->accept_traffic = mgt_param.accept_traffic = 0;
	if (!MCH_Running())
		return;
	mgt_cli_askchild(&status, &p, "traffic.refuse\n");
	VCLI_SetResult(cli, status);
	if (p != NULL) {
		VCLI_Out(cli, "%s", p);
		free(p);
	}
}

static void v_matchproto_(cli_func_t)
mcf_traffic_status(struct cli *cli, const char * const *av, void *priv)
{

	(void)av;
	(void)priv;
	VCLI_Out(cli, "Traffic %s\n",
	    mgt_param.accept_traffic ? "accepted" : "refused");
}

static void v_matchproto_(cli_func_t)
mcf_traffic_status_json(struct cli *cli, const char * const *av, void *priv)
{

	(void)priv;
	VCLI_JSON_begin(cli, 2, av);
	VCLI_Out(cli, ",\n  {\"traffic\": %s}",
	    mgt_param.accept_traffic ? "true" : "false");
	VCLI_JSON_end(cli);
}

static struct cli_proto cli_traffic[] = {
	{ CLICMD_TRAFFIC_ACCEPT, "", mcf_traffic_accept },
	{ CLICMD_TRAFFIC_REFUSE, "", mcf_traffic_refuse },
	{ CLICMD_TRAFFIC_STATUS, "", mcf_traffic_status,
		mcf_traffic_status_json },
	{ NULL }
};

void
TRF_Init(void)
{

	AN(mgt_cls);
	mgt_param.accept_traffic = 1;
	VCLS_AddFunc(mgt_cls, MCF_AUTH, cli_traffic);
}
