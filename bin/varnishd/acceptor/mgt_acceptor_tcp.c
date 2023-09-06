/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2023 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Asad Sajjad Ahmed <asadsa@varnish-software.com>
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
 */

#include "config.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mgt/mgt.h"
#include "common/heritage.h"

#include "acceptor/cache_acceptor.h"
#include "acceptor/acceptor_tcp.h"
#include "acceptor/mgt_acceptor.h"

#include "vav.h"
#include "vcli_serve.h"
#include "vsa.h"
#include "vss.h"
#include "vtcp.h"

int
acc_tcp_config(void)
{

	return (0);
}

static int
acc_tcp_opensocket(struct listen_sock *ls)
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

	AZ(ls->perms);
	MCH_Fd_Inherit(ls->sock, "sock");

	return (0);
}

static int v_matchproto_(vss_resolved_f)
acc_tcp_open_cb(void *priv, const struct suckaddr *sa)
{
	struct listen_arg *la;
	struct listen_sock *ls;
	char abuf[VTCP_ADDRBUFSIZE], pbuf[VTCP_PORTBUFSIZE];
	char nbuf[VTCP_ADDRBUFSIZE+VTCP_PORTBUFSIZE+2];
	int fail;

	CAST_OBJ_NOTNULL(la, priv, LISTEN_ARG_MAGIC);

	VTAILQ_FOREACH(ls, &TCP_acceptor.socks, acclist) {
		CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);

		if (!VSA_Compare(sa, ls->addr))
			ARGV_ERR("-a arguments %s and %s have same address\n",
			    ls->endpoint, la->endpoint);
	}

	ALLOC_OBJ(ls, LISTEN_SOCK_MAGIC);
	AN(ls);

	ls->sock = -1;
	ls->acc = &TCP_acceptor;

	ls->addr = VSA_Clone(sa);
	AN(ls->addr);

	REPLACE(ls->endpoint, la->endpoint);
	ls->name = la->name;
	ls->transport = la->transport;
	ls->perms = la->perms;

	VJ_master(JAIL_MASTER_PRIVPORT);
	fail = acc_tcp_opensocket(ls);
	VJ_master(JAIL_MASTER_LOW);

	if (fail) {
		VSA_free(&ls->addr);
		free(ls->endpoint);
		FREE_OBJ(ls);
		if (fail != EAFNOSUPPORT)
			ARGV_ERR("Could not get socket %s: %s\n",
			    la->endpoint, VAS_errtxt(fail));
		return (0);
	}

	AZ(ls->uds);

	if (VSA_Port(ls->addr) == 0) {
		/*
		 * If the argv port number is zero, we adopt whatever
		 * port number this VTCP_bind() found us, as if
		 * it was specified by the argv.
		 */
		VSA_free(&ls->addr);
		ls->addr = VTCP_my_suckaddr(ls->sock);
		VTCP_myname(ls->sock, abuf, sizeof abuf,
		    pbuf, sizeof pbuf);
		if (VSA_Get_Proto(sa) == AF_INET6)
			bprintf(nbuf, "[%s]:%s", abuf, pbuf);
		else
			bprintf(nbuf, "%s:%s", abuf, pbuf);
		REPLACE(ls->endpoint, nbuf);
	}

	VTAILQ_INSERT_TAIL(&la->socks, ls, arglist);
	VTAILQ_INSERT_TAIL(&heritage.socks, ls, list);
	VTAILQ_INSERT_TAIL(&TCP_acceptor.socks, ls, acclist);

	return (0);
}


int
acc_tcp_open(char **av, struct listen_arg *la, const char **err)
{
	const struct transport *xp = NULL;

	CHECK_OBJ_NOTNULL(la, LISTEN_ARG_MAGIC);
	AN(av);
	AN(err);
	AZ(la->perms);

	if (strchr(la->endpoint, '/') != NULL)
		ARGV_ERR("Unix domain socket addresses must be"
		    " absolute paths in -a (%s)\n", la->endpoint);

	for (int i = 2; av[i] != NULL; i++) {
		if (strchr(av[i], '=') == NULL) {
			if (xp != NULL)
				ARGV_ERR("Too many protocol sub-args"
				    " in -a (%s)\n", av[i]);
			xp = XPORT_Find(av[i]);
			if (xp == NULL)
				ARGV_ERR("Unknown protocol '%s'\n", av[i]);
			continue;
		}

		ARGV_ERR("Invalid sub-arg %s in -a\n", av[i]);
	}

	if (xp == NULL)
		xp = XPORT_Find("http");

	AN(xp);
	la->transport = xp;

	return (VSS_resolver(av[1], "80", acc_tcp_open_cb, la, err));
}

int
acc_tcp_reopen(void)
{
	struct listen_sock *ls;
	int err, fail = 0;

	VTAILQ_FOREACH(ls, &TCP_acceptor.socks, acclist) {
		CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);

		VJ_master(JAIL_MASTER_PRIVPORT);
		err = acc_tcp_opensocket(ls);
		VJ_master(JAIL_MASTER_LOW);

		if (err == 0)
			continue;

		fail = vmax(fail, err);
		MGT_Complain(C_ERR, "Could not reopen listen socket %s: %s",
		    ls->endpoint, VAS_errtxt(err));
	}

	return (fail);
}
