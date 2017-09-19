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
 * A transport is how we talk HTTP for a given request.
 *
 * This is different from a protocol because ESI child requests have
 * their own "protocol" to talk to the parent ESI request, which may
 * or may not, be talking a "real" HTTP protocol itself.
 *
 */

struct req;
struct boc;

typedef void vtr_deliver_f (struct req *, struct boc *, int sendbody);
typedef void vtr_req_body_f (struct req *);
typedef void vtr_sess_panic_f (struct vsb *, const struct sess *);
typedef void vtr_req_panic_f (struct vsb *, const struct req *);
typedef void vtr_req_fail_f (struct req *, enum sess_close);
typedef void vtr_reembark_f (struct worker *, struct req *);
typedef int vtr_minimal_response_f (struct req *, uint16_t status);

struct transport {
	unsigned			magic;
#define TRANSPORT_MAGIC			0xf157f32f

	uint16_t			number;

	const char			*proto_ident;	// for -a args
	const char			*name;

	task_func_t			*new_session;
	task_func_t			*unwait;

	vtr_req_fail_f			*req_fail;
	vtr_req_body_f			*req_body;
	vtr_deliver_f			*deliver;
	vtr_sess_panic_f		*sess_panic;
	vtr_req_panic_f			*req_panic;
	vtr_reembark_f			*reembark;
	vtr_minimal_response_f		*minimal_response;

	VTAILQ_ENTRY(transport)		list;
};

extern struct transport PROXY_transport;
extern struct transport HTTP1_transport;
extern struct transport H2_transport;
htc_complete_f H2_prism_complete;
void H2_PU_Sess(struct worker *, struct sess *, struct req *);
void H2_OU_Sess(struct worker *, struct sess *, struct req *);

const struct transport *XPORT_ByNumber(uint16_t no);
void VPX_Send_Proxy(int fd, int version, const struct sess *);

/* cache_session.c */
struct sess *SES_New(struct pool *);
void SES_Delete(struct sess *, enum sess_close reason, double now);
void SES_Close(struct sess *, enum sess_close reason);
void SES_SetTransport(struct worker *, struct sess *, struct req *,
    const struct transport *);
