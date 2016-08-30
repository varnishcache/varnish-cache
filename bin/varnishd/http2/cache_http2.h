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

struct h2_sess;

#include "hpack/vhp.h"

enum h2_error_e {
#define H2_ERROR(NAME, val, desc)		\
	H2E_##NAME = val,
#include "tbl/h2_error.h"
};

enum h2_frame_e {
#define H2_FRAME(l,u,t,f) H2_FRAME_##u = t,
#include "tbl/h2_frames.h"
};

enum h2_stream_e {
#define H2_STREAM(U,s,d) H2_S_##U,
#include "tbl/h2_stream.h"
};

#define H2_FRAME_FLAGS(l,u,v)   extern const uint8_t H2FF_##u;
#include "tbl/h2_frames.h"

#define H2_SETTINGS_N			7

struct h2_req {
	unsigned			magic;
#define H2_REQ_MAGIC			0x03411584
	uint32_t			stream;
	enum h2_stream_e		state;
	struct h2_sess			*h2sess;
	struct req			*req;
	VTAILQ_ENTRY(h2_req)		list;
	int64_t				window;
};

VTAILQ_HEAD(h2_req_s, h2_req);

struct h2_sess {
	unsigned			magic;
#define H2_SESS_MAGIC			0xa16f7e4b

	struct sess			*sess;
	int				refcnt;
	uint32_t			highest_stream;

	struct h2_req_s			streams;

	struct req			*srq;
	struct ws			*ws;
	struct http_conn		*htc;
	struct vsl_log			*vsl;
	struct vht_table		dectbl[1];

	unsigned			rxf_len;
	unsigned			rxf_flags;
	unsigned			rxf_stream;
	uint8_t				*rxf_data;

	uint32_t			their_settings[H2_SETTINGS_N];
	uint32_t			our_settings[H2_SETTINGS_N];

	struct req			*new_req;
	int				go_away;
	uint32_t			go_away_last_stream;
};

/* http2/cache_http2_panic.c */
#ifdef TRANSPORT_MAGIC
vtr_sess_panic_f h2_sess_panic;
#endif

/* http2/cache_http2_deliver.c */
#ifdef TRANSPORT_MAGIC
vtr_deliver_f h2_deliver;
#endif /* TRANSPORT_MAGIC */

/* http2/cache_http2_hpack.c */
struct h2h_decode {
	unsigned			magic;
#define H2H_DECODE_MAGIC		0xd092bde4

	int				error;
	enum vhd_ret_e			vhd_ret;
	char				*out;
	char				*reset;
	size_t				out_l;
	size_t				out_u;
	size_t				namelen;
	struct vhd_decode		vhd[1];
};

void h2h_decode_init(const struct h2_sess *h2, struct h2h_decode *d);
int h2h_decode_fini(const struct h2_sess *h2, struct h2h_decode *d);
int h2h_decode_bytes(struct h2_sess *h2, struct h2h_decode *d,
    const uint8_t *ptr, size_t len);

int H2_Send_Frame(struct worker *, const struct h2_sess *,
    enum h2_frame_e type, uint8_t flags, uint32_t len, uint32_t stream,
    const void *);

int H2_Send(struct worker *, struct h2_req *, int flush,
    enum h2_frame_e type, uint8_t flags, uint32_t len, const void *);

typedef void h2_frame_f(struct worker *, struct h2_sess *,
    struct h2_req *);
#define H2_FRAME(l,u,t,f) h2_frame_f h2_rx_##l ;
#include "tbl/h2_frames.h"

