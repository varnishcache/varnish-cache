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
 */

#include "config.h"

#include "cache.h"
#include "cache_filter.h"

int
VDP_bytes(struct req *req, enum vdp_action act, const void *ptr, ssize_t len)
{
	int retval;
	struct vdp_entry *vdp;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	vdp = req->vdp_nxt;
	CHECK_OBJ_NOTNULL(vdp, VDP_ENTRY_MAGIC);
	req->vdp_nxt = VTAILQ_NEXT(vdp, list);

	assert(act > VDP_NULL || len > 0);
	/* Call the present layer, while pointing to the next layer down */
	retval = vdp->func(req, act, &vdp->priv, ptr, len);
	req->vdp_nxt = vdp;
	return (retval);
}

void
VDP_push(struct req *req, vdp_bytes *func, void *priv)
{
	struct vdp_entry *vdp;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AN(func);

	vdp = WS_Alloc(req->ws, sizeof *vdp);
	AN(vdp);
	memset(vdp, 0, sizeof *vdp);
	vdp->magic = VDP_ENTRY_MAGIC;
	vdp->func = func;
	vdp->priv = priv;
	VTAILQ_INSERT_HEAD(&req->vdp, vdp, list);
	req->vdp_nxt = vdp;

	AZ(func(req, VDP_INIT, &vdp->priv, NULL, 0));
}

void
VDP_pop(struct req *req, vdp_bytes *func)
{
	struct vdp_entry *vdp;
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	vdp = VTAILQ_FIRST(&req->vdp);
	CHECK_OBJ_NOTNULL(vdp, VDP_ENTRY_MAGIC);
	assert(vdp->func == func);
	VTAILQ_REMOVE(&req->vdp, vdp, list);
	AZ(vdp->func(req, VDP_FINI, &vdp->priv, NULL, 0));
	AZ(vdp->priv);
	req->vdp_nxt = VTAILQ_FIRST(&req->vdp);
}
