/*-
 * Copyright (c) 2013 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@FreeBSD.org>
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

#include <stdlib.h>

#include "cache/cache.h"
#include "cache/cache_backend.h"

#include "vrt.h"
#include "vcc_if.h"

struct rr_entry {
	unsigned				magic;
#define RR_ENTRY_MAGIC				0xa80970cf
	VTAILQ_ENTRY(rr_entry)			list;
	VCL_BACKEND				be;
};

struct vmod_directors_round_robin {
	unsigned				magic;
#define VMOD_DEBUG_RR_MAGIC			0x99f4b726
	VTAILQ_HEAD(, rr_entry)			listhead;
	int					nbe;
	pthread_mutex_t				mtx;
	struct director				*dir;
};

static unsigned
vmod_rr_healthy(const struct director *dir, const struct req *req)
{
	struct rr_entry *ep;
	struct vmod_directors_round_robin *rr;
	unsigned retval = 0;

	CAST_OBJ_NOTNULL(rr, dir->priv, VMOD_DEBUG_RR_MAGIC);
	AZ(pthread_mutex_lock(&rr->mtx));
	VTAILQ_FOREACH(ep, &rr->listhead, list) {
		if (ep->be->healthy(ep->be, req)) {
			retval = 1;
			break;
		}
	}
	AZ(pthread_mutex_unlock(&rr->mtx));
	return (retval);
}

static struct vbc *
vmod_rr_getfd(const struct director *dir, struct req *req)
{
	struct rr_entry *ep = NULL;
	struct vmod_directors_round_robin *rr;
	int i;

	CAST_OBJ_NOTNULL(rr, dir->priv, VMOD_DEBUG_RR_MAGIC);
	AZ(pthread_mutex_lock(&rr->mtx));
	for (i = 0; i < rr->nbe; i++) {
		ep = VTAILQ_FIRST(&rr->listhead);
		VTAILQ_REMOVE(&rr->listhead, ep, list);
		VTAILQ_INSERT_TAIL(&rr->listhead, ep, list);
		if (ep->be->healthy(ep->be, req))
			break;
	}
	AZ(pthread_mutex_unlock(&rr->mtx));
	if (i == rr->nbe || ep == NULL)
		return (NULL);
	return (ep->be->getfd(ep->be, req));
}

VCL_VOID
vmod_round_robin__init(struct req *req, struct vmod_directors_round_robin **rrp,
    const char *vcl_name)
{
	struct vmod_directors_round_robin *rr;

	(void)req;

	AN(rrp);
	AZ(*rrp);
	ALLOC_OBJ(rr, VMOD_DEBUG_RR_MAGIC);
	AN(rr);
	*rrp = rr;
	AZ(pthread_mutex_init(&rr->mtx, NULL));
	VTAILQ_INIT(&rr->listhead);
	ALLOC_OBJ(rr->dir, DIRECTOR_MAGIC);
	AN(rr->dir);
	REPLACE(rr->dir->vcl_name, vcl_name);
	rr->dir->priv = rr;
	rr->dir->healthy = vmod_rr_healthy;
	rr->dir->getfd = vmod_rr_getfd;
}

VCL_VOID
vmod_round_robin__fini(struct req *req, struct vmod_directors_round_robin **rrp)
{
	struct vmod_directors_round_robin *rr;
	struct rr_entry *ep;

	(void)req;

	rr = *rrp;
	*rrp = NULL;
	CHECK_OBJ_NOTNULL(rr, VMOD_DEBUG_RR_MAGIC);

	AZ(pthread_mutex_destroy(&rr->mtx));
	while (!VTAILQ_EMPTY(&rr->listhead)) {
		ep = VTAILQ_FIRST(&rr->listhead);
		VTAILQ_REMOVE(&rr->listhead, ep, list);
		FREE_OBJ(ep);
	}
	REPLACE(rr->dir->vcl_name, NULL);
	FREE_OBJ(rr->dir);
	FREE_OBJ(rr);
}

VCL_VOID
vmod_round_robin_add_backend(struct req *req, struct vmod_directors_round_robin * rr,
    VCL_BACKEND be)
{
	struct rr_entry *ep;
	(void)req;

	ALLOC_OBJ(ep, RR_ENTRY_MAGIC);
	AN(ep);
	ep->be = be;
	AZ(pthread_mutex_lock(&rr->mtx));
	VTAILQ_INSERT_TAIL(&rr->listhead, ep, list);
	rr->nbe++;
	AZ(pthread_mutex_unlock(&rr->mtx));
}

VCL_BACKEND __match_proto__()
vmod_round_robin_backend(struct req *req, struct vmod_directors_round_robin *rr)
{
	(void)req;
	return (rr->dir);
}
