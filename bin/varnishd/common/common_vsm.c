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
 * We have three potential conflicts we need to deal with:
 *
 * VSM-studying programs (varnishstat...) vs. everybody else
 *	The VSM studying programs only have read-only access to the VSM
 *	so everybody else must use memory barriers, stable storage and
 *	similar tricks to keep the VSM image in sync (long enough) for
 *	the studying programs.
 *	It can not be prevented, and may indeed in some cases be
 *	desirable for such programs to write to VSM, for instance to
 *	zero counters.
 *	Varnishd should never trust the integrity of VSM content.
 *
 * Manager process vs child process.
 *	The manager will create a fresh VSM for each child process launch
 *	and not muck about with VSM while the child runs.  If the child
 *	crashes, the panicstring will be evacuated and the VSM possibly
 *	saved for debugging, and a new VSM created before the child is
 *	started again.
 *
 * Child process threads
 *	Pthread locking necessary.
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
	unsigned 			magic;
#define VSM_RANGE_MAGIC			0x8d30f14
	VTAILQ_ENTRY(vsm_range)		list;
	unsigned			off; 
	unsigned			len;
	double				cool;
	struct VSM_chunk		*chunk;
	void				*ptr;
};

struct vsm_sc {
	unsigned 			magic;
#define VSM_SC_MAGIC			0x8b83270d
	char				*b;
	unsigned			len;
	struct VSM_head			*head;
	VTAILQ_HEAD(,vsm_range)		r_used;
	VTAILQ_HEAD(,vsm_range)		r_cooling;
	VTAILQ_HEAD(,vsm_range)		r_free;
	VTAILQ_HEAD(,vsm_range)		r_bogus;
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
VSM_common_new(void *p, unsigned l)
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

	sc->head = (void *)sc->b;
	memset(TRUST_ME(sc->head), 0, sizeof *sc->head);
	sc->head->magic = VSM_HEAD_MAGIC;
	sc->head->hdrsize = sizeof *sc->head;
	sc->head->shm_size = l;

	ALLOC_OBJ(vr, VSM_RANGE_MAGIC);
	AN(vr);
	vr->off = PRNDUP(sizeof(*sc->head));
	vr->len = l - vr->off;
	VTAILQ_INSERT_TAIL(&sc->r_free, vr, list);
	return (sc);
}

/*--------------------------------------------------------------------
 * Allocate a chunk from VSM
 */

void *
VSM_common_alloc(struct vsm_sc *sc, unsigned size,
    const char *class, const char *type, const char *ident)
{
	struct vsm_range *vr, *vr2, *vr3;
	double now = VTIM_real();
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

	/* Move cooled off stuff to free list */
	VTAILQ_FOREACH_SAFE(vr, &sc->r_cooling, list, vr2) {
		if (vr->cool > now)
			break;
		VTAILQ_REMOVE(&sc->r_cooling, vr, list);
		vsm_common_insert_free(sc, vr);
	}

	size = PRNDUP(size);
	l1 = size + sizeof(struct VSM_chunk);
	l2 = size + 2 * sizeof(struct VSM_chunk);

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
		VTAILQ_INSERT_TAIL(&sc->r_bogus, vr, list);
		/* XXX: log + stats */
		return (vr->ptr);
	}

	/* XXX: stats ? */

	/* Zero the entire allocation, to avoid garbage confusing readers */
	memset(TRUST_ME(sc->b + vr->off), 0, vr->len);

	vr->chunk = (void *)(sc->b + vr->off);
	vr->ptr = (vr->chunk + 1);

	vr->chunk->magic = VSM_CHUNK_MAGIC;
	strcpy(TRUST_ME(vr->chunk->class), class);
	strcpy(TRUST_ME(vr->chunk->type), type);
	strcpy(TRUST_ME(vr->chunk->ident), ident);
	VWMB();

	vr3 = VTAILQ_FIRST(&sc->r_used);
	VTAILQ_INSERT_HEAD(&sc->r_used, vr, list);

	if (vr3 != NULL) {	
		AZ(vr3->chunk->next);
		vr3->chunk->next = vr->off;
	} else {
		sc->head->first = vr->off;
	}
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
		/* XXX: stats ? */
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
		VWMB();
		return;
	}
	/* Look in bogus list, free */
	VTAILQ_FOREACH(vr, &sc->r_bogus, list) {
		if (vr->ptr == ptr) {
			VTAILQ_REMOVE(&sc->r_bogus, vr, list);
			FREE_OBJ(vr);
			/* XXX: stats ? */
			free(TRUST_ME(ptr));
			return;
		}
	}
	/* Panic */
	assert(ptr == "Bogus pointer freed");
}

/*--------------------------------------------------------------------
 * Delete a VSM segment
 */

void
VSM_common_delete(struct vsm_sc *sc)
{
	struct vsm_range *vr, *vr2;

	CHECK_OBJ_NOTNULL(sc, VSM_SC_MAGIC);
	VTAILQ_FOREACH_SAFE(vr, &sc->r_free, list, vr2)
		FREE_OBJ(vr);
	VTAILQ_FOREACH_SAFE(vr, &sc->r_used, list, vr2)
		FREE_OBJ(vr);
	VTAILQ_FOREACH_SAFE(vr, &sc->r_cooling, list, vr2)
		FREE_OBJ(vr);
	VTAILQ_FOREACH_SAFE(vr, &sc->r_bogus, list, vr2) {
		free(TRUST_ME(vr->ptr));
		FREE_OBJ(vr);
	}
	sc->head->magic = 0;
	FREE_OBJ(sc);
}
