/*-
 * Copyright 2009-2016 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Authors: Nils Goroll <nils.goroll@uplex.de>
 *	    Geoffrey Simmons <geoff@uplex.de>
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

#include "config.h"

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cache/cache.h"

#include "vmod_directors_shard_dir.h"
#include "vmod_directors_shard_cfg.h"

/*lint -esym(749,  shard_change_task_e::*) */
enum shard_change_task_e {
	_SHARD_TASK_E_INVALID = 0,
	CLEAR,
	ADD_BE,
	REMOVE_BE,
	_SHARD_TASK_E_MAX
};

struct shard_change_task {
	unsigned				magic;
#define SHARD_CHANGE_TASK_MAGIC			0x1e1168af
	enum shard_change_task_e		task;
	void					*priv;
	VCL_REAL				weight;
	VSTAILQ_ENTRY(shard_change_task)	list;
};

struct shard_change {
	unsigned				magic;
#define SHARD_CHANGE_MAGIC			0xdff5c9a6
	struct vsl_log				*vsl;
	struct sharddir				*shardd;
	VSTAILQ_HEAD(,shard_change_task)	tasks;
};

struct backend_reconfig {
	struct sharddir * const shardd;
	unsigned		hint;	// on number of backends after reconfig
	unsigned		hole_n; // number of holes in backends array
	unsigned		hole_i; // index hint on first hole
};

/* forward decl */
static VCL_BOOL
change_reconfigure(VRT_CTX, struct shard_change *change, VCL_INT replicas);

/*
 * ============================================================
 * change / task list
 *
 * for backend reconfiguration, we create a change list on the VCL workspace in
 * a PRIV_TASK state, which we work in reconfigure.
 */

static void v_matchproto_(vmod_priv_fini_f)
shard_change_fini(VRT_CTX, void * priv)
{
	struct shard_change *change;

	if (priv == NULL)
		return;

	CAST_OBJ_NOTNULL(change, priv, SHARD_CHANGE_MAGIC);

	(void) change_reconfigure(ctx, change, 67);
}

static const struct vmod_priv_methods shard_change_priv_methods[1] = {{
	.magic = VMOD_PRIV_METHODS_MAGIC,
	.type = "vmod_directors_shard_cfg",
	.fini = shard_change_fini
}};

static struct shard_change *
shard_change_get(VRT_CTX, struct sharddir * const shardd)
{
	struct vmod_priv *task;
	struct shard_change *change;
	const void *id = (const char *)shardd + task_off_cfg;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	task = VRT_priv_task(ctx, id);
	if (task == NULL) {
		shard_fail(ctx, shardd->name, "%s", "no priv_task");
		return (NULL);
	}

	if (task->priv != NULL) {
		CAST_OBJ_NOTNULL(change, task->priv, SHARD_CHANGE_MAGIC);
		assert (change->vsl == ctx->vsl);
		assert (change->shardd == shardd);
		return (change);
	}

	WS_TASK_ALLOC_OBJ(ctx, change, SHARD_CHANGE_MAGIC);
	if (change == NULL)
		return (NULL);
	change->vsl = ctx->vsl;
	change->shardd = shardd;
	VSTAILQ_INIT(&change->tasks);
	task->priv = change;
	task->methods = shard_change_priv_methods;

	return (change);
}

static void
shard_change_finish(struct shard_change *change)
{
	CHECK_OBJ_NOTNULL(change, SHARD_CHANGE_MAGIC);

	VSTAILQ_INIT(&change->tasks);
}

static struct shard_change_task *
shard_change_task_add(VRT_CTX, struct shard_change *change,
    enum shard_change_task_e task_e, void *priv)
{
	struct shard_change_task *task;

	CHECK_OBJ_NOTNULL(change, SHARD_CHANGE_MAGIC);

	WS_TASK_ALLOC_OBJ(ctx, task, SHARD_CHANGE_TASK_MAGIC);
	if (task == NULL)
		return (NULL);
	task->task = task_e;
	task->priv = priv;
	VSTAILQ_INSERT_TAIL(&change->tasks, task, list);

	return (task);
}

static inline struct shard_change_task *
shard_change_task_backend(VRT_CTX, struct sharddir *shardd,
    enum shard_change_task_e task_e, VCL_BACKEND be, VCL_STRING ident,
    VCL_DURATION rampup)
{
	struct shard_change *change;
	struct shard_backend *b;

	CHECK_OBJ_NOTNULL(shardd, SHARDDIR_MAGIC);
	assert(task_e == ADD_BE || task_e == REMOVE_BE);

	change = shard_change_get(ctx, shardd);
	if (change == NULL)
		return (NULL);

	b = WS_Alloc(ctx->ws, sizeof(*b));
	if (b == NULL) {
		shard_fail(ctx, change->shardd->name, "%s",
		    "could not get workspace for task");
		return (NULL);
	}

	b->backend = NULL;
	VRT_Assign_Backend(&b->backend, be);
	b->ident = ident != NULL && *ident != '\0' ? ident : NULL;
	b->rampup = rampup;

	return (shard_change_task_add(ctx, change, task_e, b));
}

/*
 * ============================================================
 * director reconfiguration tasks
 */
VCL_BOOL
shardcfg_add_backend(VRT_CTX, struct sharddir *shardd,
    VCL_BACKEND be, VCL_STRING ident, VCL_DURATION rampup, VCL_REAL weight)
{
	struct shard_change_task *task;

	assert (weight >= 1);
	AN(be);

	task = shard_change_task_backend(ctx, shardd, ADD_BE,
	    be, ident, rampup);

	if (task == NULL)
		return (0);

	task->weight = weight;
	return (1);
}

VCL_BOOL
shardcfg_remove_backend(VRT_CTX, struct sharddir *shardd,
    VCL_BACKEND be, VCL_STRING ident)
{
	return (shard_change_task_backend(ctx, shardd, REMOVE_BE,
	    be, ident, 0) != NULL);
}

VCL_BOOL
shardcfg_clear(VRT_CTX, struct sharddir *shardd)
{
	struct shard_change *change;

	CHECK_OBJ_NOTNULL(shardd, SHARDDIR_MAGIC);

	change = shard_change_get(ctx, shardd);
	if (change == NULL)
		return (0);

	return (shard_change_task_add(ctx, change, CLEAR, NULL) != NULL);
}

/*
 * ============================================================
 * consistent hashing circle init
 */

typedef int (*compar)( const void*, const void* );

static int
circlepoint_compare(const struct shard_circlepoint *a,
    const struct shard_circlepoint *b)
{
	return ((a->point == b->point) ? 0 : ((a->point > b->point) ? 1 : -1));
}

static void
shardcfg_hashcircle(struct sharddir *shardd)
{
	const struct shard_backend *backends, *b;
	unsigned h;
	uint32_t i, j, n_points, r, rmax;
	const char *ident;
	const int len = 12; // log10(UINT32_MAX) + 2;
	char s[len];

	CHECK_OBJ_NOTNULL(shardd, SHARDDIR_MAGIC);
	AZ(shardd->hashcircle);

	assert(shardd->n_backend > 0);
	backends=shardd->backend;
	AN(backends);

	n_points = 0;
	rmax = (UINT32_MAX - 1) / shardd->n_backend;
	for (b = backends; b < backends + shardd->n_backend; b++) {
		CHECK_OBJ_NOTNULL(b->backend, DIRECTOR_MAGIC);
		n_points += vmin_t(uint32_t, b->replicas, rmax);
	}

	assert(n_points < UINT32_MAX);

	shardd->n_points = n_points;
	shardd->hashcircle = calloc(n_points, sizeof(struct shard_circlepoint));
	AN(shardd->hashcircle);

	i = 0;
	for (h = 0, b = backends; h < shardd->n_backend; h++, b++) {
		ident = b->ident ? b->ident : VRT_BACKEND_string(b->backend);

		AN(ident);
		assert(ident[0] != '\0');

		r = vmin_t(uint32_t, b->replicas, rmax);

		for (j = 0; j < r; j++) {
			assert(snprintf(s, len, "%d", j) < len);
			assert (i < n_points);
			shardd->hashcircle[i].point =
			    VRT_HashStrands32(TOSTRANDS(2, ident, s));
			shardd->hashcircle[i].host = h;
			i++;
		}
	}
	assert (i == n_points);
	qsort( (void *) shardd->hashcircle, n_points,
	    sizeof (struct shard_circlepoint), (compar) circlepoint_compare);

	if ((shardd->debug_flags & SHDBG_CIRCLE) == 0)
		return;

	for (i = 0; i < n_points; i++)
		SHDBG(SHDBG_CIRCLE, shardd,
		    "hashcircle[%5jd] = {point = %8x, host = %2u}\n",
		    (intmax_t)i, shardd->hashcircle[i].point,
		    shardd->hashcircle[i].host);
}

/*
 * ============================================================
 * configure the director backends
 */

static void
shardcfg_backend_free(struct shard_backend *f)
{
	if (f->freeptr)
		free (f->freeptr);
	VRT_Assign_Backend(&f->backend, NULL);
	memset(f, 0, sizeof(*f));
}

static void
shardcfg_backend_copyin(struct shard_backend *dst,
    const struct shard_backend *src)
{
	dst->backend = src->backend;
	dst->ident = src->ident ? strdup(src->ident) : NULL;
	dst->rampup = src->rampup;
}

static int
shardcfg_backend_cmp(const struct shard_backend *a,
    const struct shard_backend *b)
{
	const char *ai, *bi;

	ai = a->ident;
	bi = b->ident;

	assert(ai || a->backend);
	assert(bi || b->backend);

	/* vcl_names are unique, so we can compare the backend pointers */
	if (ai == NULL && bi == NULL)
		return (a->backend != b->backend);

	if (ai == NULL)
		ai = VRT_BACKEND_string(a->backend);

	if (bi == NULL)
		bi = VRT_BACKEND_string(b->backend);

	AN(ai);
	AN(bi);
	return (strcmp(ai, bi));
}

/* for removal, we delete all instances if the backend matches */
static int
shardcfg_backend_del_cmp(const struct shard_backend *task,
    const struct shard_backend *b)
{
	assert(task->backend || task->ident);

	if (task->ident == NULL)
		return (task->backend != b->backend);

	return (shardcfg_backend_cmp(task, b));
}

static const struct shard_backend *
shardcfg_backend_lookup(const struct backend_reconfig *re,
    const struct shard_backend *b)
{
	unsigned i, max = re->shardd->n_backend + re->hole_n;
	const struct shard_backend *bb = re->shardd->backend;

	if (max > 0)
		AN(bb);

	for (i = 0; i < max; i++) {
		if (bb[i].backend == NULL)
			continue;	// hole
		if (!shardcfg_backend_cmp(b, &bb[i]))
			return (&bb[i]);
	}
	return (NULL);
}

static void
shardcfg_backend_expand(const struct backend_reconfig *re)
{
	unsigned min = re->hint;

	CHECK_OBJ_NOTNULL(re->shardd, SHARDDIR_MAGIC);

	min = vmax_t(unsigned, min, 16);

	if (re->shardd->l_backend < min)
		re->shardd->l_backend = min;
	else
		re->shardd->l_backend *= 2;

	re->shardd->backend = realloc(re->shardd->backend,
	    re->shardd->l_backend * sizeof *re->shardd->backend);

	AN(re->shardd->backend);
}

static void
shardcfg_backend_add(struct backend_reconfig *re,
    const struct shard_backend *b, uint32_t replicas)
{
	unsigned i;
	struct shard_backend *bb = re->shardd->backend;

	if (re->hole_n == 0) {
		if (re->shardd->n_backend >= re->shardd->l_backend) {
			shardcfg_backend_expand(re);
			bb = re->shardd->backend;
		}
		assert(re->shardd->n_backend < re->shardd->l_backend);
		i = re->shardd->n_backend;
	} else {
		assert(re->hole_i != UINT_MAX);
		do {
			if (!bb[re->hole_i].backend)
				break;
		} while (++(re->hole_i) < re->shardd->n_backend + re->hole_n);
		assert(re->hole_i < re->shardd->n_backend + re->hole_n);

		i = (re->hole_i)++;
		(re->hole_n)--;
	}

	re->shardd->n_backend++;
	shardcfg_backend_copyin(&bb[i], b);
	bb[i].replicas = replicas;
}

void
shardcfg_backend_clear(struct sharddir *shardd)
{
	unsigned i;
	for (i = 0; i < shardd->n_backend; i++)
		shardcfg_backend_free(&shardd->backend[i]);
	shardd->n_backend = 0;
}


static void
shardcfg_backend_del(struct backend_reconfig *re, struct shard_backend *spec)
{
	unsigned i, max = re->shardd->n_backend + re->hole_n;
	struct shard_backend * const bb = re->shardd->backend;

	for (i = 0; i < max; i++) {
		if (bb[i].backend == NULL)
			continue;	// hole
		if (shardcfg_backend_del_cmp(spec, &bb[i]))
			continue;

		shardcfg_backend_free(&bb[i]);
		re->shardd->n_backend--;
		if (i < re->shardd->n_backend + re->hole_n) {
			(re->hole_n)++;
			re->hole_i = vmin(re->hole_i, i);
		}
	}
	VRT_Assign_Backend(&spec->backend, NULL);
}

static void
shardcfg_backend_finalize(struct backend_reconfig *re)
{
	unsigned i;
	struct shard_backend * const bb = re->shardd->backend;

	while (re->hole_n > 0) {
		// trim end
		i = re->shardd->n_backend + re->hole_n - 1;
		while (re->hole_n && bb[i].backend == NULL) {
			(re->hole_n)--;
			i--;
		}

		if (re->hole_n == 0)
			break;

		assert(re->hole_i < i);

		do {
			if (!bb[re->hole_i].backend)
				break;
		} while (++(re->hole_i) <= i);

		assert(re->hole_i < i);
		assert(bb[re->hole_i].backend == NULL);
		assert(bb[i].backend != NULL);

		memcpy(&bb[re->hole_i], &bb[i], sizeof(*bb));
		memset(&bb[i], 0, sizeof(*bb));

		(re->hole_n)--;
		(re->hole_i)++;
	}

	assert(re->hole_n == 0);
}

/*
 * ============================================================
 * work the change tasks
 */

static void
shardcfg_apply_change(struct vsl_log *vsl, struct sharddir *shardd,
    const struct shard_change *change, VCL_INT replicas)
{
	struct shard_change_task *task, *clear;
	const struct shard_backend *b;
	uint32_t b_replicas;

	struct backend_reconfig re = {
		.shardd = shardd,
		.hint = shardd->n_backend,
		.hole_n = 0,
		.hole_i = UINT_MAX
	};

	// XXX assert sharddir_locked(shardd)

	clear = NULL;
	VSTAILQ_FOREACH(task, &change->tasks, list) {
		CHECK_OBJ_NOTNULL(task, SHARD_CHANGE_TASK_MAGIC);
		switch (task->task) {
		case CLEAR:
			clear = task;
			re.hint = 0;
			break;
		case ADD_BE:
			re.hint++;
			break;
		case REMOVE_BE:
			re.hint--;
			break;
		default:
			INCOMPL();
		}
	}

	if (clear) {
		shardcfg_backend_clear(shardd);
		clear = VSTAILQ_NEXT(clear, list);
		if (clear == NULL)
			return;
	}

	task = clear;
	VSTAILQ_FOREACH_FROM(task, &change->tasks, list) {
		CHECK_OBJ_NOTNULL(task, SHARD_CHANGE_TASK_MAGIC);
		switch (task->task) {
		case CLEAR:
			assert(task->task != CLEAR);
			break;
		case ADD_BE:
			b = shardcfg_backend_lookup(&re, task->priv);

			if (b == NULL) {
				assert (task->weight >= 1);
				if (replicas * task->weight > UINT32_MAX)
					b_replicas = UINT32_MAX;
				else
					b_replicas = (uint32_t) // flint
						(replicas * task->weight);

				shardcfg_backend_add(&re, task->priv,
				    b_replicas);
				break;
			}

			const char * const ident = b->ident;

			shard_notice(vsl, shardd->name,
			    "backend %s%s%s already exists - skipping",
			    VRT_BACKEND_string(b->backend),
			    ident ? "/" : "",
			    ident ? ident : "");
			break;
		case REMOVE_BE:
			shardcfg_backend_del(&re, task->priv);
			break;
		default:
			INCOMPL();
		}
	}
	shardcfg_backend_finalize(&re);
}

/*
 * ============================================================
 * top reconfiguration function
 */

static VCL_BOOL
change_reconfigure(VRT_CTX, struct shard_change *change, VCL_INT replicas)
{
	struct sharddir *shardd;

	CHECK_OBJ_NOTNULL(change, SHARD_CHANGE_MAGIC);
	assert (replicas > 0);
	shardd = change->shardd;
	CHECK_OBJ_NOTNULL(shardd, SHARDDIR_MAGIC);

	if (VSTAILQ_FIRST(&change->tasks) == NULL)
		return (1);

	sharddir_wrlock(shardd);

	shardcfg_apply_change(ctx->vsl, shardd, change, replicas);
	shard_change_finish(change);

	if (shardd->hashcircle)
		free(shardd->hashcircle);
	shardd->hashcircle = NULL;

	if (shardd->n_backend == 0) {
		shard_err0(ctx->vsl, shardd->name,
		    ".reconfigure() no backends");
		sharddir_unlock(shardd);
		return (0);
	}

	shardcfg_hashcircle(shardd);
	sharddir_unlock(shardd);
	return (1);
}

VCL_BOOL
shardcfg_reconfigure(VRT_CTX, struct sharddir *shardd, VCL_INT replicas)
{
	struct shard_change *change;

	CHECK_OBJ_NOTNULL(shardd, SHARDDIR_MAGIC);
	if (replicas <= 0) {
		shard_err(ctx->vsl, shardd->name,
		    ".reconfigure() invalid replicas argument %ld", replicas);
		return (0);
	}

	change = shard_change_get(ctx, shardd);
	if (change == NULL)
		return (0);

	return (change_reconfigure(ctx, change, replicas));
}

/*
 * ============================================================
 * misc config related
 */

/* only for sharddir_delete() */
void
shardcfg_delete(const struct sharddir *shardd)
{

	AZ(shardd->n_backend);
	if (shardd->backend)
		free(shardd->backend);
	if (shardd->hashcircle)
		free(shardd->hashcircle);
}

VCL_VOID
shardcfg_set_warmup(struct sharddir *shardd, VCL_REAL ratio)
{
	CHECK_OBJ_NOTNULL(shardd, SHARDDIR_MAGIC);
	assert(ratio >= 0 && ratio < 1);
	sharddir_wrlock(shardd);
	shardd->warmup = ratio;
	sharddir_unlock(shardd);
}

VCL_VOID
shardcfg_set_rampup(struct sharddir *shardd, VCL_DURATION duration)
{
	CHECK_OBJ_NOTNULL(shardd, SHARDDIR_MAGIC);
	assert(duration >= 0);
	sharddir_wrlock(shardd);
	shardd->rampup_duration = duration;
	sharddir_unlock(shardd);
}

VCL_DURATION
shardcfg_get_rampup(const struct sharddir *shardd, unsigned host)
{
	VCL_DURATION r;

	CHECK_OBJ_NOTNULL(shardd, SHARDDIR_MAGIC);
	// assert sharddir_rdlock_held(shardd);
	assert (host < shardd->n_backend);

	if (isnan(shardd->backend[host].rampup))
		r = shardd->rampup_duration;
	else
		r = shardd->backend[host].rampup;

	return (r);
}
