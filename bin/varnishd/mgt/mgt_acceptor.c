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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mgt/mgt.h"
#include "common/heritage.h"

#include "vav.h"
#include "vcli_priv.h"
#include "vsa.h"
#include "vss.h"
#include "vtcp.h"


static int
mac_opensocket(struct listen_sock *ls, struct cli *cli)
{
	int fail;

	CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);
	if (ls->sock > 0) {
		mgt_child_inherit(ls->sock, NULL);
		AZ(close(ls->sock));
	}
	ls->sock = VTCP_bind(ls->addr, NULL);
	fail = errno;
	if (ls->sock < 0) {
		if (cli != NULL)
			VCLI_Out(cli, "Could not get socket %s: %s\n",
			    ls->name, strerror(errno));
		AN(fail);
		return (fail);
	}
	mgt_child_inherit(ls->sock, "sock");
	return (0);
}

/*=====================================================================
 * Reopen the accept sockets to get rid of listen status.
 */

void
MAC_reopen_sockets(struct cli *cli)
{
	struct listen_sock *ls;

	VJ_master(JAIL_MASTER_PRIVPORT);
	VTAILQ_FOREACH(ls, &heritage.socks, list)
		(void)mac_opensocket(ls, cli);
	VJ_master(JAIL_MASTER_LOW);
}

/*=====================================================================
 * Make sure we have all our sockets (and try once more to get them)
 */

int
MAC_sockets_ready(struct cli *cli)
{
	int retval = 1;
	struct listen_sock *ls;

	VJ_master(JAIL_MASTER_PRIVPORT);
	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		if (ls->sock < 0)
			(void)mac_opensocket(ls, cli);
		if (ls->sock < 0)
			retval = 0;
	}
	VJ_master(JAIL_MASTER_LOW);
	return (retval);
}

/*--------------------------------------------------------------------*/

struct mac_help {
	unsigned		magic;
#define MAC_HELP_MAGIC		0x1e00a9d9
	int			good;
	const char		*name;
	const char		*proto_name;
	enum sess_step		first_step;
};

static int __match_proto__(vss_resolved_f)
mac_callback(void *priv, const struct suckaddr *sa)
{
	struct mac_help *mh;
	struct listen_sock *ls;

	CAST_OBJ_NOTNULL(mh, priv, MAC_HELP_MAGIC);

	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		if (!VSA_Compare(sa, ls->addr))
			ARGV_ERR("-a arguments %s and %s have same address\n",
			    ls->name, mh->name);
	}
	ALLOC_OBJ(ls, LISTEN_SOCK_MAGIC);
	AN(ls);
	ls->sock = -1;
	ls->addr = VSA_Clone(sa);
	AN(ls->addr);
	ls->name = strdup(mh->name);
	AN(ls->name);
	ls->proto_name = mh->proto_name;
	ls->first_step = mh->first_step;
	VTAILQ_INSERT_TAIL(&heritage.socks, ls, list);
	mh->good++;
	return (0);
}

void
MAC_Validate(void)
{
	struct listen_sock *ls;
	int fail;
	char abuf[VTCP_ADDRBUFSIZE], pbuf[VTCP_PORTBUFSIZE];
	char nbuf[VTCP_ADDRBUFSIZE+VTCP_PORTBUFSIZE+2];

	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		VJ_master(JAIL_MASTER_PRIVPORT);
		fail = mac_opensocket(ls, NULL);
		VJ_master(JAIL_MASTER_LOW);
		if (fail)
			ARGV_ERR("Cannot open socket: %s: %s\n",
			    ls->name, strerror(fail));
		if (VSA_Port(ls->addr) == 0) {
			/*
			 * If the port number is zero, we adopt whatever
			 * port number this VTCP_bind() found us, as if
			 * specified by argument.
			 */
			free(ls->addr);
			ls->addr = VTCP_my_suckaddr(ls->sock);
			VTCP_myname(ls->sock, abuf, sizeof abuf,
			    pbuf, sizeof pbuf);
			bprintf(nbuf, "%s:%s", abuf, pbuf);
			REPLACE(ls->name, nbuf);
		}
	}
}

void
MAC_Arg(const char *arg)
{
	char **av;
	struct mac_help *mh;
	const char *err;
	int error;

	av = VAV_Parse(arg, NULL, ARGV_COMMA);
	if (av == NULL)
		ARGV_ERR("Parse error: out of memory\n");
	if (av[0] != NULL)
		ARGV_ERR("%s\n", av[0]);

	ALLOC_OBJ(mh, MAC_HELP_MAGIC);
	AN(mh);
	mh->name = av[1];

	if (av[2] == NULL || !strcmp(av[2], "HTTP/1")) {
		mh->first_step = S_STP_H1NEWSESS;
		mh->proto_name = "HTTP/1";
		if (av[2] != NULL && av[3] != NULL)
			ARGV_ERR("Too many sub-arguments to -a(HTTP/1)\n");
	} else if (!strcmp(av[2], "PROXY")) {
		mh->first_step = S_STP_PROXYNEWSESS;
		mh->proto_name = "PROXY";
		if (av[3] != NULL)
			ARGV_ERR("Too many sub-arguments to -a(PROXY)\n");
	} else {
		ARGV_ERR("Unknown protocol '%s'\n", av[2]);
	}

	error = VSS_resolver(av[1], "80", mac_callback, mh, &err);
	if (mh->good == 0 || error)
		ARGV_ERR("socket %s didn't resolve\n", av[1]);
	VAV_Free(av);
	FREE_OBJ(mh);
}
