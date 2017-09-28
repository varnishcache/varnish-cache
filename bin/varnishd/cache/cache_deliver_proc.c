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
 */

#include "config.h"

#include "cache_priv.h"
#include "cache_filter.h"

/* VDP_bytes
 *
 * Pushes len bytes at ptr down the delivery processor list.
 *
 * This function picks and calls the next delivery processor from the
 * list. The return value is the return value of the delivery
 * processor. Upon seeing a non-zero return value, that lowest value
 * observed is latched in req->vdpe_retval and all subsequent calls to
 * VDP_bytes will return that value directly without calling the next
 * processor.
 *
 * Valid return values (of VDP_bytes and any VDP function):
 * r < 0:  Error, breaks out early on an error condition
 * r == 0: Continue
 * r > 0:  Stop, breaks out early without error condition
 */
int
VDP_bytes(struct req *req, enum vdp_action act, const void *ptr, ssize_t len)
{
	int retval;
	struct vdp_entry *vdpe;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	assert(act == VDP_NULL || act == VDP_FLUSH);
	if (req->vdpe_retval)
		return (req->vdpe_retval);
	vdpe = req->vdpe_nxt;
	CHECK_OBJ_NOTNULL(vdpe, VDP_ENTRY_MAGIC);
	req->vdpe_nxt = VTAILQ_NEXT(vdpe, list);

	assert(act > VDP_NULL || len > 0);
	/* Call the present layer, while pointing to the next layer down */
	retval = vdpe->vdp->func(req, act, &vdpe->priv, ptr, len);
	if (retval && (req->vdpe_retval == 0 || retval < req->vdpe_retval))
		req->vdpe_retval = retval; /* Latch error value */
	req->vdpe_nxt = vdpe;
	return (req->vdpe_retval);
}

void
VDP_push(struct req *req, const struct vdp *vdp, void *priv, int bottom)
{
	struct vdp_entry *vdpe;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AN(vdp);
	AN(vdp->name);
	AN(vdp->func);

	vdpe = WS_Alloc(req->ws, sizeof *vdpe);
	if (vdpe == NULL)
		return;
	INIT_OBJ(vdpe, VDP_ENTRY_MAGIC);
	vdpe->vdp = vdp;
	vdpe->priv = priv;
	if (bottom)
		VTAILQ_INSERT_TAIL(&req->vdpe, vdpe, list);
	else
		VTAILQ_INSERT_HEAD(&req->vdpe, vdpe, list);
	req->vdpe_nxt = VTAILQ_FIRST(&req->vdpe);

	AZ(vdpe->vdp->func(req, VDP_INIT, &vdpe->priv, NULL, 0));
}

void
VDP_close(struct req *req)
{
	struct vdp_entry *vdpe;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	while (!VTAILQ_EMPTY(&req->vdpe)) {
		vdpe = VTAILQ_FIRST(&req->vdpe);
		CHECK_OBJ_NOTNULL(vdpe, VDP_ENTRY_MAGIC);
		VTAILQ_REMOVE(&req->vdpe, vdpe, list);
		AZ(vdpe->vdp->func(req, VDP_FINI, &vdpe->priv, NULL, 0));
		AZ(vdpe->priv);
		req->vdpe_nxt = VTAILQ_FIRST(&req->vdpe);
	}
}

/*--------------------------------------------------------------------*/

static int __match_proto__(objiterate_f)
vdp_objiterator(void *priv, int flush, const void *ptr, ssize_t len)
{
	return (VDP_bytes(priv, flush ? VDP_FLUSH : VDP_NULL, ptr, len));
}


int
VDP_DeliverObj(struct req *req)
{
	int r;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	r = ObjIterate(req->wrk, req->objcore, req, vdp_objiterator,
	    req->objcore->flags & OC_F_PRIVATE ? 1 : 0);
	if (r < 0)
		return (r);
	return (0);
}
