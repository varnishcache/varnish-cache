/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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
	if (ls->sock >= 0)
		mgt_child_inherit(ls->sock, "sock");
	if (cli != NULL && ls->sock < 0) {
		VCLI_Out(cli, "Could not get socket %s: %s\n",
		    ls->name, strerror(errno));
	}
	return (fail);
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
	const char		*name;
	int			good;
	const char		**err;
};

static int __match_proto__(vss_resolver_f)
mac_callback(void *priv, const struct suckaddr *sa)
{
	struct mac_help *mh;
	struct listen_sock *ls;
	int fail;
	char abuf[VTCP_ADDRBUFSIZE], pbuf[VTCP_PORTBUFSIZE];
	char nbuf[VTCP_ADDRBUFSIZE+VTCP_PORTBUFSIZE+2];

	CAST_OBJ_NOTNULL(mh, priv, MAC_HELP_MAGIC);

	ALLOC_OBJ(ls, LISTEN_SOCK_MAGIC);
	AN(ls);
	ls->sock = -1;
	ls->addr = sa;
	ls->first_step = S_STP_H1NEWSESS;
	fail = mac_opensocket(ls, NULL);
	if (ls->sock < 0) {
		*(mh->err) = strerror(fail);
		FREE_OBJ(ls);
		return (0);
	}
	if (VSA_Port(sa) == 0) {
		/*
		 * If the port number is zero, we adopt whatever port number
		 * this VTCP_bind() found us, as if specified by argument.
		 */
		ls->addr = VTCP_my_suckaddr(ls->sock);
		VTCP_myname(ls->sock, abuf, sizeof abuf, pbuf, sizeof pbuf);
		bprintf(nbuf, "%s:%s", abuf, pbuf);
		ls->name = strdup(nbuf);
	} else {
		ls->addr = VSA_Clone(sa);
		ls->name = strdup(mh->name);
	}
	AN(ls->addr);
	AN(ls->name);
	VTAILQ_INSERT_TAIL(&heritage.socks, ls, list);
	mh->good++;
	return (0);
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
	if (av[2] != NULL)
		ARGV_ERR("XXX: not yet\n");

	ALLOC_OBJ(mh, MAC_HELP_MAGIC);
	AN(mh);
	mh->name = av[1];
	mh->err = &err;
	error = VSS_resolver(av[1], "80", mac_callback, mh, &err);
	if (mh->good == 0 || err != NULL)
		ARGV_ERR("Could not bind to address %s: %s\n", av[1], err);
	AZ(error);
	FREE_OBJ(mh);
}
