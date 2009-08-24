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
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <sys/types.h>
#include <sys/socket.h>

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shmlog.h"
#include "cache.h"
#include "cache_backend.h"
#include "vrt.h"

/*--------------------------------------------------------------------*/

struct vdi_random_host {
	struct backend		*backend;
	double			weight;
};

struct vdi_random {
	unsigned		magic;
#define VDI_RANDOM_MAGIC	0x3771ae23
	struct director		dir;

	unsigned		retries;
	struct vdi_random_host	*hosts;
	unsigned		nhosts;
};

static struct vbe_conn *
vdi_random_getfd(struct sess *sp)
{
	int i, k;
	struct vdi_random *vs;
	double r, s1;
	struct vbe_conn *vbe;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->director, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vs, sp->director->priv, VDI_RANDOM_MAGIC);

	for (k = 0; k < vs->retries; ) {

		/* Sum up the weights of healty backends */
		s1 = 0.0;
		for (i = 0; i < vs->nhosts; i++)
			if (backend_is_healthy(sp,vs->hosts[i].backend))
				s1 += vs->hosts[i].weight;

		if (s1 == 0.0)
			return (NULL);

		/* Pick a random threshold in that interval */
		r = random() / 2147483648.0;	/* 2^31 */
		assert(r >= 0.0 && r < 1.0);
		r *= s1;

		s1 = 0.0;
		for (i = 0; i < vs->nhosts; i++)  {
			if (!backend_is_healthy(sp, vs->hosts[i].backend))
				continue;
			s1 += vs->hosts[i].weight;
			if (r >= s1)
				continue;
			vbe = VBE_GetVbe(sp, vs->hosts[i].backend);
			if (vbe != NULL)
				return (vbe);
			break;
		}
		k++;
	}
	return (NULL);
}

static unsigned
vdi_random_healthy(const struct sess *sp)
{
	struct vdi_random *vs;
	int i;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->director, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vs, sp->director->priv, VDI_RANDOM_MAGIC);

	for (i = 0; i < vs->nhosts; i++) {
		if (backend_is_healthy(sp,vs->hosts[i].backend))
			return 1;
	}
	return 0;
}

/*lint -e{818} not const-able */
static void
vdi_random_fini(struct director *d)
{
	int i;
	struct vdi_random *vs;
	struct vdi_random_host *vh;

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vs, d->priv, VDI_RANDOM_MAGIC);

	vh = vs->hosts;
	for (i = 0; i < vs->nhosts; i++, vh++)
		VBE_DropRef(vh->backend);
	free(vs->hosts);
	free(vs->dir.vcl_name);
	vs->dir.magic = 0;
	FREE_OBJ(vs);
}

void
VRT_init_dir_random(struct cli *cli, struct director **bp,
    const struct vrt_dir_random *t)
{
	struct vdi_random *vs;
	const struct vrt_dir_random_entry *te;
	struct vdi_random_host *vh;
	int i;

	(void)cli;

	ALLOC_OBJ(vs, VDI_RANDOM_MAGIC);
	XXXAN(vs);
	vs->hosts = calloc(sizeof *vh, t->nmember);
	XXXAN(vs->hosts);

	vs->dir.magic = DIRECTOR_MAGIC;
	vs->dir.priv = vs;
	vs->dir.name = "random";
	REPLACE(vs->dir.vcl_name, t->name);
	vs->dir.getfd = vdi_random_getfd;
	vs->dir.fini = vdi_random_fini;
	vs->dir.healthy = vdi_random_healthy;

	vs->retries = t->retries;
	if (vs->retries == 0)
		vs->retries = t->nmember;
	vh = vs->hosts;
	te = t->members;
	for (i = 0; i < t->nmember; i++, vh++, te++) {
		assert(te->weight > 0.0);
		vh->weight = te->weight;
		vh->backend = VBE_AddBackend(cli, te->host);
	}
	vs->nhosts = t->nmember;
	*bp = &vs->dir;
}
