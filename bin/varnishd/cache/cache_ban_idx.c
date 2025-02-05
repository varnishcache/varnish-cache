/*-
 * Copyright 2025 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Author: Nils Goroll <nils.goroll@uplex.de>
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
 * This code creates an index on top of the ban list to speed up lookup during
 * cache load (while ban_holds > 0). Its only assumption is that bans do not
 * vanish (as guaranteed by ban_holds), and it does not change any functions
 * called _after_ cache reload by constructing the additional index during
 * lookup only.
 *
 * The lookup function returns a ban for VTAILQ_FOREACH_FROM() to start from,
 * such that changes to the ban code remain minimal.
 *
 */

#include "config.h"

#include <stdlib.h>

#include "cache_varnishd.h"
#include "cache_ban.h"
#include "cache_objhead.h"

#include "vtree.h"

struct metaban {
	unsigned		magic;
#define BANIDX_MAGIC		0x39b799f8
	VRBT_ENTRY(metaban)	tree;
	// duplicate the ban time for efficiency
	vtim_real		time;
	struct ban		*ban;
};

static inline int
metaban_cmp(const struct metaban *i1, struct metaban *i2)
{
	if (i1->time < i2->time)
		return (-1);
	if (i1->time > i2->time)
		return (1);
	return (0);
}

VRBT_HEAD(banidx_s, metaban);
VRBT_GENERATE_REMOVE_COLOR(banidx_s, metaban, tree, static)
VRBT_GENERATE_REMOVE(banidx_s, metaban, tree, static)
VRBT_GENERATE_NFIND(banidx_s, metaban, tree, metaban_cmp, static)
VRBT_GENERATE_INSERT_COLOR(banidx_s, metaban, tree, static)
VRBT_GENERATE_INSERT_FINISH(banidx_s, metaban, tree, static)
VRBT_GENERATE_INSERT(banidx_s, metaban, tree, metaban_cmp, static)
VRBT_GENERATE_NEXT(banidx_s, metaban, tree, static)
VRBT_GENERATE_MINMAX(banidx_s, metaban, tree, static)

static struct banidx_s banidx = VRBT_INITIALIZER(banidx);
static pthread_mutex_t banidxmtx = PTHREAD_MUTEX_INITIALIZER;

struct ban *
BANIDX_lookup(vtim_real t0)
{
	struct metaban *m, needle = {0, .time = t0};
	struct ban *best = NULL, *b = NULL;
	vtim_real t1;

	PTOK(pthread_mutex_lock(&banidxmtx));
	m = VRBT_NFIND(banidx_s, &banidx, &needle);
	if (m != NULL && ! (m->time > t0)) {
		PTOK(pthread_mutex_unlock(&banidxmtx));
		return (m->ban);
	}
	/*
	 * if we have m, it is later than t0, which is higher up the list.
	 * check if there is a better match and create missing elements
	 * along the way
	 * if VRBT_NFIND did not return anything, it means it has no index for
	 * elements higher up the list and we can index from the top (implicit
	 * in VTAILQ_FOREACH_FROM())
	 */
	if (m != NULL) {
		best = m->ban;
		b = VTAILQ_NEXT(best, list);
		if (b == NULL) {
			PTOK(pthread_mutex_unlock(&banidxmtx));
			return (best);
		}
	}
	VTAILQ_FOREACH_FROM(b, &ban_head, list) {
		t1 = ban_time(b->spec);
		if (t1 < t0)
			break;
		ALLOC_OBJ(m, BANIDX_MAGIC);
		m->time = t1;
		m->ban = b;
		AZ(VRBT_INSERT(banidx_s, &banidx, m));
		best = b;
	}
	PTOK(pthread_mutex_unlock(&banidxmtx));
	return (best);
}

void
BANIDX_fini(void)
{
	struct metaban *m, *mm;

	VRBT_FOREACH_SAFE(m, banidx_s, &banidx, mm) {
		VRBT_REMOVE(banidx_s, &banidx, m);
		FREE_OBJ(m);
	}
}
