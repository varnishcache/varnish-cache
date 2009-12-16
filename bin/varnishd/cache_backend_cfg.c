/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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
 * Handle configuration of backends from VCL programs.
 *
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

#include <sys/socket.h>

#include "shmlog.h"
#include "cache.h"
#include "vrt.h"
#include "vsha256.h"
#include "cache_backend.h"
#include "cli_priv.h"

struct lock VBE_mtx;

/*
 * The list of backends is not locked, it is only ever accessed from
 * the CLI thread, so there is no need.
 */
static VTAILQ_HEAD(, backend) backends = VTAILQ_HEAD_INITIALIZER(backends);


/*--------------------------------------------------------------------
 */

static void
VBE_Nuke(struct backend *b)
{

	ASSERT_CLI();
	VTAILQ_REMOVE(&backends, b, list);
	free(b->ident);
	free(b->hosthdr);
	free(b->ipv4);
	free(b->ipv6);
	FREE_OBJ(b);
	VSL_stats->n_backend--;
}

/*--------------------------------------------------------------------
 */

void
VBE_Poll(void)
{
	struct backend *b, *b2;

	ASSERT_CLI();
	VTAILQ_FOREACH_SAFE(b, &backends, list, b2) {
		if (b->refcount == 0 && b->probe == NULL)
			VBE_Nuke(b);
	}
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
	Lck_Unlock(&b->mtx);
	if (i > 0)
		return;

	ASSERT_CLI();
	VTAILQ_FOREACH_SAFE(vbe, &b->connlist, list, vbe2) {
		VTAILQ_REMOVE(&b->connlist, vbe, list);
		if (vbe->fd >= 0) {
			AZ(close(vbe->fd));
			vbe->fd = -1;
		}
		vbe->backend = NULL;
		VBE_ReleaseConn(vbe);
	}
	if (b->probe != NULL)
		VBP_Stop(b);
	else
		VBE_Nuke(b);
}

void
VBE_DropRef(struct backend *b)
{

	CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);

	Lck_Lock(&b->mtx);
	VBE_DropRefLocked(b);
}

void
VBE_DropRefConn(struct backend *b)
{

	CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);

	Lck_Lock(&b->mtx);
	assert(b->n_conn > 0);
	b->n_conn--;
	VBE_DropRefLocked(b);
}

/*--------------------------------------------------------------------*/

static void
copy_sockaddr(struct sockaddr **sa, socklen_t *len, const unsigned char *src)
{

	assert(*src > 0);
	*sa = malloc(*src);
	AN(*sa);
	memcpy(*sa, src + 1, *src);
	*len = *src;
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
	struct SHA256Context ctx;
	uint8_t hash[SHA256_LEN];

	AN(vb->ident);
	assert(vb->ipv4_sockaddr != NULL || vb->ipv6_sockaddr != NULL);
	(void)cli;
	ASSERT_CLI();

	/* calculate a hash of (ident + ipv4_sockaddr + ipv6_sockaddr) */
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, vb->ident, strlen(vb->ident));
	if (vb->ipv4_sockaddr != NULL)
		SHA256_Update(&ctx,
		    vb->ipv4_sockaddr + 1, vb->ipv4_sockaddr[0]);
	if (vb->ipv6_sockaddr != NULL)
		SHA256_Update(&ctx,
		    vb->ipv6_sockaddr + 1, vb->ipv6_sockaddr[0]);

	SHA256_Final(hash, &ctx);
	memcpy(&u, hash, sizeof u);

	/* Run through the list and see if we already have this backend */
	VTAILQ_FOREACH(b, &backends, list) {
		CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);
		if (u != b->hash)
			continue;
		if (strcmp(b->ident, vb->ident))
			continue;
		if (vb->ipv4_sockaddr != NULL &&
		    b->ipv4len != vb->ipv4_sockaddr[0])
			continue;
		if (vb->ipv6_sockaddr != NULL &&
		    b->ipv6len != vb->ipv6_sockaddr[0])
			continue;
		if (b->ipv4len != 0 &&
		    memcmp(b->ipv4, vb->ipv4_sockaddr + 1, b->ipv4len))
			continue;
		if (b->ipv6len != 0 &&
		    memcmp(b->ipv6, vb->ipv6_sockaddr + 1, b->ipv6len))
			continue;
		b->refcount++;
		return (b);
	}

	/* Create new backend */
	ALLOC_OBJ(b, BACKEND_MAGIC);
	XXXAN(b);
	Lck_New(&b->mtx);
	b->refcount = 1;

	VTAILQ_INIT(&b->connlist);
	b->hash = u;

	VTAILQ_INIT(&b->troublelist);

	/*
	 * This backend may live longer than the VCL that instantiated it
	 * so we cannot simply reference the VCL's copy of things.
	 */
	REPLACE(b->ident, vb->ident);
	REPLACE(b->vcl_name, vb->vcl_name);
	REPLACE(b->hosthdr, vb->hosthdr);

	b->connect_timeout = vb->connect_timeout;
	b->first_byte_timeout = vb->first_byte_timeout;
	b->between_bytes_timeout = vb->between_bytes_timeout;
	b->max_conn = vb->max_connections;
	b->saintmode_threshold = vb->saintmode_threshold;

	/*
	 * Copy over the sockaddrs
	 */
	if (vb->ipv4_sockaddr != NULL)
		copy_sockaddr(&b->ipv4, &b->ipv4len, vb->ipv4_sockaddr);
	if (vb->ipv6_sockaddr != NULL)
		copy_sockaddr(&b->ipv6, &b->ipv6len, vb->ipv6_sockaddr);

	assert(b->ipv4 != NULL || b->ipv6 != NULL);

	VBP_Start(b, &vb->probe);
	VTAILQ_INSERT_TAIL(&backends, b, list);
	VSL_stats->n_backend++;
	return (b);
}


/*--------------------------------------------------------------------*/

void
VRT_init_dir(struct cli *cli, struct director **dir, const char *name,
    int idx, const void *priv)
{

	ASSERT_CLI();
	if (!strcmp(name, "simple"))
		VRT_init_dir_simple(cli, dir, idx, priv);
	else if (!strcmp(name, "hash"))
		VRT_init_dir_hash(cli, dir, idx, priv);
	else if (!strcmp(name, "random"))
		VRT_init_dir_random(cli, dir, idx, priv);
	else if (!strcmp(name, "round-robin"))
		VRT_init_dir_round_robin(cli, dir, idx, priv);
	else
		INCOMPL();
}

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
		cli_out(cli, "%p %s %d %d/%d\n",
		    b, b->vcl_name, b->refcount,
		    b->n_conn, b->max_conn);
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

	Lck_New(&VBE_mtx);
	CLI_AddFuncs(DEBUG_CLI, debug_cmds);
}
