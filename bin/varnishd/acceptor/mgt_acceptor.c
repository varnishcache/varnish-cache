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

#include "mgt/mgt.h"
#include "acceptor/cache_acceptor.h"
#include "acceptor/mgt_acceptor.h"
#include "common/heritage.h"

#include "vav.h"
#include "vcli_serve.h"
#include "vsa.h"
#include "vss.h"
#include "vtcp.h"
#include "vus.h"

static VTAILQ_HEAD(,listen_arg) listen_args =
    VTAILQ_HEAD_INITIALIZER(listen_args);

/*=====================================================================
 * Reopen the accept sockets to get rid of listen status.
 * returns the highest errno encountered, 0 for success
 */

int
ACC_reopen_sockets(void)
{
	int fail, fail2;

	fail = acc_tcp_open();
	fail2 = acc_uds_open();

	return (vmax(fail, fail2));
}

/*--------------------------------------------------------------------*/

void
ACC_Arg(const char *spec)
{
	char **av;
	struct listen_arg *la;
	const char *err;
	int error;
	const char *name;
	char name_buf[8];
	static unsigned seq = 0;

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

	if (VUS_is(la->endpoint))
		error = acc_uds_open(av[1], la, &err);
	else
		error = acc_tcp_open(av[1], la, &err);

	if (VTAILQ_EMPTY(&la->socks) || error)
		ARGV_ERR("Got no socket(s) for %s\n", av[1]);
	VAV_Free(av);
}
