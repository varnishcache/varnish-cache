/*-
 * Copyright (c) 2005 Varnish Software AS
 * All rights reserved.
 *
 * Author: Dridi Boukelmoune <dridi@varnish-software.com>
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

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vtc.h"

enum barrier_e {
	BARRIER_NONE = 0,
	BARRIER_COND,
	BARRIER_SOCK,
};

struct barrier {
	unsigned		magic;
#define BARRIER_MAGIC		0x7b54c275
	char			*name;
	VTAILQ_ENTRY(barrier)	list;
	pthread_mutex_t		mtx;
	pthread_cond_t		cond;

	unsigned		waiters;
	unsigned		expected;
	unsigned		cyclic;

	enum barrier_e		type;
};

static pthread_mutex_t		barrier_mtx;
static VTAILQ_HEAD(, barrier)	barriers = VTAILQ_HEAD_INITIALIZER(barriers);

static struct barrier *
barrier_new(char *name, struct vtclog *vl)
{
	struct barrier *b;

	ALLOC_OBJ(b, BARRIER_MAGIC);
	AN(b);
	AN(name);
	if (*name != 'b')
		vtc_log(vl, 0, "Barrier name must start with 'b' (%s)", name);
	REPLACE(b->name, name);

	AZ(pthread_mutex_init(&b->mtx, NULL));
	AZ(pthread_cond_init(&b->cond, NULL));
	b->waiters = 0;
	b->expected = 0;
	VTAILQ_INSERT_TAIL(&barriers, b, list);
	return (b);
}

/**********************************************************************
 * Init a barrier
 */

static void
barrier_expect(struct barrier *b, const char *av, struct vtclog *vl)
{
	unsigned expected;

	if (b->type != BARRIER_NONE)
		vtc_log(vl, 0,
		    "Barrier(%s) use error: already initialized", b->name);

	AZ(b->expected);
	AZ(b->waiters);
	expected = strtoul(av, NULL, 0);
	if (expected < 2)
		vtc_log(vl, 0,
		    "Barrier(%s) use error: wrong expectation (%u)",
		    b->name, expected);

	b->expected = expected;
}

static void
barrier_cond(struct barrier *b, const char *av, struct vtclog *vl)
{

	CHECK_OBJ_NOTNULL(b, BARRIER_MAGIC);
	barrier_expect(b, av, vl);
	b->type = BARRIER_COND;
}

static void
barrier_sock(struct barrier *b, const char *av, struct vtclog *vl)
{

	CHECK_OBJ_NOTNULL(b, BARRIER_MAGIC);
	barrier_expect(b, av, vl);
	b->type = BARRIER_SOCK;
	INCOMPL();
}

static void
barrier_cyclic(struct barrier *b, struct vtclog *vl)
{

	CHECK_OBJ_NOTNULL(b, BARRIER_MAGIC);

	if (b->type == BARRIER_NONE)
		vtc_log(vl, 0,
		    "Barrier(%s) use error: not initialized", b->name);

	if (b->waiters != 0)
		vtc_log(vl, 0,
		    "Barrier(%s) use error: already in use", b->name);

	b->cyclic = 1;
}

/**********************************************************************
 * Sync a barrier
 */

static void
barrier_cond_sync(struct barrier *b, struct vtclog *vl)
{

	CHECK_OBJ_NOTNULL(b, BARRIER_MAGIC);
	assert(b->type == BARRIER_COND);

	assert(b->waiters <= b->expected);
	if (b->waiters == b->expected)
		vtc_log(vl, 0,
		    "Barrier(%s) use error: more waiters than the %u expected",
		    b->name, b->expected);

	if (++b->waiters == b->expected) {
		vtc_log(vl, 4, "Barrier(%s) wake %u", b->name, b->expected);
		AZ(pthread_cond_broadcast(&b->cond));
	}
	else {
		vtc_log(vl, 4, "Barrier(%s) wait %u of %u",
		    b->name, b->waiters, b->expected);
		AZ(pthread_cond_wait(&b->cond, &b->mtx));
	}

	if (b->cyclic)
		b->waiters = 0;
}

static void
barrier_sync(struct barrier *b, struct vtclog *vl)
{

	CHECK_OBJ_NOTNULL(b, BARRIER_MAGIC);

	switch (b->type) {
	case BARRIER_NONE:
		vtc_log(vl, 0,
		    "Barrier(%s) use error: not initialized", b->name);
	case BARRIER_COND:
		barrier_cond_sync(b, vl);
		break;
	case BARRIER_SOCK:
		INCOMPL();
		break;
	default:
		WRONG("Wrong barrier type");
	}
}

/**********************************************************************
 * Barrier command dispatch
 */

void
cmd_barrier(CMD_ARGS)
{
	struct barrier *b, *b2;

	(void)priv;
	(void)cmd;

	if (av == NULL) {
		AZ(pthread_mutex_lock(&barrier_mtx));
		/* Reset and free */
		VTAILQ_FOREACH_SAFE(b, &barriers, list, b2) {
			AZ(pthread_mutex_lock(&b->mtx));
			assert(b->type != BARRIER_NONE);
			if (b->cyclic)
				AZ(b->waiters);
			else
				assert(b->waiters == b->expected);
			AZ(pthread_mutex_unlock(&b->mtx));
		}
		AZ(pthread_mutex_unlock(&barrier_mtx));
		return;
	}

	AZ(strcmp(av[0], "barrier"));
	av++;

	AZ(pthread_mutex_lock(&barrier_mtx));
	VTAILQ_FOREACH(b, &barriers, list)
		if (!strcmp(b->name, av[0]))
			break;
	if (b == NULL)
		b = barrier_new(av[0], vl);
	av++;
	AZ(pthread_mutex_lock(&b->mtx));
	AZ(pthread_mutex_unlock(&barrier_mtx));

	for (; *av != NULL; av++) {
		if (!strcmp(*av, "cond")) {
			av++;
			AN(*av);
			barrier_cond(b, *av, vl);
			continue;
		}
		if (!strcmp(*av, "sock")) {
			av++;
			AN(*av);
			barrier_sock(b, *av, vl);
			continue;
		}
		if (!strcmp(*av, "sync")) {
			barrier_sync(b, vl);
			continue;
		}
		if (!strcmp(*av, "-cyclic")) {
			barrier_cyclic(b, vl);
			continue;
		}
		vtc_log(vl, 0, "Unknown barrier argument: %s", *av);
	}
	AZ(pthread_mutex_unlock(&b->mtx));
}

void
init_barrier(void)
{

	AZ(pthread_mutex_init(&barrier_mtx, NULL));
}
