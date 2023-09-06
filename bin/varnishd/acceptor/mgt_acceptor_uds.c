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

#include "acceptor/cache_acceptor.h"
#include "acceptor/acceptor_uds.h"
#include "acceptor/mgt_acceptor.h"

#include "vav.h"
#include "vcli_serve.h"
#include "vsa.h"
#include "vss.h"
#include "vus.h"

int
acc_uds_config(void)
{

	return (0);
}

static int
acc_vus_bind(void *priv, const struct sockaddr_un *uds)
{
	return (VUS_bind(uds, priv));
}

static int
acc_uds_opensocket(struct listen_sock *ls)
{
	int fail;
	const char *err;

	CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);

	if (ls->sock > 0) {
		MCH_Fd_Inherit(ls->sock, NULL);
		closefd(&ls->sock);
	}

	ls->sock = VUS_resolver(ls->endpoint, acc_vus_bind, NULL, &err);
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

	MCH_Fd_Inherit(ls->sock, "sock");
	return (0);
}

static int v_matchproto_(vus_resolved_f)
acc_uds_open_cb(void *priv, const struct sockaddr_un *uds)
{
	struct listen_arg *la;
	struct listen_sock *ls;
	int fail;

	CAST_OBJ_NOTNULL(la, priv, LISTEN_ARG_MAGIC);
	(void) uds;

	VTAILQ_FOREACH(ls, &UDS_acceptor.socks, acclist) {
		CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);

		if (strcmp(ls->endpoint, la->endpoint) == 0)
			ARGV_ERR("-a arguments %s and %s have same address\n",
			    ls->endpoint, la->endpoint);
	}

	ALLOC_OBJ(ls, LISTEN_SOCK_MAGIC);
	AN(ls);

	ls->sock = -1;
	ls->acc = &UDS_acceptor;

	ls->addr = VSA_Clone(bogo_ip);
	AN(ls->addr);

	REPLACE(ls->endpoint, la->endpoint);
	ls->name = la->name;
	ls->transport = la->transport;
	ls->perms = la->perms;
	ls->uds = 1;

	VJ_master(JAIL_MASTER_PRIVPORT);
	fail = acc_uds_opensocket(ls);
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

	VTAILQ_INSERT_TAIL(&la->socks, ls, arglist);
	VTAILQ_INSERT_TAIL(&heritage.socks, ls, list);
	VTAILQ_INSERT_TAIL(&UDS_acceptor.socks, ls, acclist);

	return (0);
}

int
acc_uds_open(char **av, struct listen_arg *la, const char **err)
{

	const struct transport *xp = NULL;
	struct passwd *pwd = NULL;
	struct group *grp = NULL;
	struct uds_perms *perms;
	mode_t mode = 0;

	CHECK_OBJ_NOTNULL(la, LISTEN_ARG_MAGIC);
	AN(av);
	AN(err);

	if (*la->endpoint != '/' && strchr(la->endpoint, '/') != NULL)
		ARGV_ERR("Unix domain socket addresses must be"
		    " absolute paths in -a (%s)\n", la->endpoint);

	heritage.min_vcl_version = vmax(heritage.min_vcl_version, 41U);

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

	return (VUS_resolver(la->endpoint, acc_uds_open_cb, la, err));
}

int
acc_uds_reopen(void)
{
	struct listen_sock *ls;
	int err, fail = 0;

	VTAILQ_FOREACH(ls, &UDS_acceptor.socks, acclist) {
		CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);

		VJ_master(JAIL_MASTER_PRIVPORT);
		err = acc_uds_opensocket(ls);
		VJ_master(JAIL_MASTER_LOW);

		if (err == 0)
			continue;

		fail = vmax(fail, err);
		MGT_Complain(C_ERR, "Could not reopen listen socket %s: %s",
		    ls->endpoint, VAS_errtxt(err));
	}

	return (fail);
}
