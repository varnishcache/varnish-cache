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

static const char *
h2_panic_error(const struct h2_error_s *e)
{
	if (e == NULL)
		return ("(null)");
	else
		return (e->name);
}

void
h2_sess_panic(struct vsb *vsb, const struct sess *sp)
{
	uintptr_t *up;
	struct h2_sess *h2;
	struct h2_req *r2;

	AZ(SES_Get_proto_priv(sp, &up));

	h2 = (void*)*up;
	VSB_printf(vsb, "h2_sess = %p,\n", h2);
	PAN_CheckMagic(vsb, h2, H2_SESS_MAGIC);
	if (!VALID_OBJ(h2, H2_SESS_MAGIC))
		return;
	VSB_printf(vsb, "refcnt = %d, bogosity = %d, error = %s\n",
	    h2->refcnt, h2->bogosity, h2_panic_error(h2->error));
	VSB_printf(vsb,
	    "open_streams = %d, highest_stream = %u,"
	    " goaway_last_stream = %u,\n",
	    h2->open_streams, h2->highest_stream, h2->goaway_last_stream);
	VSB_printf(vsb,
	    "{rxf_len, rxf_type, rxf_flags, rxf_stream} ="
	    " {%u, %u, 0x%x, %u},\n",
	    h2->rxf_len, h2->rxf_type, h2->rxf_flags, h2->rxf_stream);
	VSB_printf(vsb, "streams {\n");
	VSB_indent(vsb, 2);
	VTAILQ_FOREACH(r2, &h2->streams, list) {
		PAN_CheckMagic(vsb, r2, H2_REQ_MAGIC);
		VSB_printf(vsb, "%u %p ", r2->stream, r2);
		switch (r2->state) {
#define H2_STREAM(U,sd,d) case H2_S_##U: VSB_printf(vsb, "%s", sd); break;
#include <tbl/h2_stream.h>
		default:
			VSB_printf(vsb, " 0x%x", r2->state);
			break;
		}
		VSB_printf(vsb, " {\n");
		VSB_indent(vsb, 2);

		VSB_printf(vsb, "h2_sess = %p, scheduled = %d, error = %s,\n",
		    r2->h2sess, r2->scheduled, h2_panic_error(r2->error));
		VSB_printf(vsb, "t_send = %f, t_winupd = %f,\n",
		    r2->t_send, r2->t_winupd);
		VSB_printf(vsb, "t_window = %jd, r_window = %jd,\n",
		    r2->t_window, r2->r_window);

		VSB_indent(vsb, -2);
		VSB_printf(vsb, "},\n");
	}
	VSB_indent(vsb, -2);
	VSB_printf(vsb, "},\n");
}
