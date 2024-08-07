/*-
 * Copyright 2009-2016 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Authors: Nils Goroll <nils.goroll@uplex.de>
 *          Geoffrey Simmons <geoff.simmons@uplex.de>
 *          Julian Wiesener <jw@uplex.de>
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
 */

/*lint -e801 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <limits.h>

#include "cache/cache.h"

#include "vbm.h"
#include "vrnd.h"

#include "vcc_directors_if.h"
#include "vmod_directors_shard_dir.h"

struct shard_be_info {
	unsigned	hostid;
	unsigned	healthy;
	double		changed;	// when
};

/*
 * circle walk state for shard_next
 *
 * pick* cut off the search after having seen all possible backends
 */
struct shard_state {
	const struct vrt_ctx	*ctx;
	struct sharddir	*shardd;
	uint32_t		idx;

	struct vbitmap		*picklist;
	unsigned		pickcount;

	struct shard_be_info	previous;
	struct shard_be_info	last;
};

void
sharddir_debug(struct sharddir *shardd, const uint32_t flags)
{
	CHECK_OBJ_NOTNULL(shardd, SHARDDIR_MAGIC);
	shardd->debug_flags = flags;
}

void
sharddir_log(struct vsl_log *vsl, enum VSL_tag_e tag,  const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (vsl != NULL)
		VSLbv(vsl, tag, fmt, ap);
	else
		VSLv(tag, NO_VXID, fmt, ap);
	va_end(ap);
}

static int
shard_lookup(const struct sharddir *shardd, const uint32_t key)
{
	CHECK_OBJ_NOTNULL(shardd, SHARDDIR_MAGIC);

	const uint32_t n = shardd->n_points;
	uint32_t i, idx = UINT32_MAX, high = n, low = 0;

	assert (n < idx);

	do {
	    i = (high + low) / 2 ;
	    if (shardd->hashcircle[i].point == key)
		idx = i;
	    else if (i == n - 1)
		idx = n - 1;
	    else if (shardd->hashcircle[i].point < key &&
		     shardd->hashcircle[i+1].point >= key)
		idx = i + 1;
	    else if (shardd->hashcircle[i].point > key)
		if (i == 0)
		    idx = 0;
		else
		    high = i;
	    else
		low = i;
	} while (idx == UINT32_MAX);

	return (idx);
}

static int
shard_next(struct shard_state *state, VCL_INT skip, VCL_BOOL healthy)
{
	int c, chosen = -1;
	VCL_BACKEND be;
	vtim_real changed;
	struct shard_be_info *sbe;

	AN(state);
	CHECK_OBJ_NOTNULL(state->shardd, SHARDDIR_MAGIC);

	if (state->pickcount >= state->shardd->n_backend)
		return (-1);

	while (state->pickcount < state->shardd->n_backend && skip >= 0) {

		c = state->shardd->hashcircle[state->idx].host;

		if (!vbit_test(state->picklist, c)) {

			vbit_set(state->picklist, c);
			state->pickcount++;

			sbe = NULL;
			be = state->shardd->backend[c].backend;
			AN(be);
			if (VRT_Healthy(state->ctx, be, &changed)) {
				if (skip-- == 0) {
					chosen = c;
					sbe = &state->last;
				} else {
					sbe = &state->previous;
				}

			} else if (!healthy && skip-- == 0) {
				chosen = c;
				sbe = &state->last;
			}
			if (sbe == &state->last &&
			    state->last.hostid != UINT_MAX)
				memcpy(&state->previous, &state->last,
				    sizeof(state->previous));

			if (sbe) {
				sbe->hostid = c;
				sbe->healthy = 1;
				sbe->changed = changed;
			}
			if (chosen != -1)
				break;
		}

		if (++(state->idx) == state->shardd->n_points)
			state->idx = 0;
	}
	return (chosen);
}

void
sharddir_new(struct sharddir **sharddp, const char *vcl_name,
    const struct vmod_directors_shard_param *param)
{
	struct sharddir *shardd;

	AN(vcl_name);
	AN(sharddp);
	AZ(*sharddp);
	ALLOC_OBJ(shardd, SHARDDIR_MAGIC);
	AN(shardd);
	*sharddp = shardd;
	shardd->name = vcl_name;
	shardd->param = param;
	PTOK(pthread_rwlock_init(&shardd->mtx, NULL));
}

void
sharddir_set_param(struct sharddir *shardd,
    const struct vmod_directors_shard_param *param)
{
	CHECK_OBJ_NOTNULL(shardd, SHARDDIR_MAGIC);
	shardd->param = param;
}

void
sharddir_release(struct sharddir *shardd)
{
	CHECK_OBJ_NOTNULL(shardd, SHARDDIR_MAGIC);
	shardcfg_backend_clear(shardd);
}

void
sharddir_delete(struct sharddir **sharddp)
{
	struct sharddir *shardd;

	TAKE_OBJ_NOTNULL(shardd, sharddp, SHARDDIR_MAGIC);
	shardcfg_delete(shardd);
	PTOK(pthread_rwlock_destroy(&shardd->mtx));
	FREE_OBJ(shardd);
}

void
sharddir_rdlock(struct sharddir *shardd)
{
	CHECK_OBJ_NOTNULL(shardd, SHARDDIR_MAGIC);
	PTOK(pthread_rwlock_rdlock(&shardd->mtx));
}

void
sharddir_wrlock(struct sharddir *shardd)
{
	CHECK_OBJ_NOTNULL(shardd, SHARDDIR_MAGIC);
	PTOK(pthread_rwlock_wrlock(&shardd->mtx));
}

void
sharddir_unlock(struct sharddir *shardd)
{
	CHECK_OBJ_NOTNULL(shardd, SHARDDIR_MAGIC);
	PTOK(pthread_rwlock_unlock(&shardd->mtx));
}

static inline void
validate_alt(VRT_CTX, const struct sharddir *shardd, VCL_INT *alt)
{
	const VCL_INT alt_max = shardd->n_backend - 1;

	if (*alt < 0) {
		shard_err(ctx->vsl, shardd->name,
		    "invalid negative parameter alt=%ld, set to 0", *alt);
		*alt = 0;
	} else if (*alt > alt_max) {
		shard_err(ctx->vsl, shardd->name,
		    "parameter alt=%ld limited to %ld", *alt, alt_max);
		*alt = alt_max;
	}
}

static inline void
init_state(struct shard_state *state,
    VRT_CTX, struct sharddir *shardd, struct vbitmap *picklist)
{
	AN(picklist);

	state->ctx = ctx;
	state->shardd = shardd;
	state->idx = UINT32_MAX;
	state->picklist = picklist;

	/* healthy and changed only defined for valid hostids */
	state->previous.hostid = UINT_MAX;
	state->last.hostid = UINT_MAX;
}

/* basically same as vdir_any_healthy
 * - XXX we should embed a vdir
 * - XXX should we return the health state of the actual backend
 *   for healthy=IGNORE ?
 */
VCL_BOOL
sharddir_any_healthy(VRT_CTX, struct sharddir *shardd, VCL_TIME *changed)
{
	unsigned i, retval = 0;
	VCL_BACKEND be;
	vtim_real c;

	CHECK_OBJ_NOTNULL(shardd, SHARDDIR_MAGIC);
	sharddir_rdlock(shardd);
	if (changed != NULL)
		*changed = 0;
	for (i = 0; i < shardd->n_backend; i++) {
		be = shardd->backend[i].backend;
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
		retval = VRT_Healthy(ctx, be, &c);
		if (changed != NULL && c > *changed)
			*changed = c;
		if (retval)
			break;
	}
	sharddir_unlock(shardd);
	return (retval);
}

/*
 * core function for the director backend/resolve method
 */

static VCL_BACKEND
sharddir_pick_be_locked(VRT_CTX, const struct sharddir *shardd, uint32_t key,
    VCL_INT alt, VCL_REAL warmup, VCL_BOOL rampup, VCL_ENUM healthy,
    struct shard_state *state)
{
	VCL_BACKEND be;
	VCL_DURATION chosen_r, alt_r;

	CHECK_OBJ_NOTNULL(shardd, SHARDDIR_MAGIC);
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(ctx->vsl);
	assert(shardd->n_backend > 0);

	assert(shardd->hashcircle);

	validate_alt(ctx, shardd, &alt);

	state->idx = shard_lookup(shardd, key);
	assert(state->idx < UINT32_MAX);

	SHDBG(SHDBG_LOOKUP, shardd, "lookup key %x idx %u host %u",
	    key, state->idx, shardd->hashcircle[state->idx].host);

	if (alt > 0) {
		if (shard_next(state, alt - 1,
		    healthy == VENUM(ALL) ? 1 : 0) == -1) {
			if (state->previous.hostid != UINT_MAX) {
				be = sharddir_backend(shardd,
				    state->previous.hostid);
				AN(be);
				return (be);
			}
			return (NULL);
		}
	}

	if (shard_next(state, 0, healthy == VENUM(IGNORE) ? 0 : 1) == -1) {
		if (state->previous.hostid != UINT_MAX) {
			be = sharddir_backend(shardd, state->previous.hostid);
			AN(be);
			return (be);
		}
		return (NULL);
	}

	be = sharddir_backend(shardd, state->last.hostid);
	AN(be);

	if (warmup == -1)
		warmup = shardd->warmup;

	/* short path for cases we dont want ramup/warmup or can't */
	if (alt > 0 || healthy == VENUM(IGNORE) || (!rampup && warmup == 0) ||
	    shard_next(state, 0, 1) == -1)
		return (be);

	assert(alt == 0);
	assert(state->previous.hostid != UINT_MAX);
	assert(state->last.hostid != UINT_MAX);
	assert(state->previous.hostid != state->last.hostid);
	assert(be == sharddir_backend(shardd, state->previous.hostid));

	chosen_r = shardcfg_get_rampup(shardd, state->previous.hostid);
	alt_r = shardcfg_get_rampup(shardd, state->last.hostid);

	SHDBG(SHDBG_RAMPWARM, shardd, "chosen host %u rampup %f changed %f",
	    state->previous.hostid, chosen_r,
	    ctx->now - state->previous.changed);
	SHDBG(SHDBG_RAMPWARM, shardd, "alt host %u rampup %f changed %f",
	    state->last.hostid, alt_r,
	    ctx->now - state->last.changed);

	if (ctx->now - state->previous.changed < chosen_r) {
		/*
		 * chosen host is in rampup
		 * - no change if alternative host is also in rampup or the dice
		 *   has rolled in favour of the chosen host
		 */
		if (!rampup ||
		    ctx->now - state->last.changed < alt_r ||
		    VRND_RandomTestableDouble() * chosen_r <
		    (ctx->now - state->previous.changed))
			return (be);
	} else {
		/* chosen host not in rampup - warmup ? */
		if (warmup == 0 || VRND_RandomTestableDouble() > warmup)
			return (be);
	}

	be = sharddir_backend(shardd, state->last.hostid);
	return (be);
}

VCL_BACKEND
sharddir_pick_be(VRT_CTX, struct sharddir *shardd, uint32_t key, VCL_INT alt,
    VCL_REAL warmup, VCL_BOOL rampup, VCL_ENUM healthy)
{
	VCL_BACKEND be;
	struct shard_state state[1];
	unsigned picklist_sz;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(shardd, SHARDDIR_MAGIC);

	sharddir_rdlock(shardd);

	if (shardd->n_backend == 0) {
		shard_err0(ctx->vsl, shardd->name, "no backends");
		sharddir_unlock(shardd);
		return (NULL);
	}

	picklist_sz = VBITMAP_SZ(shardd->n_backend);
	char picklist_spc[picklist_sz];

	memset(state, 0, sizeof(state));
	init_state(state, ctx, shardd, vbit_init(picklist_spc, picklist_sz));

	be = sharddir_pick_be_locked(ctx, shardd, key, alt, warmup, rampup,
	    healthy, state);
	sharddir_unlock(shardd);

	vbit_destroy(state->picklist);
	return (be);
}
