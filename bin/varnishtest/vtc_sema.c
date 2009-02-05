/*
 * Copyright (c) 2006-2008 Linpro AS
 * All rights reserved.
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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include "vtc.h"

#include "vqueue.h"
#include "miniobj.h"
#include "libvarnish.h"

struct sema {
	unsigned		magic;
#define SEMA_MAGIC		0x29b64317
	char			*name;
	VTAILQ_ENTRY(sema)	list;
	pthread_mutex_t		mtx;
	pthread_cond_t		cond;

	unsigned		waiters;
	unsigned		expected;
};

static pthread_mutex_t		sema_mtx;
static VTAILQ_HEAD(, sema)	semas = VTAILQ_HEAD_INITIALIZER(semas);

/**********************************************************************
 * Allocate and initialize a sema
 */

static struct sema *
sema_new(char *name, struct vtclog *vl)
{
	struct sema *r;

	ALLOC_OBJ(r, SEMA_MAGIC);
	AN(r);
	r->name = name;
	if (*name != 'r')
		vtc_log(vl, 0, "Sema name must start with 'r' (%s)", *name);

	AZ(pthread_mutex_init(&r->mtx, NULL));
	AZ(pthread_cond_init(&r->cond, NULL));
	r->waiters = 0;
	r->expected = 0;
	VTAILQ_INSERT_TAIL(&semas, r, list);
	return (r);
}

/**********************************************************************
 * Sync a sema
 */

static void
sema_sync(struct sema *r, const char *av, struct vtclog *vl)
{
	unsigned u;

	CHECK_OBJ_NOTNULL(r, SEMA_MAGIC);
	u = strtoul(av, NULL, 0);

	AZ(pthread_mutex_lock(&r->mtx));
	if (r->expected == 0)
		r->expected = u;
	if (r->expected != u)
		vtc_log(vl, 0,
		    "Sema(%s) use error: different expectations (%u vs %u)",
		    r->name, r->expected, u);

	if (++r->waiters == r->expected) {
		vtc_log(vl, 4, "Sema(%s) wake %u", r->name, r->expected);
		AZ(pthread_cond_broadcast(&r->cond));
		r->waiters = 0;
		r->expected = 0;
	} else {
		vtc_log(vl, 4, "Sema(%s) wait %u of %u",
		    r->name, r->waiters, r->expected);
		AZ(pthread_cond_wait(&r->cond, &r->mtx));
	}
	AZ(pthread_mutex_unlock(&r->mtx));
}

/**********************************************************************
 * Semaphore command dispatch
 */

void
cmd_sema(CMD_ARGS)
{
	struct sema *r, *r2;

	(void)priv;
	(void)cmd;

	if (av == NULL) {
		AZ(pthread_mutex_lock(&sema_mtx));
		/* Reset and free */
		VTAILQ_FOREACH_SAFE(r, &semas, list, r2) {
			AZ(pthread_mutex_lock(&r->mtx));
			AZ(r->waiters);
			AZ(r->expected);
			AZ(pthread_mutex_unlock(&r->mtx));
		}
		AZ(pthread_mutex_unlock(&sema_mtx));
		return;
	}

	assert(!strcmp(av[0], "sema"));
	av++;

	AZ(pthread_mutex_lock(&sema_mtx));
	VTAILQ_FOREACH(r, &semas, list)
		if (!strcmp(r->name, av[0]))
			break;
	if (r == NULL)
		r = sema_new(av[0], vl);
	AZ(pthread_mutex_unlock(&sema_mtx));
	av++;

	for (; *av != NULL; av++) {
		if (!strcmp(*av, "sync")) {
			av++;
			AN(*av);
			sema_sync(r, *av, vl);
			continue;
		}
		vtc_log(vl, 0, "Unknown sema argument: %s", *av);
	}
}

void
init_sema(void)
{
	AZ(pthread_mutex_init(&sema_mtx, NULL));
}
