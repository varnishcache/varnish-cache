/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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
 * VDP_END marks the end of successful processing, it is issued by
 * VDP_DeliverObj() and may also be sent downstream by processors ending the
 * stream (for return value != 0)
 *
 * VDP_END must at most be received once per processor, so any VDP sending it
 * downstream must itself not forward it a second time.
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
	struct vdp_ctx *vdc;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	vdc = req->vdc;
	if (vdc->retval)
		return (vdc->retval);
	vdpe = vdc->nxt;
	CHECK_OBJ_NOTNULL(vdpe, VDP_ENTRY_MAGIC);

	/* at most one VDP_END call */
	assert(vdpe->end == VDP_NULL);

	if (act == VDP_NULL)
		assert(len > 0);
	else if (act == VDP_END)
		vdpe->end = VDP_END;
	else
		assert(act == VDP_FLUSH);

	/* Call the present layer, while pointing to the next layer down */
	vdc->nxt = VTAILQ_NEXT(vdpe, list);
	vdpe->calls++;
	vdpe->bytes_in += len;
	retval = vdpe->vdp->bytes(req, act, &vdpe->priv, ptr, len);
	if (retval && (vdc->retval == 0 || retval < vdc->retval))
		vdc->retval = retval; /* Latch error value */
	vdc->nxt = vdpe;
	return (vdc->retval);
}

int
VDP_Push(struct req *req, const struct vdp *vdp, void *priv)
{
	struct vdp_entry *vdpe;
	struct vdp_ctx *vdc;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	vdc = req->vdc;
	AN(vdp);
	AN(vdp->name);
	AN(vdp->bytes);

	if (vdc->retval)
		return (vdc->retval);

	if (DO_DEBUG(DBG_PROCESSORS))
		VSLb(req->vsl, SLT_Debug, "VDP_push(%s)", vdp->name);

	vdpe = WS_Alloc(req->ws, sizeof *vdpe);
	if (vdpe == NULL) {
		AZ(vdc->retval);
		vdc->retval = -1;
		return (vdc->retval);
	}
	INIT_OBJ(vdpe, VDP_ENTRY_MAGIC);
	vdpe->vdp = vdp;
	vdpe->priv = priv;
	VTAILQ_INSERT_TAIL(&vdc->vdp, vdpe, list);
	vdc->nxt = VTAILQ_FIRST(&vdc->vdp);

	AZ(vdc->retval);
	if (vdpe->vdp->init != NULL)
		vdc->retval = vdpe->vdp->init(req, &vdpe->priv);
	if (vdc->retval > 0) {
		VTAILQ_REMOVE(&vdc->vdp, vdpe, list);
		vdc->nxt = VTAILQ_FIRST(&vdc->vdp);
		vdc->retval = 0;
	}
	return (vdc->retval);
}

uint64_t
VDP_Close(struct req *req)
{
	struct vdp_entry *vdpe;
	struct vdp_ctx *vdc;
	uint64_t rv = 0;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	vdc = req->vdc;
	while (!VTAILQ_EMPTY(&vdc->vdp)) {
		vdpe = VTAILQ_FIRST(&vdc->vdp);
		rv = vdpe->bytes_in;
		VSLb(req->vsl, SLT_VdpAcct, "%s %ju %ju", vdpe->vdp->name,
		    (uintmax_t)vdpe->calls, (uintmax_t)rv);
		if (vdc->retval >= 0)
			AN(vdpe);
		if (vdpe != NULL) {
			CHECK_OBJ(vdpe, VDP_ENTRY_MAGIC);
			if (vdpe->vdp->fini != NULL)
				AZ(vdpe->vdp->fini(req, &vdpe->priv));
			AZ(vdpe->priv);
			VTAILQ_REMOVE(&vdc->vdp, vdpe, list);
		}
		vdc->nxt = VTAILQ_FIRST(&vdc->vdp);
	}
	return (rv);
}

/*--------------------------------------------------------------------*/

static int v_matchproto_(objiterate_f)
vdp_objiterator(void *priv, unsigned flush, const void *ptr, ssize_t len)
{

	return (VDP_bytes(priv, flush ? VDP_FLUSH : VDP_NULL, ptr, len));
}


int
VDP_DeliverObj(struct req *req)
{
	int r, final;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	final = req->objcore->flags & (OC_F_PRIVATE | OC_F_HFM | OC_F_HFP)
	    ? 1 : 0;
	r = ObjIterate(req->wrk, req->objcore, req, vdp_objiterator, final);
	if (r == 0)
		r = VDP_bytes(req, VDP_END, NULL, 0);
	if (r < 0)
		return (r);
	return (0);
}
