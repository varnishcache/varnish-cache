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
#include "acceptor/cache_acceptor.h"	// XXX mgt_acceptor should not use
#include "acceptor/mgt_acceptor.h"
#include "common/heritage.h"

#include "vav.h"
#include "vus.h"

static VTAILQ_HEAD(,listen_arg) listen_args =
    VTAILQ_HEAD_INITIALIZER(listen_args);

static VTAILQ_HEAD(,acceptor) acceptors = VTAILQ_HEAD_INITIALIZER(acceptors);

int
VCA__iter(struct acceptor ** const pvca)
{

	AN(pvca);
	CHECK_OBJ_ORNULL(*pvca, ACCEPTOR_MAGIC);
	if (*pvca != NULL)
		*pvca = VTAILQ_NEXT(*pvca, list);
	else
		*pvca = VTAILQ_FIRST(&acceptors);
	return (*pvca != NULL);
}

static struct acceptor *
VCA_Find(const char *name)
{
	struct acceptor *vca;

	VCA_Foreach(vca) {
		CHECK_OBJ_NOTNULL(vca, ACCEPTOR_MAGIC);

		if (!strcmp(vca->name, name))
			return (vca);
	}

	return (NULL);
}

/*=====================================================================
 * Reopen the accept sockets to get rid of listen status.
 * returns the highest errno encountered, 0 for success
 */

int
VCA_reopen_sockets(void)
{
	struct acceptor *vca;
	int fail;
	int err;

	fail = 0;

	VCA_Foreach(vca) {
		CHECK_OBJ_NOTNULL(vca, ACCEPTOR_MAGIC);
		err = vca->reopen();
		fail = vmax(fail, err);
	}

	return (fail);
}

/*--------------------------------------------------------------------
 * [name=][%kind,][listen_address[,PROTO|,option=value,...]]
 */

void
VCA_Arg(const char *spec)
{
	struct acceptor *vca = NULL;
	char **av, **ap;
	struct listen_arg *la;
	const char *err;
	int error;
	const char *name;
	char name_buf[8];
	static unsigned seq = 0;

	av = MGT_NamedArg(spec, &name, "-a");
	AN(av);
	ap = av + 1;

	ALLOC_OBJ(la, LISTEN_ARG_MAGIC);
	AN(la);
	VTAILQ_INIT(&la->socks);
	VTAILQ_INSERT_TAIL(&listen_args, la, list);

	if (ap[0] != NULL && ap[0][0] == '%') {
		vca = VCA_Find(ap[0] + 1);
		if (vca == NULL)
			ARGV_ERR("Acceptor %s not found\n", ap[0] + 1);
		ap++;
	}

	la->endpoint = *(ap++);

	if (name == NULL) {
		bprintf(name_buf, "a%u", seq++);
		name = strdup(name_buf);
		AN(name);
	}

	la->name = name;

	if (vca == NULL && VUS_is(la->endpoint))
		vca = VCA_Find("uds");
	else if (vca == NULL)
		vca = VCA_Find("tcp");

	AN(vca);
	error = vca->open(ap, la, &err);

	if (error) {
		ARGV_ERR("Got no socket(s) for %s=%s (%s)\n",
		    la->name, la->endpoint, err);
	}
	else if (VTAILQ_EMPTY(&la->socks)) {
		ARGV_ERR("Got no socket(s) for %s=%s\n",
		    la->name, la->endpoint);
	}
	VAV_Free(av);
}

void
VCA_Add(struct acceptor *vca)
{

	CHECK_OBJ_NOTNULL(vca, ACCEPTOR_MAGIC);
	AN(vca->name);
	AN(vca->config);
	AN(vca->init);
	AN(vca->open);
	AN(vca->reopen);
	AN(vca->start);
	AN(vca->event);
	AN(vca->accept);
	AN(vca->update);
	AN(vca->shutdown);

	if (VCA_Find(vca->name) != NULL)
		ARGV_ERR("Acceptor '%s' already exist\n", vca->name);

	VTAILQ_INIT(&vca->socks);
	VTAILQ_INSERT_TAIL(&acceptors, vca, list);

	if (vca->config())
		ARGV_ERR("Acceptor '%s' failed to initialize\n", vca->name);
}

void
VCA_Config(void)
{

	VCA_Add(&TCP_acceptor);
	VCA_Add(&UDS_acceptor);
}
