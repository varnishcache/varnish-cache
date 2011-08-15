/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 * This code is shared between the random, client and hash directors, because
 * they share the same properties and most of the same selection logic.
 *
 * The random director picks a backend on random.
 *
 * The hash director picks based on the hash from vcl_hash{}
 *
 * The client director picks based on client identity or IP-address
 *
 * In all cases, the choice is by weight of the healthy subset of
 * configured backends.
 *
 * Failures to get a connection are retried, here all three policies
 * fall back to a deterministically random choice, by weight in the
 * healthy subset.
 *
 */

#include "config.h"

#include <sys/types.h>
#include <sys/socket.h>

#include <stdio.h>
#include <errno.h>
#include <math.h>
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

/*
 * Applies sha256 using the given context and input/length, and returns
 * a double in the range [0...1[ based on the hash.
 */
static double
vdi_random_sha(const char *input, ssize_t len)
{
	struct SHA256Context ctx;
	uint8_t sign[SHA256_LEN];

	AN(input);
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, input, len);
	SHA256_Final(sign, &ctx);
	return (vle32dec(sign) / exp2(32));
}

/*
 * Sets up the initial seed for picking a backend according to policy.
 */
static double
vdi_random_init_seed(const struct vdi_random *vs, const struct sess *sp)
{
	const char *p;
	double retval;

	switch (vs->criteria) {
	case c_client:
		if (sp->client_identity != NULL)
			p = sp->client_identity;
		else
			p = sp->addr;
		retval = vdi_random_sha(p, strlen(p));
		break;
	case c_hash:
		AN(sp->digest);
		retval = vle32dec(sp->digest) / exp2(32);
		break;
	case c_random:
	default:
		retval = random() / exp2(31);
		break;
	}
	return (retval);
}

/*
 * Find the healthy backend corresponding to the weight r [0...1[
 */
static struct vbc *
vdi_random_pick_one(struct sess *sp, const struct vdi_random *vs, double r)
{
	double w[vs->nhosts];
	int i;
	double s1;

	assert(r >= 0.0 && r < 1.0);

	memset(w, 0, sizeof w);
	/* Sum up the weights of healty backends */
	s1 = 0.0;
	for (i = 0; i < vs->nhosts; i++) {
		if (VDI_Healthy(vs->hosts[i].backend, sp))
			w[i] = vs->hosts[i].weight;
		s1 += w[i];
	}

	if (s1 == 0.0)
		return (NULL);

	r *= s1;
	s1 = 0.0;
	for (i = 0; i < vs->nhosts; i++)  {
		s1 += w[i];
		if (r < s1)
			return(VDI_GetFd(vs->hosts[i].backend, sp));
	}
	return (NULL);
}

/*
 * Try the specified number of times to get a backend.
 * First one according to policy, after that, deterministically
 * random by rehashing the key.
 */
static struct vbc *
vdi_random_getfd(const struct director *d, struct sess *sp)
{
	int k;
	struct vdi_random *vs;
	double r;
	struct vbc *vbe;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vs, d->priv, VDI_RANDOM_MAGIC);

	r = vdi_random_init_seed(vs, sp);

	for (k = 0; k < vs->retries; k++) {
		vbe = vdi_random_pick_one(sp, vs, r);
		if (vbe != NULL)
			return (vbe);
		r = vdi_random_sha((void *)&r, sizeof(r));
	}
	return (NULL);
}

/*
 * Healthy if just a single backend is...
 */
static unsigned
vdi_random_healthy(const struct director *d, const struct sess *sp)
{
	struct vdi_random *vs;
	int i;

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vs, d->priv, VDI_RANDOM_MAGIC);

	for (i = 0; i < vs->nhosts; i++) {
		if (VDI_Healthy(vs->hosts[i].backend, sp))
			return (1);
	}
	return (0);
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
