/*-
 * Copyright (c) 2016 Varnish Software AS
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
 */

#include "config.h"

#include "cache/cache_varnishd.h"

#include "cache/cache_transport.h"
#include "http2/cache_http2.h"

void
h2_sess_panic(struct vsb *vsb, const struct sess *sp)
{
	uintptr_t *up;
	struct h2_sess *h2;
	struct h2_req *r2;

	AZ(SES_Get_proto_priv(sp, &up));

	h2 = (void*)*up;
	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	VSB_printf(vsb, "streams {\n");
	VSB_indent(vsb, 2);
	VTAILQ_FOREACH(r2, &h2->streams, list) {
		PAN_CheckMagic(vsb, r2, H2_REQ_MAGIC);
		VSB_printf(vsb, "0x%08x", r2->stream);
		switch (r2->state) {
#define H2_STREAM(U,sd,d) case H2_S_##U: VSB_printf(vsb, " %-6s", sd); break;
#include <tbl/h2_stream.h>
		default:
			VSB_printf(vsb, " State %d", r2->state);
			break;
		}
		VSB_printf(vsb, "\n");
	}
	VSB_indent(vsb, -2);
	VSB_printf(vsb, "}\n");
}
