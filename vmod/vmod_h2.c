/*-
 * Copyright 2023 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Author: Nils Goroll <nils.goroll@uplex.de>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "config.h"

#include "cache/cache_varnishd.h"

#include "vcc_h2_if.h"

#include "cache/cache_transport.h"
#include "http2/cache_http2.h"

static struct h2_sess *
h2get(VRT_CTX)
{
	struct h2_sess *h2;
	uintptr_t *up;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC); // $Restrict client
	if (ctx->req->transport != &HTTP2_transport)
		return (NULL);
	AZ(SES_Get_proto_priv(ctx->req->sp, &up));
	CAST_OBJ_NOTNULL(h2, (void *)*up, H2_SESS_MAGIC);
	return (h2);
}
VCL_BOOL
vmod_is(VRT_CTX)
{
	struct h2_sess *h2 = h2get(ctx);

	return (h2 != NULL);
}

#define GETSET(type, name, argname) \
type							\
vmod_ ## name(VRT_CTX, struct VARGS(name) *args)	\
{							\
	struct h2_sess *h2 = h2get(ctx);		\
	type r;						\
							\
	(void)args;					\
							\
	if (h2 == NULL)					\
		return (-1);				\
							\
	if (! args->valid_ ## argname)			\
		return (h2->name);			\
	if (h2->name == args->argname)			\
		return (h2->name);			\
							\
	Lck_Lock(&h2->sess->mtx);			\
	r = h2->name;					\
	if (h2->name != args->argname) {		\
		h2->name = args->argname;		\
		h2->rst_budget = h2->rapid_reset_limit;	\
		h2->last_rst = ctx->now;		\
	}						\
	Lck_Unlock(&h2->sess->mtx);			\
	return (r);					\
}

GETSET(VCL_DURATION, rapid_reset, threshold)
GETSET(VCL_INT, rapid_reset_limit, number)
GETSET(VCL_DURATION, rapid_reset_period, duration)

VCL_REAL
vmod_rapid_reset_budget(VRT_CTX)
{
	struct h2_sess *h2 = h2get(ctx);

	if (h2 == NULL)
		return (-1);

	return (h2->rst_budget);
}
