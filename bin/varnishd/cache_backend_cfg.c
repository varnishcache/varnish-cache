/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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
 * $Id$
 *
 * Handle configuration of backends from VCL programs.
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

#include <sys/socket.h>
#include <netdb.h>

#include "shmlog.h"
#include "cache.h"
#include "vrt.h"
#include "cache_backend.h"
#include "cli_priv.h"

MTX VBE_mtx;

/*
 * The list of backends is not locked, it is only ever accessed from
 * the CLI thread, so there is no need.
 */
static VTAILQ_HEAD(, backend) backends = VTAILQ_HEAD_INITIALIZER(backends);

/*--------------------------------------------------------------------*/

void
VBE_SelectBackend(struct sess *sp)
{
	struct backend *bp;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->director, DIRECTOR_MAGIC);
	bp = sp->director->choose(sp);
	CHECK_OBJ_NOTNULL(bp, BACKEND_MAGIC);
	sp->backend = bp;
}

/*--------------------------------------------------------------------
 * Drop a reference to a backend.
 * The last reference must come from the watcher in the CLI thread,
 * as only that thread is allowed to clean up the backend list.
 */

void
VBE_DropRefLocked(struct backend *b)
{
	int i;
	struct vbe_conn *vbe, *vbe2;

	CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);
	assert(b->refcount > 0);

	i = --b->refcount;
	UNLOCK(&b->mtx);
	if (i > 0)
		return;

	ASSERT_CLI();
	VTAILQ_REMOVE(&backends, b, list);
	VTAILQ_FOREACH_SAFE(vbe, &b->connlist, list, vbe2) {
		VTAILQ_REMOVE(&b->connlist, vbe, list);
		if (vbe->fd >= 0)
			AZ(close(vbe->fd));
		VBE_ReleaseConn(vbe);
	}
	free(TRUST_ME(b->vrt->ident));
	free(TRUST_ME(b->vrt->hostname));
	free(TRUST_ME(b->vrt->portname));
	b->magic = 0;
	free(b);
	VSL_stats->n_backend--;
}

void
VBE_DropRef(struct backend *b)
{

	CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);

	LOCK(&b->mtx);
	VBE_DropRefLocked(b);
}

/*--------------------------------------------------------------------
 * DNS lookup of backend host/port
 */

static void
vbe_dns_lookup(const struct cli *cli, struct backend *bp)
{
	int error;
	struct addrinfo *res, hint, *old;

	CHECK_OBJ_NOTNULL(bp, BACKEND_MAGIC);

	memset(&hint, 0, sizeof hint);
	hint.ai_family = PF_UNSPEC;
	hint.ai_socktype = SOCK_STREAM;
	res = NULL;
	error = getaddrinfo(bp->vrt->hostname, bp->vrt->portname,
	    &hint, &res);
	if (error) {
		if (res != NULL)
			freeaddrinfo(res);
		/*
		 * We cannot point to the source code any more, it may
		 * be long gone from memory.   We already checked over in
		 * the VCL compiler, so this is only relevant for refreshes.
		 * XXX: which we do when exactly ?
		 */
		cli_out(cli, "DNS(/hosts) lookup failed for (%s/%s): %s",
		    bp->vrt->hostname, bp->vrt->portname, gai_strerror(error));
		return;
	}
	LOCK(&bp->mtx);
	old = bp->ai;
	bp->ai = res;
	bp->last_ai = res;
	UNLOCK(&bp->mtx);
	if (old != NULL)
		freeaddrinfo(old);
}

/*--------------------------------------------------------------------
 * Add a backend/director instance when loading a VCL.
 * If an existing backend is matched, grab a refcount and return.
 * Else create a new backend structure with reference initialized to one.
 */

struct backend *
VBE_AddBackend(struct cli *cli, const struct vrt_backend *vb)
{
	struct backend *b;
	uint32_t u;

	AN(vb->hostname);
	AN(vb->portname);
	AN(vb->ident);
	(void)cli;
	ASSERT_CLI();
	u = crc32_l(vb->ident, strlen(vb->ident));
	VTAILQ_FOREACH(b, &backends, list) {
		CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);
		if (u != b->hash)
			continue;
		if (strcmp(b->vrt->ident, vb->ident))
			continue;
		b->refcount++;
		return (b);
	}

	ALLOC_OBJ(b, BACKEND_MAGIC);
	XXXAN(b);
	b->magic = BACKEND_MAGIC;

	VTAILQ_INIT(&b->connlist);
	b->hash = u;

	/*
	 * This backend may live longer than the VCL that instantiated it
	 * so we cannot simply reference the VCL's copy of things.
	 */
	REPLACE(b->vrt->ident, vb->ident);
	REPLACE(b->vrt->hostname, vb->hostname);
	REPLACE(b->vrt->portname, vb->portname);
	REPLACE(b->vrt->vcl_name, vb->vcl_name);

	b->vrt->connect_timeout = vb->connect_timeout;

	MTX_INIT(&b->mtx);
	b->refcount = 1;

	vbe_dns_lookup(cli, b);

	VTAILQ_INSERT_TAIL(&backends, b, list);
	VSL_stats->n_backend++;
	return (b);
}


/*--------------------------------------------------------------------*/

void
VRT_fini_dir(struct cli *cli, struct director *b)
{

	(void)cli;
	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(b, DIRECTOR_MAGIC);
	b->fini(b);
}

/*--------------------------------------------------------------------*/

static void
cli_debug_backend(struct cli *cli, const char * const *av, void *priv)
{
	struct backend *b;

	(void)av;
	(void)priv;
	ASSERT_CLI();
	VTAILQ_FOREACH(b, &backends, list) {
		CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);
		cli_out(cli, "%p %s/%s/%s %d\n",
		    b,
		    b->vrt->vcl_name,
		    b->vrt->hostname,
		    b->vrt->portname,
		    b->refcount);
	}
}

static struct cli_proto debug_cmds[] = {
	{ "debug.backend", "debug.backend",
	    "\tExamine Backend internals\n", 0, 0, cli_debug_backend },
	{ NULL }
};

/*--------------------------------------------------------------------*/

void
VBE_Init(void)
{

	MTX_INIT(&VBE_mtx);
	CLI_AddFuncs(DEBUG_CLI, debug_cmds);
}
