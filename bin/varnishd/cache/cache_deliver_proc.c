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

#include "cache_varnishd.h"
#include "cache_filter.h"

/* VDP_bytes
 *
 * Pushes len bytes at ptr down the delivery processor list.
 *
 * This function picks and calls the next delivery processor from the
 * list. The return value is the return value of the delivery
 * processor. Upon seeing a non-zero return value, that lowest value
 * observed is latched in ->retval and all subsequent calls to
 * VDP_bytes will return that value directly without calling the next
 * processor.
 *
 * Valid return values (of VDP_bytes and any VDP function):
 * r < 0:  Error, breaks out early on an error condition
 * r == 0: Continue
 * r > 0:  Stop, breaks out early without error condition
 */
int
VDP_bytes(struct req *req, enum vdp_flush flush, const void *ptr, ssize_t len)
{
	enum vdp_status status;
	struct vdp_entry *vdpe;
	struct vdp_ctx *vdc;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	vdc = req->vdc;
	if (vdc->status != VDP_OK)
		return (vdc->status);
	vdpe = vdc->nxt;
	CHECK_OBJ_NOTNULL(vdpe, VDP_ENTRY_MAGIC);

	/* We should be called only once with the VDP_LAST flag */
	AZ(vdpe->f_seen_last);

	/* Call the present layer, while pointing to the next layer down */
	vdc->nxt = VTAILQ_NEXT(vdpe, list);
	CHECK_OBJ_ORNULL(vdc->nxt, VDP_ENTRY_MAGIC);
	status = vdpe->vdp->bytes(req, flush, &vdpe->priv, ptr, len);
	vdc->nxt = vdpe;
	if (status != VDP_OK && (vdc->status == VDP_OK || status < vdc->status))
		vdc->status = status; /* Latch error status */
	if (flush == VDP_LAST)
		vdpe->f_seen_last = 1;

	return (vdc->status);
}

int
VDP_push(struct req *req, const struct vdp *vdp, void *priv, int bottom)
{
	struct vdp_entry *vdpe;
	struct vdp_ctx *vdc;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	vdc = req->vdc;
	AN(vdp);
	AN(vdp->name);
	AN(vdp->bytes);

	if (vdc->status != VDP_OK)
		return (vdc->status);

	if (DO_DEBUG(DBG_PROCESSORS))
		VSLb(req->vsl, SLT_Debug, "VDP_push(%s)", vdp->name);

	vdpe = WS_Alloc(req->ws, sizeof *vdpe);
	if (vdpe == NULL) {
		VSLb(req->vsl, SLT_Error,
		    "VDP_push(%s) workspace_client overflow", vdp->name);
		vdc->status = VDP_ERROR;
		return (vdc->status);
	}
	INIT_OBJ(vdpe, VDP_ENTRY_MAGIC);
	vdpe->vdp = vdp;
	vdpe->priv = priv;
	if (bottom)
		VTAILQ_INSERT_TAIL(&vdc->vdp, vdpe, list);
	else
		VTAILQ_INSERT_HEAD(&vdc->vdp, vdpe, list);
	vdc->nxt = VTAILQ_FIRST(&vdc->vdp);

	if (vdpe->vdp->init)
		vdc->status = vdpe->vdp->init(req, &vdpe->priv);
	return (vdc->status);
}

void
VDP_close(struct req *req)
{
	struct vdp_entry *vdpe;
	struct vdp_ctx *vdc;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	vdc = req->vdc;
	while (!VTAILQ_EMPTY(&vdc->vdp)) {
		vdpe = VTAILQ_FIRST(&vdc->vdp);
		CHECK_OBJ_NOTNULL(vdpe, VDP_ENTRY_MAGIC);
		VTAILQ_REMOVE(&vdc->vdp, vdpe, list);
		if (vdpe->vdp->fini) {
			vdpe->vdp->fini(req, &vdpe->priv);
			AZ(vdpe->priv);
		}
		vdc->nxt = VTAILQ_FIRST(&vdc->vdp);
	}
}

/*--------------------------------------------------------------------*/

static int v_matchproto_(objiterate_f)
vdp_objiterator(void *priv, int flush, int last, const void *ptr, ssize_t len)
{
	if (last)
		return (VDP_bytes(priv, VDP_LAST, ptr, len));
	return (VDP_bytes(priv, flush ? VDP_FLUSH : VDP_NULL, ptr, len));
}


int
VDP_DeliverObj(struct req *req)
{
	int r;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	if (req->vdc->status != VDP_OK)
		return (req->vdc->status);
	r = ObjIterate(req->wrk, req->objcore, req, vdp_objiterator,
	    req->objcore->flags & OC_F_PRIVATE ? 1 : 0);
	if (r < 0 && req->vdc->status == VDP_OK)
		/* Streaming fetch error */
		req->vdc->status = VDP_ERROR_FETCH;
	return (req->vdc->status);
}
