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
 * Handle backend connections and backend request structures.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include "cache.h"

static struct lock nbusyobj_mtx;
static struct busyobj *nbusyobj;

void
VBO_Init(void)
{
	Lck_New(&nbusyobj_mtx, lck_nbusyobj);
	nbusyobj = NULL;
}

/*--------------------------------------------------------------------
 * BusyObj handling
 */

static struct busyobj *
vbo_NewBusyObj(void)
{
	struct busyobj *busyobj;

	ALLOC_OBJ(busyobj, BUSYOBJ_MAGIC);
	AN(busyobj);
	Lck_New(&busyobj->mtx, lck_busyobj);
	return (busyobj);
}

static void
vbe_FreeBusyObj(struct busyobj *busyobj)
{
	CHECK_OBJ_NOTNULL(busyobj, BUSYOBJ_MAGIC);
	AZ(busyobj->refcount);
	Lck_Delete(&busyobj->mtx);
	FREE_OBJ(busyobj);
}

struct busyobj *
VBO_GetBusyObj(struct worker *wrk)
{
	struct busyobj *busyobj = NULL;

	(void)wrk;
	Lck_Lock(&nbusyobj_mtx);
	if (nbusyobj != NULL) {
		CHECK_OBJ_NOTNULL(nbusyobj, BUSYOBJ_MAGIC);
		busyobj = nbusyobj;
		nbusyobj = NULL;
		memset((char *)busyobj + offsetof(struct busyobj, refcount), 0,
		       sizeof *busyobj - offsetof(struct busyobj, refcount));
	}
	Lck_Unlock(&nbusyobj_mtx);
	if (busyobj == NULL)
		busyobj = vbo_NewBusyObj();
	AN(busyobj);
	busyobj->refcount = 1;
	busyobj->beresp = wrk->x_beresp;
	busyobj->bereq = wrk->x_bereq;
	return (busyobj);
}

struct busyobj *
VBO_RefBusyObj(struct busyobj *busyobj)
{
	CHECK_OBJ_NOTNULL(busyobj, BUSYOBJ_MAGIC);
	Lck_Lock(&busyobj->mtx);
	assert(busyobj->refcount > 0);
	busyobj->refcount++;
	Lck_Unlock(&busyobj->mtx);
	return (busyobj);
}

void
VBO_DerefBusyObj(struct worker *wrk, struct busyobj **pbo)
{
	struct busyobj *busyobj;

	(void)wrk;
	busyobj = *pbo;
	CHECK_OBJ_NOTNULL(busyobj, BUSYOBJ_MAGIC);
	Lck_Lock(&busyobj->mtx);
	assert(busyobj->refcount > 0);
	busyobj->refcount--;
	*pbo = NULL;
	if (busyobj->refcount > 0) {
		Lck_Unlock(&busyobj->mtx);
		return;
	}
	Lck_Unlock(&busyobj->mtx);

	/* XXX Sanity checks e.g. AZ(busyobj->vbc) */

	Lck_Lock(&nbusyobj_mtx);
	if (nbusyobj == NULL) {
		nbusyobj = busyobj;
		busyobj = NULL;
	}
	Lck_Unlock(&nbusyobj_mtx);
	if (busyobj != NULL)
		vbe_FreeBusyObj(busyobj);
}
