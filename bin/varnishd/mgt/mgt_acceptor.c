/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 * Acceptor socket management
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
#include <pwd.h>
#include <grp.h>

#include "mgt/mgt.h"
#include "common/heritage.h"

#include "vav.h"
#include "vcli_serve.h"
#include "vsa.h"
#include "vss.h"
#include "vtcp.h"
#include "vus.h"

struct listen_arg {
	unsigned			magic;
#define LISTEN_ARG_MAGIC		0xbb2fc333
	VTAILQ_ENTRY(listen_arg)	list;
	const char			*endpoint;
	const char			*name;
	VTAILQ_HEAD(,listen_sock)	socks;
	const struct transport		*transport;
	const struct uds_perms		*perms;
};

struct uds_perms {
	unsigned	magic;
#define UDS_PERMS_MAGIC 0x84fb5635
	mode_t		mode;
	uid_t		uid;
	gid_t		gid;
};

static VTAILQ_HEAD(,listen_arg) listen_args =
    VTAILQ_HEAD_INITIALIZER(listen_args);

static void
mac_closesocket(struct listen_sock *ls)
{

	CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);
	if (ls->sock < 0)
		return;
	closefd(&ls->sock);
}

static int
mac_vus_bind(void *priv, const struct sockaddr_un *uds)
{
	return (VUS_bind(uds, priv));
}

static int
mac_opensocket(struct listen_sock *ls)
{
	int fail;
	const char *err;

	CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);
	assert(ls->sock < 0);
	if (!ls->uds)
		ls->sock = VTCP_bind(ls->addr, NULL);
	else
		ls->sock = VUS_resolver(ls->endpoint, mac_vus_bind, NULL, &err);
	fail = errno;
	if (ls->sock < 0) {
		AN(fail);
		return (fail);
	}
	if (ls->perms != NULL) {
		CHECK_OBJ(ls->perms, UDS_PERMS_MAGIC);
		assert(ls->uds);
		errno = 0;
		if (ls->perms->mode != 0 &&
		    chmod(ls->endpoint, ls->perms->mode) != 0)
			return (errno);
		if (chown(ls->endpoint, ls->perms->uid, ls->perms->gid) != 0)
			return (errno);
	}
	return (0);
}

/*=====================================================================
 * Reopen the accept sockets to get rid of listen status.
 * returns the highest errno encountered, 0 for success
 */

int
MAC_reopen_sockets(void)
{
	struct listen_sock *ls;
	int err, fail = 0;

	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		CHECK_OBJ(ls, LISTEN_SOCK_MAGIC);
		mac_closesocket(ls);
		VJ_master(JAIL_MASTER_PRIVPORT);
		err = mac_opensocket(ls);
		VJ_master(JAIL_MASTER_LOW);
		if (err == 0)
			continue;
		fail = vmax(fail, err);
		MGT_Complain(C_ERR,
		    "Could not reopen listen socket %s: %s",
		    ls->endpoint, VAS_errtxt(err));
	}
	return (fail);
}

/*--------------------------------------------------------------------*/

void
MAC_close_sockets(void)
{
	struct listen_sock *ls;

	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		CHECK_OBJ(ls, LISTEN_SOCK_MAGIC);
		mac_closesocket(ls);
	}
}

/*--------------------------------------------------------------------*/

int
MAC_smuggle_sockets(void)
{
	struct listen_sock *ls;

	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		CHECK_OBJ(ls, LISTEN_SOCK_MAGIC);
		assert(ls->sock > 0);
		AN(ls->nonce);
		AZ(*ls->nonce);
		*ls->nonce = mgt_smuggle(ls->sock);
		if (*ls->nonce == 0)
			break;
	}

	if (ls == NULL) {
		MAC_close_sockets();
		return (0);
	}

	MGT_Complain(C_ERR, "Could not smuggle listen socket %d: (%s)",
	    ls->sock, ls->endpoint);

	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		if (*ls->nonce != 0) {
			assert(ls->sock > 0);
			XXXAZ(mgt_SMUG_Cancel(*ls->nonce));
			*ls->nonce = 0;
		}
	}

	return (-1);
}

/*--------------------------------------------------------------------*/

static struct listen_sock *
mk_listen_sock(const struct listen_arg *la, const struct suckaddr *sa)
{
	struct listen_sock *ls;
	int fail;

	ALLOC_OBJ(ls, LISTEN_SOCK_MAGIC);
	AN(ls);
	ls->sock = -1;
	ls->addr = VSA_Clone(sa);
	AN(ls->addr);
	REPLACE(ls->endpoint, la->endpoint);
	ls->name = la->name;
	ls->transport = la->transport;
	ls->perms = la->perms;
	ls->uds = VUS_is(la->endpoint);
	VJ_master(JAIL_MASTER_PRIVPORT);
	fail = mac_opensocket(ls);
	VJ_master(JAIL_MASTER_LOW);
	if (fail) {
		VSA_free(&ls->addr);
		free(ls->endpoint);
		FREE_OBJ(ls);
		if (fail != EAFNOSUPPORT)
			ARGV_ERR("Could not get socket %s: %s\n",
			    la->endpoint, VAS_errtxt(fail));
		return (NULL);
	}
	return (ls);
}

static int v_matchproto_(vss_resolved_f)
mac_tcp(void *priv, const struct suckaddr *sa)
{
	struct listen_arg *la;
	struct listen_sock *ls;
	char abuf[VTCP_ADDRBUFSIZE], pbuf[VTCP_PORTBUFSIZE];
	char nbuf[VTCP_ADDRBUFSIZE+VTCP_PORTBUFSIZE+2];

	CAST_OBJ_NOTNULL(la, priv, LISTEN_ARG_MAGIC);

	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		if (!ls->uds && !VSA_Compare(sa, ls->addr))
			ARGV_ERR("-a arguments %s and %s have same address\n",
			    ls->endpoint, la->endpoint);
	}
	ls = mk_listen_sock(la, sa);
	if (ls == NULL)
		return (0);
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
	return (0);
}

static int v_matchproto_(vus_resolved_f)
mac_uds(void *priv, const struct sockaddr_un *uds)
{
	struct listen_arg *la;
	struct listen_sock *ls;

	CAST_OBJ_NOTNULL(la, priv, LISTEN_ARG_MAGIC);
	(void) uds;

	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		if (ls->uds && strcmp(ls->endpoint, la->endpoint) == 0)
			ARGV_ERR("-a arguments %s and %s have same address\n",
			    ls->endpoint, la->endpoint);
	}
	ls = mk_listen_sock(la, bogo_ip);
	if (ls == NULL)
		return (0);
	AN(ls->uds);
	VTAILQ_INSERT_TAIL(&la->socks, ls, arglist);
	VTAILQ_INSERT_TAIL(&heritage.socks, ls, list);
	return (0);
}

void
MAC_Arg(const char *spec)
{
	char **av;
	struct listen_arg *la;
	const char *err;
	int error;
	const struct transport *xp = NULL;
	const char *name;
	char name_buf[8];
	static unsigned seq = 0;
	struct passwd *pwd = NULL;
	struct group *grp = NULL;
	mode_t mode = 0;
	struct uds_perms *perms;

	av = MGT_NamedArg(spec, &name, "-a");
	AN(av);

	ALLOC_OBJ(la, LISTEN_ARG_MAGIC);
	AN(la);
	VTAILQ_INIT(&la->socks);
	VTAILQ_INSERT_TAIL(&listen_args, la, list);
	la->endpoint = av[1];

	if (name == NULL) {
		bprintf(name_buf, "a%u", seq++);
		name = strdup(name_buf);
		AN(name);
	}
	la->name = name;

	if (*la->endpoint != '/' && strchr(la->endpoint, '/') != NULL)
		ARGV_ERR("Unix domain socket addresses must be"
		    " absolute paths in -a (%s)\n", la->endpoint);

	if (VUS_is(la->endpoint) && heritage.min_vcl_version < 41)
		heritage.min_vcl_version = 41;

	for (int i = 2; av[i] != NULL; i++) {
		char *eq, *val;
		int len;

		if ((eq = strchr(av[i], '=')) == NULL) {
			if (xp != NULL)
				ARGV_ERR("Too many protocol sub-args"
				    " in -a (%s)\n", av[i]);
			xp = XPORT_Find(av[i]);
			if (xp == NULL)
				ARGV_ERR("Unknown protocol '%s'\n", av[i]);
			continue;
		}
		if (la->endpoint[0] != '/')
			ARGV_ERR("Invalid sub-arg %s"
			    " in -a\n", av[i]);

		val = eq + 1;
		len = eq - av[i];
		assert(len >= 0);
		if (len == 0)
			ARGV_ERR("Invalid sub-arg %s in -a\n", av[i]);

		if (strncmp(av[i], "user", len) == 0) {
			if (pwd != NULL)
				ARGV_ERR("Too many user sub-args in -a (%s)\n",
					 av[i]);
			pwd = getpwnam(val);
			if (pwd == NULL)
				ARGV_ERR("Unknown user %s in -a\n", val);
			continue;
		}

		if (strncmp(av[i], "group", len) == 0) {
			if (grp != NULL)
				ARGV_ERR("Too many group sub-args in -a (%s)\n",
					 av[i]);
			grp = getgrnam(val);
			if (grp == NULL)
				ARGV_ERR("Unknown group %s in -a\n", val);
			continue;
		}

		if (strncmp(av[i], "mode", len) == 0) {
			long m;
			char *p;

			if (mode != 0)
				ARGV_ERR("Too many mode sub-args in -a (%s)\n",
					 av[i]);
			if (*val == '\0')
				ARGV_ERR("Empty mode sub-arg in -a\n");
			errno = 0;
			m = strtol(val, &p, 8);
			if (*p != '\0')
				ARGV_ERR("Invalid mode sub-arg %s in -a\n",
					 val);
			if (errno)
				ARGV_ERR("Cannot parse mode sub-arg %s in -a: "
					 "%s\n", val, VAS_errtxt(errno));
			if (m <= 0 || m > 0777)
				ARGV_ERR("Mode sub-arg %s out of range in -a\n",
					 val);
			mode = (mode_t) m;
			continue;
		}

		ARGV_ERR("Invalid sub-arg %s in -a\n", av[i]);
	}

	if (xp == NULL)
		xp = XPORT_Find("http");
	AN(xp);
	la->transport = xp;

	if (pwd != NULL || grp != NULL || mode != 0) {
		ALLOC_OBJ(perms, UDS_PERMS_MAGIC);
		AN(perms);
		if (pwd != NULL)
			perms->uid = pwd->pw_uid;
		else
			perms->uid = (uid_t) -1;
		if (grp != NULL)
			perms->gid = grp->gr_gid;
		else
			perms->gid = (gid_t) -1;
		perms->mode = mode;
		la->perms = perms;
	}
	else
		AZ(la->perms);

	if (VUS_is(la->endpoint))
		error = VUS_resolver(av[1], mac_uds, la, &err);
	else
		error = VSS_resolver(av[1], "80", mac_tcp, la, &err);

	if (VTAILQ_EMPTY(&la->socks) || error)
		ARGV_ERR("Got no socket(s) for %s\n", av[1]);
	VAV_Free(av);
}
