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
//#include "cache_filter.h"

int
VDP_bytes(struct req *req, enum vdp_action act, const void *ptr, ssize_t len)
{
	int i, retval;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	assert(act > VDP_NULL || len > 0);
	/* Call the present layer, while pointing to the next layer down */
	i = req->vdp_nxt--;
	assert(i >= 0 && i < N_VDPS);
	retval = req->vdps[i](req, act, &req->vdpp[i], ptr, len);
	req->vdp_nxt++;
	return (retval);
}

void
VDP_push(struct req *req, vdp_bytes *func, void *priv)
{
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AN(func);

	/* Push another layer */
	assert(req->vdp_nxt >= 0);
	assert(req->vdp_nxt + 1 < N_VDPS);
	req->vdps[++req->vdp_nxt] = func;
	req->vdpp[req->vdp_nxt] = priv;
	AZ(req->vdps[req->vdp_nxt](req, VDP_INIT,
	   &req->vdpp[req->vdp_nxt], NULL, 0));
}

void
VDP_pop(struct req *req, vdp_bytes *func)
{
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	/* Pop top layer */
	assert(req->vdp_nxt >= 1);
	assert(req->vdp_nxt < N_VDPS);
	assert(req->vdps[req->vdp_nxt] == func);
	AZ(req->vdps[req->vdp_nxt](req, VDP_FINI,
	   &req->vdpp[req->vdp_nxt], NULL, 0));
	AZ(req->vdpp[req->vdp_nxt]);
	req->vdps[req->vdp_nxt] = NULL;
	req->vdp_nxt--;
}
