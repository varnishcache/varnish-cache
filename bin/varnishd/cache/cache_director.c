/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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

#include "cache_director.h"

/* Resolve director --------------------------------------------------*/

static const struct director *
vdi_resolve(struct worker *wrk, struct busyobj *bo)
{
	const struct director *d;
	const struct director *d2;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	for (d = bo->director_req; d != NULL && d->resolve != NULL; d = d2) {
		CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
		d2 = d->resolve(d, wrk, bo);
		if (d2 == NULL)
			VSLb(bo->vsl, SLT_FetchError,
			    "Director %s returned no backend", d->vcl_name);
	}
	CHECK_OBJ_ORNULL(d, DIRECTOR_MAGIC);
	if (d == NULL)
		VSLb(bo->vsl, SLT_FetchError, "No backend");
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

	d = vdi_resolve(wrk, bo);
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
VDI_GetBody(struct worker *wrk, struct busyobj *bo)
{
	const struct director *d;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	d = bo->director_resp;
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	AZ(d->resolve);
	AN(d->getbody);

	assert(bo->director_state == DIR_S_HDRS);
	bo->director_state = DIR_S_BODY;
	return (d->getbody(d, wrk, bo));
}

/* Get IP number (if any ) -------------------------------------------*/

const struct suckaddr *
VDI_GetIP(struct worker *wrk, struct busyobj *bo)
{
	const struct director *d;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	d = bo->director_resp;
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	assert(bo->director_state == DIR_S_HDRS ||
	   bo->director_state == DIR_S_BODY);
	AZ(d->resolve);
	if (d->getip == NULL)
		return (NULL);
	return (d->getip(d, wrk, bo));
}

/* Finish fetch ------------------------------------------------------*/

void
VDI_Finish(struct worker *wrk, struct busyobj *bo)
{
	const struct director *d;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	d = bo->director_resp;
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);

	AZ(d->resolve);
	AN(d->finish);

	assert(bo->director_state != DIR_S_NULL);
	d->finish(d, wrk, bo);
	bo->director_state = DIR_S_NULL;
}

/* Get a connection --------------------------------------------------*/

int
VDI_Http1Pipe(struct req *req, struct busyobj *bo)
{
	const struct director *d;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	d = vdi_resolve(req->wrk, bo);
	if (d == NULL || d->http1pipe == NULL)
		return (-1);
	d->http1pipe(d, req, bo);
	return (0);
}

/* Check health --------------------------------------------------------
 *
 * If director has no healthy method, we just assume it is healthy.
 */

int
VDI_Healthy(const struct director *d, const struct busyobj *bo)
{

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	if (d->healthy == NULL)
		return (1);
	return (d->healthy(d, bo, NULL));
}
