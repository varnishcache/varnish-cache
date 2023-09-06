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

static VTAILQ_HEAD(,acceptor) acceptors = VTAILQ_HEAD_INITIALIZER(acceptors);

int
ACC__iter(struct acceptor ** const pacc)
{

	AN(pacc);
	CHECK_OBJ_ORNULL(*pacc, ACCEPTOR_MAGIC);
	if (*pacc != NULL)
		*pacc = VTAILQ_NEXT(*pacc, list);
	else
		*pacc = VTAILQ_FIRST(&acceptors);
	return (*pacc != NULL);
}

static struct acceptor *
ACC_Find(const char *name)
{
	struct acceptor *acc;

	ACC_Foreach(acc) {
		CHECK_OBJ_NOTNULL(acc, ACCEPTOR_MAGIC);

		if (!strcmp(acc->name, name))
			return (acc);
	}

	return (NULL);
}

/*=====================================================================
 * Reopen the accept sockets to get rid of listen status.
 * returns the highest errno encountered, 0 for success
 */

int
ACC_reopen_sockets(void)
{
	struct acceptor *acc;
	int fail;
	int err;

	fail = 0;

	ACC_Foreach(acc) {
		CHECK_OBJ_NOTNULL(acc, ACCEPTOR_MAGIC);
		err = acc->reopen();
		fail = vmax(fail, err);
	}

	return (fail);
}

/*--------------------------------------------------------------------*/

void
ACC_Arg(const char *spec)
{
	struct acceptor *acc;
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
		acc = ACC_Find("uds");
	else
		acc = ACC_Find("tcp");

	AN(acc);
	error = acc->open(av, la, &err);

	if (VTAILQ_EMPTY(&la->socks) || error)
		ARGV_ERR("Got no socket(s) for %s\n", av[1]);
	VAV_Free(av);
}

void
ACC_Add(struct acceptor *acc)
{

	CHECK_OBJ_NOTNULL(acc, ACCEPTOR_MAGIC);
	AN(acc->name);
	AN(acc->config);
	AN(acc->init);
	AN(acc->open);
	AN(acc->reopen);
	AN(acc->start);
	AN(acc->event);
	AN(acc->accept);
	AN(acc->update);
	AN(acc->shutdown);

	if (ACC_Find(acc->name) != NULL)
		ARGV_ERR("Acceptor '%s' already exist\n", acc->name);

	VTAILQ_INIT(&acc->socks);
	VTAILQ_INSERT_TAIL(&acceptors, acc, list);

	if (acc->config())
		ARGV_ERR("Acceptor '%s' failed to initialize\n", acc->name);
}

void
ACC_Config(void)
{

	ACC_Add(&TCP_acceptor);
	ACC_Add(&UDS_acceptor);
}
