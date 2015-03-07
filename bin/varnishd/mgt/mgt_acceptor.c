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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mgt/mgt.h"
#include "mgt/mgt_param.h"
#include "common/heritage.h"
#include "common/params.h"

#include "vav.h"
#include "vss.h"

/*=====================================================================
 * Open and close the accept sockets.
 *
 * (The child is priv-sep'ed, so it can't do it.)
 */

int
MAC_open_sockets(void)
{
	struct listen_sock *ls;
	int good = 0;

	VJ_master(JAIL_MASTER_HIGH);
	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		if (ls->sock >= 0) {
			good++;
			continue;
		}
		ls->sock = VSS_bind(ls->addr);
		if (ls->sock < 0)
			continue;

		mgt_child_inherit(ls->sock, "sock");

		good++;
	}
	VJ_master(JAIL_MASTER_LOW);
	if (!good)
		return (1);
	return (0);
}

/*--------------------------------------------------------------------*/

void
MAC_close_sockets(void)
{
	struct listen_sock *ls;

	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		if (ls->sock < 0)
			continue;
		mgt_child_inherit(ls->sock, NULL);
		AZ(close(ls->sock));
		ls->sock = -1;
	}
}

/*--------------------------------------------------------------------*/

static void
clean_listen_sock_head(struct listen_sock_head *lsh)
{
	struct listen_sock *ls, *ls2;

	VTAILQ_FOREACH_SAFE(ls, lsh, list, ls2) {
		CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);
		VTAILQ_REMOVE(lsh, ls, list);
		free(ls->name);
		free(ls->addr);
		FREE_OBJ(ls);
	}
}

int
tweak_listen_address(struct vsb *vsb, const struct parspec *par,
    const char *arg)
{
	char **av;
	int i, retval = 0;
	struct listen_sock		*ls;
	struct listen_sock_head		lsh;

	(void)par;
	if (arg == NULL) {
		VSB_quote(vsb, mgt_param.listen_address, -1, 0);
		return (0);
	}

	av = VAV_Parse(arg, NULL, ARGV_COMMA);
	if (av == NULL) {
		VSB_printf(vsb, "Parse error: out of memory");
		return(-1);
	}
	if (av[0] != NULL) {
		VSB_printf(vsb, "Parse error: %s", av[0]);
		VAV_Free(av);
		return(-1);
	}
	if (av[1] == NULL) {
		VSB_printf(vsb, "Empty listen address");
		VAV_Free(av);
		return(-1);
	}
	VTAILQ_INIT(&lsh);
	for (i = 1; av[i] != NULL; i++) {
		struct vss_addr **ta;
		int j, n;

		n = VSS_resolve(av[i], "http", &ta);
		if (n == 0) {
			VSB_printf(vsb, "Invalid listen address ");
			VSB_quote(vsb, av[i], -1, 0);
			retval = -1;
			break;
		}
		for (j = 0; j < n; ++j) {
			ALLOC_OBJ(ls, LISTEN_SOCK_MAGIC);
			AN(ls);
			ls->sock = -1;
			ls->addr = ta[j];
			ls->name = strdup(av[i]);
			AN(ls->name);
			VTAILQ_INSERT_TAIL(&lsh, ls, list);
		}
		free(ta);
	}
	VAV_Free(av);
	if (retval) {
		clean_listen_sock_head(&lsh);
		return (-1);
	}

	REPLACE(mgt_param.listen_address, arg);

	clean_listen_sock_head(&heritage.socks);
	heritage.nsocks = 0;

	while (!VTAILQ_EMPTY(&lsh)) {
		ls = VTAILQ_FIRST(&lsh);
		VTAILQ_REMOVE(&lsh, ls, list);
		CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);
		VTAILQ_INSERT_TAIL(&heritage.socks, ls, list);
		heritage.nsocks++;
	}
	return (0);
}
