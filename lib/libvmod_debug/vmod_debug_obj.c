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

#include "vrt.h"
#include "vcc_if.h"

struct vmod_debug_obj {
	unsigned		magic;
#define VMOD_DEBUG_OBJ_MAGIC	0xccbd9b77
	int foobar;
};

VCL_VOID
vmod_obj__init(struct req *req, struct vmod_debug_obj **op, VCL_STRING s)
{
	struct vmod_debug_obj *o;

	(void)req;
	(void)s;
	AN(op);
	AZ(*op);
	ALLOC_OBJ(o, VMOD_DEBUG_OBJ_MAGIC);
	AN(o);
	*op = o;
	o->foobar = 42;
	AN(*op);
}

VCL_VOID
vmod_obj__fini(struct req *req, struct vmod_debug_obj **op)
{

	(void)req;
	AN(op);
	AN(*op);
	FREE_OBJ(*op);
	*op = NULL;
}

VCL_STRING __match_proto__()
vmod_obj_foo(struct req *req, struct vmod_debug_obj *o, VCL_STRING s)
{
	(void)req;
	(void)s;
	CHECK_OBJ_NOTNULL(o, VMOD_DEBUG_OBJ_MAGIC);
	assert(o->foobar == 42);
	return ("BOO");
}

VCL_TIME __match_proto__()
vmod_obj_date(struct req *req, struct vmod_debug_obj *o)
{
	(void)req;
	CHECK_OBJ_NOTNULL(o, VMOD_DEBUG_OBJ_MAGIC);
	assert(o->foobar == 42);
	return (21.4);
}

/*----------------------------------------------------------------------*/

struct vmod_debug_rr_entry {
	unsigned				magic;
#define VMOD_DEBUG_RR_ENTRY_MAGIC		0xa80970cf
	VTAILQ_ENTRY(vmod_debug_rr_entry)	list;
	VCL_BACKEND				be;
};

struct vmod_debug_rr {
	unsigned				magic;
#define VMOD_DEBUG_RR_MAGIC			0x99f4b726
	VTAILQ_HEAD(, vmod_debug_rr_entry)	listhead;
	pthread_mutex_t				mtx;
};

VCL_VOID
vmod_rr__init(struct req *req, struct vmod_debug_rr **rrp)
{
	struct vmod_debug_rr *rr;

	(void)req;

	AN(rrp);
	AZ(*rrp);
	ALLOC_OBJ(rr, VMOD_DEBUG_RR_MAGIC);
	AN(rr);
	*rrp = rr;
	AZ(pthread_mutex_init(&rr->mtx, NULL));
	VTAILQ_INIT(&rr->listhead);
}

VCL_VOID
vmod_rr__fini(struct req *req, struct vmod_debug_rr **rrp)
{
	struct vmod_debug_rr *rr;
	struct vmod_debug_rr_entry *ep;

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
	FREE_OBJ(*rrp);
}

VCL_VOID
vmod_rr_add_backend(struct req *req, struct vmod_debug_rr * rr, VCL_BACKEND be)
{
	struct vmod_debug_rr_entry *ep;
	(void)req;

	ALLOC_OBJ(ep, VMOD_DEBUG_RR_ENTRY_MAGIC);
	AN(ep);
	ep->be = be;
	AZ(pthread_mutex_lock(&rr->mtx));
	VTAILQ_INSERT_TAIL(&rr->listhead, ep, list);
	AZ(pthread_mutex_unlock(&rr->mtx));
}

VCL_BACKEND
vmod_rr_select(struct req *req, struct vmod_debug_rr *rr)
{
	struct vmod_debug_rr_entry *ep;

	(void)req;

	CHECK_OBJ_NOTNULL(rr, VMOD_DEBUG_RR_MAGIC);
	AZ(pthread_mutex_lock(&rr->mtx));
	ep = VTAILQ_FIRST(&rr->listhead);
	VTAILQ_REMOVE(&rr->listhead, ep, list);
	VTAILQ_INSERT_TAIL(&rr->listhead, ep, list);
	AZ(pthread_mutex_unlock(&rr->mtx));
	return (ep->be);
}
