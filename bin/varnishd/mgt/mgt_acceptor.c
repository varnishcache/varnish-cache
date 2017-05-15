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
 * Acceptor socket management
 */

#include "config.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mgt/mgt.h"
#include "common/heritage.h"

#include "vav.h"
#include "vcli_serve.h"
#include "vsa.h"
#include "vss.h"
#include "vtcp.h"

static VTAILQ_HEAD(,listen_arg) listen_args =
    VTAILQ_HEAD_INITIALIZER(listen_args);

static int
mac_opensocket(struct listen_sock *ls)
{
	int fail;

	CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);
	if (ls->sock > 0) {
		MCH_Fd_Inherit(ls->sock, NULL);
		closefd(&ls->sock);
	}
	ls->sock = VTCP_bind(ls->addr, NULL);
	fail = errno;
	if (ls->sock < 0) {
		AN(fail);
		return (fail);
	}
	MCH_Fd_Inherit(ls->sock, "sock");
	return (0);
}

/*=====================================================================
 * Reopen the accept sockets to get rid of listen status.
 */

void
MAC_reopen_sockets(struct cli *cli)
{
	struct listen_sock *ls;
	int fail;

	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		VJ_master(JAIL_MASTER_PRIVPORT);
		fail = mac_opensocket(ls);
		VJ_master(JAIL_MASTER_LOW);
		if (fail == 0)
			continue;
		if (cli == NULL)
			MGT_Complain(C_ERR,
			    "Could not reopen listen socket %s: %s",
			    ls->name, strerror(fail));
		else
			VCLI_Out(cli,
			    "Could not reopen listen socket %s: %s\n",
			    ls->name, strerror(fail));
	}
}

/*--------------------------------------------------------------------*/

static int __match_proto__(vss_resolved_f)
mac_callback(void *priv, const struct suckaddr *sa)
{
	struct listen_arg *la;
	struct listen_sock *ls;
	char abuf[VTCP_ADDRBUFSIZE], pbuf[VTCP_PORTBUFSIZE];
	char nbuf[VTCP_ADDRBUFSIZE+VTCP_PORTBUFSIZE+2];
	int fail;

	CAST_OBJ_NOTNULL(la, priv, LISTEN_ARG_MAGIC);

	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		if (!VSA_Compare(sa, ls->addr))
			ARGV_ERR("-a arguments %s and %s have same address\n",
			    ls->name, la->name);
	}
	ALLOC_OBJ(ls, LISTEN_SOCK_MAGIC);
	AN(ls);
	ls->sock = -1;
	ls->addr = VSA_Clone(sa);
	AN(ls->addr);
	ls->name = strdup(la->name);
	AN(ls->name);
	ls->transport = la->transport;
	VJ_master(JAIL_MASTER_PRIVPORT);
	fail = mac_opensocket(ls);
	VJ_master(JAIL_MASTER_LOW);
	if (fail) {
		free(ls->addr);
		free(ls->name);
		FREE_OBJ(ls);
		if (fail != EAFNOSUPPORT)
			ARGV_ERR("Could not get socket %s: %s\n",
			    la->name, strerror(fail));
		return(0);
	}
	if (VSA_Port(ls->addr) == 0) {
		/*
		 * If the argv port number is zero, we adopt whatever
		 * port number this VTCP_bind() found us, as if
		 * it was specified by the argv.
		 */
		free(ls->addr);
		ls->addr = VTCP_my_suckaddr(ls->sock);
		VTCP_myname(ls->sock, abuf, sizeof abuf,
		    pbuf, sizeof pbuf);
		bprintf(nbuf, "%s:%s", abuf, pbuf);
		REPLACE(ls->name, nbuf);
	}
	VTAILQ_INSERT_TAIL(&la->socks, ls, arglist);
	VTAILQ_INSERT_TAIL(&heritage.socks, ls, list);
	return (0);
}

void
MAC_Arg(const char *arg)
{
	char **av;
	struct listen_arg *la;
	const char *err;
	int error;
	const struct transport *xp;

	av = VAV_Parse(arg, NULL, ARGV_COMMA);
	if (av == NULL)
		ARGV_ERR("Parse error: out of memory\n");
	if (av[0] != NULL)
		ARGV_ERR("%s\n", av[0]);

	ALLOC_OBJ(la, LISTEN_ARG_MAGIC);
	AN(la);
	VTAILQ_INIT(&la->socks);
	VTAILQ_INSERT_TAIL(&listen_args, la, list);
	la->name = av[1];

	if (av[2] == NULL) {
		xp = XPORT_Find("http/1");
	} else {
		xp = XPORT_Find(av[2]);
		if (xp == NULL)
			ARGV_ERR("Unknown protocol '%s'\n", av[2]);
		if (av[3] != NULL)
			ARGV_ERR("Too many sub-arguments to -a(%s)\n", av[2]);
	}
	AN(xp);
	la->transport = xp;

	error = VSS_resolver(av[1], "80", mac_callback, la, &err);
	if (VTAILQ_EMPTY(&la->socks) || error)
		ARGV_ERR("Got no socket(s) for %s\n", av[1]);
	VAV_Free(av);
}
