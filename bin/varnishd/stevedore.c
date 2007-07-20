/*-
 * Copyright (c) 2007 Linpro AS
 * All rights reserved.
 *
 * Author: Dag-Erling Smørgav <des@linpro.no>
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
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "heritage.h"

extern struct stevedore sma_stevedore;
extern struct stevedore smf_stevedore;


struct storage *
STV_alloc(size_t size)
{
	struct storage *st;
	struct stevedore *stv, *stv_first;
	struct stevedore_head *stevedore_h = &heritage.stevedore_h;

	/* Simple round robin selecting of a stevedore. */
	pthread_mutex_lock(&heritage.stevedore_lock);
	stv_first = TAILQ_FIRST(stevedore_h);
	stv = stv_first;
	do {
		AN(stv->alloc);
		st = stv->alloc(stv, size);
		TAILQ_REMOVE(stevedore_h, stv, stevedore_list);
		TAILQ_INSERT_TAIL(stevedore_h, stv, stevedore_list);
		if (st != NULL) {
			pthread_mutex_unlock(&heritage.stevedore_lock);
			return (st);
		}
	} while ((stv = TAILQ_FIRST(stevedore_h)) != stv_first);
	
	/* No stevedore with enough space is found. Make room in the first
	 * one in the list, and move it to the end. Ensuring the round-robin.
	 */
	stv = TAILQ_FIRST(stevedore_h);
	TAILQ_REMOVE(stevedore_h, stv, stevedore_list);
	TAILQ_INSERT_TAIL(stevedore_h, stv, stevedore_list);
	pthread_mutex_unlock(&heritage.stevedore_lock);
	
	do {
		if ((st = stv->alloc(stv, size)) == NULL)
			AN(LRU_DiscardOne());
	} while (st == NULL);
	
	return (st);
}

void
STV_trim(struct storage *st, size_t size)
{

	AN(st->stevedore);
	if (st->stevedore->trim)
		st->stevedore->trim(st, size);
}

void
STV_free(struct storage *st)
{

	AN(st->stevedore);
	AN(st->stevedore->free);
	st->stevedore->free(st);
}

static int
cmp_storage(struct stevedore *s, const char *p, const char *q)
{
	if (strlen(s->name) != q - p)
		return (1);
	if (strncmp(s->name, p, q - p))
		return (1);
	return (0);
}

void
STV_add(const char *spec)
{
	const char *p, *q;
	struct stevedore *stp;

	p = strchr(spec, ',');
	if (p == NULL)
		q = p = strchr(spec, '\0');
	else
		q = p + 1;
	xxxassert(p != NULL);
	xxxassert(q != NULL);
	
	stp = malloc(sizeof *stp);
	
	if (!cmp_storage(&sma_stevedore, spec, p)) {
		*stp = sma_stevedore;
	} else if (!cmp_storage(&smf_stevedore, spec, p)) {
		*stp = smf_stevedore;
	} else {
		fprintf(stderr, "Unknown storage method \"%.*s\"\n",
		    (int)(p - spec), spec);
		exit (2);
	}
	TAILQ_INSERT_HEAD(&heritage.stevedore_h, stp, stevedore_list);
	if (stp->init != NULL)
		stp->init(stp, q);
}

void 
STV_open(void)
{
	struct stevedore_head *stevedore_h;
	struct stevedore *st;
	
	stevedore_h = &heritage.stevedore_h;
	TAILQ_FOREACH(st, stevedore_h, stevedore_list) {
		if (st->open != NULL)
			st->open(st);
	}
	pthread_mutex_init(&heritage.stevedore_lock, NULL);
}
