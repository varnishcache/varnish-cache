/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Redpill Linpro AS
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
 * This code is shared between the random and hash directors, because they
 * share the same properties and most of the same selection logic.
 *
 * The random director picks a backend on random, according to weight,
 * from the healty subset of backends.
 *
 * The hash director first tries to locate the "canonical" backend from
 * the full set, according to weight, and if it is healthy selects it.
 * If the canonical backend is not healthy, we pick a backend according
 * to weight from the healthy subset. That way only traffic to unhealthy
 * backends gets redistributed.
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

#include "cache.h"
#include "cache_backend.h"
#include "vrt.h"
#include "vsha256.h"
#include "vend.h"

/*--------------------------------------------------------------------*/

struct vdi_random_host {
	struct director		*backend;
	double			weight;
};

enum crit_e {c_random, c_hash, c_client};

struct vdi_random {
	unsigned		magic;
#define VDI_RANDOM_MAGIC	0x3771ae23
	struct director		dir;

	enum crit_e		criteria;
	unsigned		retries;
	double			tot_weight;
	struct vdi_random_host	*hosts;
	unsigned		nhosts;
};

static struct vbc *
vdi_random_getfd(const struct director *d, struct sess *sp)
{
	int i, k;
	struct vdi_random *vs;
	double r, s1;
	unsigned u = 0;
	struct vbc *vbe;
	struct director *d2;
	struct SHA256Context ctx;
	uint8_t sign[SHA256_LEN], *hp;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vs, d->priv, VDI_RANDOM_MAGIC);

	if (vs->criteria == c_client) {
		/*
		 * Hash the client IP# ascii representation, rather than
		 * rely on the raw IP# being a good hash distributor, since
		 * experience shows this not to be the case.
		 * We do not hash the port number, to make everybody behind
		 * a given NAT gateway fetch from the same backend.
		 */
		SHA256_Init(&ctx);
		AN(sp->addr);
		if (sp->client_identity != NULL)
			SHA256_Update(&ctx, sp->client_identity,
			    strlen(sp->client_identity));
		else
			SHA256_Update(&ctx, sp->addr, strlen(sp->addr));
		SHA256_Final(sign, &ctx);
		hp = sign;
	}
	if (vs->criteria == c_hash) {
		/*
		 * Reuse the hash-string, the objective here is to fetch the
		 * same object on the same backend all the time
		 */
		hp = sp->digest;
	}

	/*
	 * If we are hashing, first try to hit our "canonical backend"
	 * If that fails, we fall through, and select a weighted backend
	 * amongst the healthy set.
	 */
	if (vs->criteria != c_random) {
		u = vle32dec(hp);
		r = u / 4294967296.0;
		assert(r >= 0.0 && r < 1.0);
		r *= vs->tot_weight;
		s1 = 0.0;
		for (i = 0; i < vs->nhosts; i++)  {
			s1 += vs->hosts[i].weight;
			if (r >= s1)
				continue;
			d2 = vs->hosts[i].backend;
			if (!VDI_Healthy_sp(sp, d2))
				break;
			vbe = VDI_GetFd(d2, sp);
			if (vbe != NULL)
				return (vbe);
			break;
		}
	}

	for (k = 0; k < vs->retries; ) {
		/* Sum up the weights of healty backends */
		s1 = 0.0;
		for (i = 0; i < vs->nhosts; i++) {
			d2 = vs->hosts[i].backend;
			/* XXX: cache result of healty to avoid double work */
			if (VDI_Healthy_sp(sp, d2))
				s1 += vs->hosts[i].weight;
		}

		if (s1 == 0.0)
			return (NULL);

		if (vs->criteria != c_random) {
			r = u / 4294967296.0;
		} else {
			/* Pick a random threshold in that interval */
			r = random() / 2147483648.0;	/* 2^31 */
		}
		assert(r >= 0.0 && r < 1.0);
		r *= s1;

		s1 = 0.0;
		for (i = 0; i < vs->nhosts; i++)  {
			d2 = vs->hosts[i].backend;
			if (!VDI_Healthy_sp(sp, d2))
				continue;
			s1 += vs->hosts[i].weight;
			if (r >= s1)
				continue;
			vbe = VDI_GetFd(d2, sp);
			if (vbe != NULL)
				return (vbe);
			break;
		}
		k++;
	}
	return (NULL);
}

static unsigned
vdi_random_healthy(double now, const struct director *d, uintptr_t target)
{
	struct vdi_random *vs;
	int i;

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vs, d->priv, VDI_RANDOM_MAGIC);

	for (i = 0; i < vs->nhosts; i++) {
		if (VDI_Healthy(now, vs->hosts[i].backend, target))
			return 1;
	}
	return 0;
}

static void
vdi_random_fini(const struct director *d)
{
	struct vdi_random *vs;

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vs, d->priv, VDI_RANDOM_MAGIC);

	free(vs->hosts);
	free(vs->dir.vcl_name);
	vs->dir.magic = 0;
	FREE_OBJ(vs);
}

static void
vrt_init(struct cli *cli, struct director **bp, int idx,
    const void *priv, enum crit_e criteria)
{
	const struct vrt_dir_random *t;
	struct vdi_random *vs;
	const struct vrt_dir_random_entry *te;
	struct vdi_random_host *vh;
	int i;

	ASSERT_CLI();
	(void)cli;
	t = priv;

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

	vs->criteria = criteria;
	vs->retries = t->retries;
	if (vs->retries == 0)
		vs->retries = t->nmember;
	vh = vs->hosts;
	te = t->members;
	vs->tot_weight = 0.;
	for (i = 0; i < t->nmember; i++, vh++, te++) {
		assert(te->weight > 0.0);
		vh->weight = te->weight;
		vs->tot_weight += vh->weight;
		vh->backend = bp[te->host];
		AN(vh->backend);
	}
	vs->nhosts = t->nmember;
	bp[idx] = &vs->dir;
}

void
VRT_init_dir_random(struct cli *cli, struct director **bp, int idx,
    const void *priv)
{
	vrt_init(cli, bp, idx, priv, c_random);
}

void
VRT_init_dir_hash(struct cli *cli, struct director **bp, int idx,
    const void *priv)
{
	vrt_init(cli, bp, idx, priv, c_hash);
}

void
VRT_init_dir_client(struct cli *cli, struct director **bp, int idx,
    const void *priv)
{
	vrt_init(cli, bp, idx, priv, c_client);
}
