/*-
 * Copyright (c) 2010-2011 Varnish Software AS
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
 * VSM stuff common to manager and child.
 *
 * Please see comments in <vapi/vsm_int.h> for details of protocols and
 * data consistency.
 *
 */

#include "config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

#include "vapi/vsm_int.h"
#include "vmb.h"
#include "vtim.h"

/*--------------------------------------------------------------------*/

struct vsm_range {
	unsigned			magic;
#define VSM_RANGE_MAGIC			0x8d30f14
	VTAILQ_ENTRY(vsm_range)		list;
	ssize_t				off;
	ssize_t				len;
	double				cool;
	struct VSM_chunk		*chunk;
	void				*ptr;
};

struct vsm_sc {
	unsigned			magic;
#define VSM_SC_MAGIC			0x8b83270d
	char				*b;
	ssize_t				len;
	struct VSM_head			*head;
	double				t0;
	VTAILQ_HEAD(,vsm_range)		r_used;
	VTAILQ_HEAD(,vsm_range)		r_cooling;
	VTAILQ_HEAD(,vsm_range)		r_free;
	VTAILQ_HEAD(,vsm_range)		r_bogus;
	uint64_t			g_free;
	uint64_t			g_used;
	uint64_t			g_cooling;
	uint64_t			g_overflow;
	uint64_t			c_overflow;
};

/*--------------------------------------------------------------------
 * The free list is sorted by size, which means that collapsing ranges
 * on free becomes a multi-pass operation.
 */

static void
vsm_common_insert_free(struct vsm_sc *sc, struct vsm_range *vr)
{
	struct vsm_range *vr2;

	CHECK_OBJ_NOTNULL(sc, VSM_SC_MAGIC);
	CHECK_OBJ_NOTNULL(vr, VSM_RANGE_MAGIC);

	/* First try to see if we can collapse anything */
	VTAILQ_FOREACH(vr2, &sc->r_free, list) {
		if (vr2->off == vr->off + vr->len) {
			vr2->off = vr->off;
			vr2->len += vr->len;
			FREE_OBJ(vr);
			VTAILQ_REMOVE(&sc->r_free, vr2, list);
			vsm_common_insert_free(sc, vr2);
			return;
		}
		if (vr->off == vr2->off + vr2->len) {
			vr2->len += vr->len;
			FREE_OBJ(vr);
			VTAILQ_REMOVE(&sc->r_free, vr2, list);
			vsm_common_insert_free(sc, vr2);
			return;
		}
	}
	/* Insert in size order */
	VTAILQ_FOREACH(vr2, &sc->r_free, list) {
		if (vr2->len > vr->len) {
			VTAILQ_INSERT_BEFORE(vr2, vr, list);
			return;
		}
	}
	/* At tail, if everything in the list is smaller */
	VTAILQ_INSERT_TAIL(&sc->r_free, vr, list);
}

/*--------------------------------------------------------------------
 * Initialize a new VSM segment
 */

struct vsm_sc *
VSM_common_new(void *p, ssize_t l)
{
	struct vsm_sc *sc;
	struct vsm_range *vr;

	assert(PAOK(sizeof(struct VSM_chunk)));
	assert(PAOK(p));
	ALLOC_OBJ(sc, VSM_SC_MAGIC);
	AN(sc);
	VTAILQ_INIT(&sc->r_used);
	VTAILQ_INIT(&sc->r_cooling);
	VTAILQ_INIT(&sc->r_free);
	VTAILQ_INIT(&sc->r_bogus);
	sc->b = p;
	sc->len = l;
	sc->t0 = VTIM_mono();

	sc->head = (void *)sc->b;
	/* This should not be necessary, but just in case...*/
	memset(sc->head, 0, sizeof *sc->head);
	memcpy(sc->head->marker, VSM_HEAD_MARKER, sizeof sc->head->marker);
	sc->head->hdrsize = sizeof *sc->head;
	sc->head->shm_size = l;
	sc->head->alloc_seq = random() | 1;
	VWMB();

	ALLOC_OBJ(vr, VSM_RANGE_MAGIC);
	AN(vr);
	vr->off = RUP2(sizeof(*sc->head), 16);
	vr->len = RDN2(l - vr->off, 16);
	VTAILQ_INSERT_TAIL(&sc->r_free, vr, list);
	sc->g_free = vr->len;
	return (sc);
}

/*--------------------------------------------------------------------
 * Move from cooling list to free list
 */

void
VSM_common_cleaner(struct vsm_sc *sc, struct VSC_C_main *stats)
{
	double now = VTIM_real();
	struct vsm_range *vr, *vr2;

	CHECK_OBJ_NOTNULL(sc, VSM_SC_MAGIC);

	/* Move cooled off stuff to free list */
	VTAILQ_FOREACH_SAFE(vr, &sc->r_cooling, list, vr2) {
		if (vr->cool > now)
			break;
		VTAILQ_REMOVE(&sc->r_cooling, vr, list);
		vsm_common_insert_free(sc, vr);
	}
	stats->vsm_free = sc->g_free;
	stats->vsm_used = sc->g_used;
	stats->vsm_cooling = sc->g_cooling;
	stats->vsm_overflow = sc->g_overflow;
	stats->vsm_overflowed = sc->c_overflow;
}

/*--------------------------------------------------------------------
 * Allocate a chunk from VSM
 */

void *
VSM_common_alloc(struct vsm_sc *sc, ssize_t size,
    const char *class, const char *type, const char *ident)
{
	struct vsm_range *vr, *vr2, *vr3;
	unsigned l1, l2;

	CHECK_OBJ_NOTNULL(sc, VSM_SC_MAGIC);
	AN(size);

	/* XXX: silent truncation instead of assert ? */
	AN(class);
	assert(strlen(class) < sizeof(vr->chunk->class));
	AN(type);
	assert(strlen(type) < sizeof(vr->chunk->type));
	AN(ident);
	assert(strlen(ident) < sizeof(vr->chunk->ident));

	l1 = RUP2(size + sizeof(struct VSM_chunk), 16);
	l2 = RUP2(size + 2 * sizeof(struct VSM_chunk), 16);

	/* Find space in free-list */
	VTAILQ_FOREACH_SAFE(vr, &sc->r_free, list, vr2) {
		if (vr->len < l1)
			continue;
		if (vr->len <= l2) {
			VTAILQ_REMOVE(&sc->r_free, vr, list);
		} else {
			ALLOC_OBJ(vr3, VSM_RANGE_MAGIC);
			AN(vr3);
			vr3->off = vr->off;
			vr3->len = l1;
			vr->off += l1;
			vr->len -= l1;
			VTAILQ_REMOVE(&sc->r_free, vr, list);
			vsm_common_insert_free(sc, vr);
			vr = vr3;
		}
		break;
	}

	if (vr == NULL) {
		/*
		 * No space in VSM, return malloc'd space
		 */
		ALLOC_OBJ(vr, VSM_RANGE_MAGIC);
		AN(vr);
		vr->ptr = malloc(size);
		AN(vr->ptr);
		vr->len = size;
		VTAILQ_INSERT_TAIL(&sc->r_bogus, vr, list);
		sc->g_overflow += vr->len;
		sc->c_overflow += vr->len;
		return (vr->ptr);
	}

	sc->g_free -= vr->len;
	sc->g_used += vr->len;

	/* Zero the entire allocation, to avoid garbage confusing readers */
	memset(sc->b + vr->off, 0, vr->len);

	vr->chunk = (void *)(sc->b + vr->off);
	vr->ptr = (vr->chunk + 1);

	memcpy(vr->chunk->marker, VSM_CHUNK_MARKER, sizeof vr->chunk->marker);
	vr->chunk->len = vr->len;
	strcpy(vr->chunk->class, class);
	strcpy(vr->chunk->type, type);
	strcpy(vr->chunk->ident, ident);
	VWMB();

	vr3 = VTAILQ_FIRST(&sc->r_used);
	VTAILQ_INSERT_HEAD(&sc->r_used, vr, list);

	if (vr3 != NULL) {
		AZ(vr3->chunk->next);
		vr3->chunk->next = vr->off;
	} else {
		sc->head->first = vr->off;
	}
	sc->head->alloc_seq += 2;
	VWMB();
	return (vr->ptr);
}

/*--------------------------------------------------------------------
 * Free a chunk
 */

void
VSM_common_free(struct vsm_sc *sc, void *ptr)
{
	struct vsm_range *vr, *vr2;

	CHECK_OBJ_NOTNULL(sc, VSM_SC_MAGIC);
	AN(ptr);

	/* Look in used list, move to cooling list */
	VTAILQ_FOREACH(vr, &sc->r_used, list) {
		if (vr->ptr != ptr)
			continue;

		sc->g_used -= vr->len;
		sc->g_cooling += vr->len;

		vr2 = VTAILQ_NEXT(vr, list);
		VTAILQ_REMOVE(&sc->r_used, vr, list);
		VTAILQ_INSERT_TAIL(&sc->r_cooling, vr, list);
		vr->cool = VTIM_real() + 60;	/* XXX: param ? */
		if (vr2 != NULL)
			vr2->chunk->next = vr->chunk->next;
		else
			sc->head->first = vr->chunk->next;
		VWMB();
		vr->chunk->len = 0;
		sc->head->alloc_seq += 2;
		VWMB();
		return;
	}

	/* Look in bogus list, free */
	VTAILQ_FOREACH(vr, &sc->r_bogus, list) {
		if (vr->ptr != ptr)
			continue;

		sc->g_overflow -= vr->len;

		VTAILQ_REMOVE(&sc->r_bogus, vr, list);
		FREE_OBJ(vr);
		free(ptr);
		return;
	}
	/* Panic */
	assert(ptr == NULL);
}

/*--------------------------------------------------------------------
 * Delete a VSM segment
 */

void
VSM_common_delete(struct vsm_sc **scp)
{
	struct vsm_range *vr, *vr2;
	struct vsm_sc *sc;

	AN(scp);
	sc =*scp;
	*scp = NULL;

	CHECK_OBJ_NOTNULL(sc, VSM_SC_MAGIC);
	VTAILQ_FOREACH_SAFE(vr, &sc->r_free, list, vr2)
		FREE_OBJ(vr);
	VTAILQ_FOREACH_SAFE(vr, &sc->r_used, list, vr2)
		FREE_OBJ(vr);
	VTAILQ_FOREACH_SAFE(vr, &sc->r_cooling, list, vr2)
		FREE_OBJ(vr);
	VTAILQ_FOREACH_SAFE(vr, &sc->r_bogus, list, vr2) {
		free(vr->ptr);
		FREE_OBJ(vr);
	}

	/* Mark VSM as abandoned */
	sc->head->alloc_seq = 0;

	VWMB();
	FREE_OBJ(sc);
}

/*--------------------------------------------------------------------
 * Copy all chunks in one VSM segment to another VSM segment
 */

void
VSM_common_copy(struct vsm_sc *to, const struct vsm_sc *from)
{
	struct vsm_range *vr;
	void *p;

	CHECK_OBJ_NOTNULL(to, VSM_SC_MAGIC);
	CHECK_OBJ_NOTNULL(from, VSM_SC_MAGIC);
	VTAILQ_FOREACH(vr, &from->r_used, list) {
		p = VSM_common_alloc(to, vr->chunk->len,
		    vr->chunk->class, vr->chunk->type, vr->chunk->ident);
		AN(p);
		memcpy(p, vr->chunk + 1, vr->chunk->len);
	}
}

/*--------------------------------------------------------------------
 * Update age
 */

void
VSM_common_ageupdate(struct vsm_sc *sc)
{

	CHECK_OBJ_NOTNULL(sc, VSM_SC_MAGIC);
	sc->head->age = VTIM_mono() - sc->t0;
}
