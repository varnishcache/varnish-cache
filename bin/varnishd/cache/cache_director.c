/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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
 * Abstract director API
 *
 * The abstract director API does not know how we talk to the backend or
 * if there even is one in the usual meaning of the word.
 *
 */

#include "config.h"

#include "cache.h"

// #include "cache_backend.h"
#include "cache_director.h"

/* Resolve director --------------------------------------------------*/

static const struct director *
vdi_resolve(struct worker *wrk, struct busyobj *bo, const struct director *d)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	if (d == NULL) {
		VSLb(bo->vsl, SLT_FetchError, "No backend");
		return (NULL);
	}

	while (d != NULL && d->resolve != NULL) {
		CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
		d = d->resolve(d, wrk, bo);
	}
	CHECK_OBJ_ORNULL(d, DIRECTOR_MAGIC);
	if (d == NULL)
		VSLb(bo->vsl, SLT_FetchError, "Backend selection failed");
	bo->director_resp = d;
	return (d);
}

/* Get a set of response headers -------------------------------------*/

int
VDI_GetHdr(struct worker *wrk, struct busyobj *bo)
{
	const struct director *d;
	int i = -1;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	d = vdi_resolve(wrk, bo, bo->director_req);
	if (d != NULL) {
		AN(d->gethdrs);
		bo->director_state = DIR_S_HDRS;
		i = d->gethdrs(d, wrk, bo);
	}
	if (i)
		bo->director_state = DIR_S_NULL;
	return (i);
}

/* Setup body fetch --------------------------------------------------*/

int
VDI_GetBody(const struct director *d, struct worker *wrk, struct busyobj *bo)
{

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	AZ(d->resolve);
	AN(d->getbody);

	bo->director_state = DIR_S_BODY;
	return (d->getbody(d, wrk, bo));
}

/* Finish fetch ------------------------------------------------------*/

void
VDI_Finish(const struct director *d, struct worker *wrk, struct busyobj *bo)
{

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	AZ(d->resolve);
	AN(d->finish);

	assert(bo->director_state != DIR_S_NULL);
	bo->director_state = DIR_S_NULL;

	d->finish(d, wrk, bo);
}

/* Get a connection --------------------------------------------------*/

int
VDI_GetFd(const struct director *d, struct worker *wrk, struct busyobj *bo)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);

	d = vdi_resolve(wrk, bo, d);
	if (d == NULL)
		return (-1);

	AN(d->getfd);
	return (d->getfd(d, bo));
}

/* Check health ------------------------------------------------------
 */

int
VDI_Healthy(const struct director *d, const struct busyobj *bo)
{

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	AN(d->healthy);
	return (d->healthy(d, bo, NULL));
}

/* Get suckaddr ------------------------------------------------------*/

struct suckaddr *
VDI_Suckaddr(const struct director *d, struct worker *wrk, struct busyobj *bo)
{
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);

	AN(d->suckaddr);
	return (d->suckaddr(d, wrk, bo));
}
