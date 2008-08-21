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
 */

#include "config.h"

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
	unsigned		weight;
};

struct vdi_random {
	unsigned		magic;
#define VDI_RANDOM_MAGIC	0x3771ae23
	struct director		dir;
	struct vdi_random_host	*hosts;
	unsigned		nhosts;
};


/*lint -e{818} not const-able */
static struct backend *
vdi_random_choose(struct sess *sp)
{
	int i;
	struct vdi_random *vs;
	uint32_t r;
	struct vdi_random_host *vh;

	CHECK_OBJ_NOTNULL(sp->director, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vs, sp->director->priv, VDI_RANDOM_MAGIC);
	r = random();
	r &= 0x7fffffff;

	for (i = 0, vh = vs->hosts; i < vs->nhosts; vh++) 
		if (r < vh->weight)
			return (vh->backend);
	assert(0 == __LINE__);
	return (NULL);
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
	vs->dir.magic = 0;
	FREE_OBJ(vs);
}

void
VRT_init_dir_random(struct cli *cli, struct director **bp, const struct vrt_dir_random *t)
{
	struct vdi_random *vs;
	const struct vrt_dir_random_entry *te;
	struct vdi_random_host *vh;
	double s, a, b;
	unsigned v;
	int i;
	
	(void)cli;

	ALLOC_OBJ(vs, VDI_RANDOM_MAGIC);
	XXXAN(vs);
	vs->hosts = calloc(sizeof *vh, t->nmember);
	XXXAN(vs->hosts);

	vs->dir.magic = DIRECTOR_MAGIC;
	vs->dir.priv = vs;
	vs->dir.name = "random";
	vs->dir.choose = vdi_random_choose;
	vs->dir.fini = vdi_random_fini;

	s = 0;
	vh = vs->hosts;
	te = t->members;
	for (i = 0; i < t->nmember; i++, vh++, te++) {
		assert(te->weight > 0.0);
		s += te->weight;
		vh->backend = VBE_AddBackend(cli, te->host);
	}
	vs->nhosts = t->nmember;

	/* Normalize weights */
	i = 0;
	a = 0.0;
	assert(s > 0.0);
	for (te = t->members; i < t->nmember; te++, i++) {
		/* First normalize the specified weight in FP */
		b = te->weight / s;	/*lint !e795 not zero division */
		/* Then accumulate to eliminate rounding errors */
		a += b;
		/* Convert to unsigned in random() range */
		v = (unsigned)((1U<<31) * a);
		vs->hosts[i].weight = v;
	}
	assert(vs->hosts[t->nmember - 1].weight > 0x7fffffff);
	*bp = &vs->dir;
}
