/*-
 * Copyright (c) 2016 Varnish Software AS
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
 *
 */

struct h2_sess;
struct h2_req;
struct h2h_decode;
struct h2_frame_s;

#include "hpack/vhp.h"

/**********************************************************************/

struct h2_error_s {
	const char			*name;
	const char			*txt;
	uint32_t			val;
	int				stream;
	int				connection;
	stream_close_t			reason;
};

typedef const struct h2_error_s *h2_error;

#define H2EC1(U,v,r,d) extern const struct h2_error_s H2CE_##U[1];
#define H2EC2(U,v,r,d) extern const struct h2_error_s H2SE_##U[1];
#define H2EC3(U,v,r,d) H2EC1(U,v,r,d) H2EC2(U,v,r,d)
#define H2_ERROR(NAME, val, sc, reason, desc) H2EC##sc(NAME, val, reason, desc)
#include "tbl/h2_error.h"
#undef H2EC1
#undef H2EC2
#undef H2EC3

/**********************************************************************/

typedef h2_error h2_rxframe_f(struct worker *, struct h2_sess *,
    struct h2_req *);

typedef const struct h2_frame_s *h2_frame;

struct h2_frame_s {
	const char	*name;
	h2_rxframe_f	*rxfunc;
	uint8_t		type;
	uint8_t		flags;
	h2_error	act_szero;
	h2_error	act_snonzero;
	h2_error	act_sidle;
	int		respect_window;
	h2_frame	continuation;
	uint8_t		final_flags;
	int		overhead;
};

#define H2_FRAME(l,U,...) extern const struct h2_frame_s H2_F_##U[1];
#include "tbl/h2_frames.h"

/**********************************************************************/

struct h2_settings {
#define H2_SETTING(U,l,...) uint32_t l;
#include "tbl/h2_settings.h"
};

typedef void h2_setsetting_f(struct h2_settings*, uint32_t);

struct h2_setting_s {
	const char	*name;
	h2_setsetting_f	*setfunc;
	uint16_t	ident;
	uint32_t	defval;
	uint32_t	minval;
	uint32_t	maxval;
	h2_error	range_error;
};

#define H2_SETTING(U,...) extern const struct h2_setting_s H2_SET_##U[1];
#include "tbl/h2_settings.h"

/**********************************************************************/

enum h2_stream_e {
	H2_STREAM__DUMMY = -1,
#define H2_STREAM(U,s,d) H2_S_##U,
#include "tbl/h2_stream.h"
};

#define H2_FRAME_FLAGS(l,u,v)   extern const uint8_t H2FF_##u;
#include "tbl/h2_frames.h"

struct h2_rxbuf {
	unsigned			magic;
#define H2_RXBUF_MAGIC			0x73f9fb27
	unsigned			size;
	uint64_t			tail;
	uint64_t			head;
	struct stv_buffer		*stvbuf;
	uint8_t				data[];
};

struct h2_req {
	unsigned			magic;
#define H2_REQ_MAGIC			0x03411584
	uint32_t			stream;
	int				scheduled;
	enum h2_stream_e		state;
	int				counted;
	struct h2_sess			*h2sess;
	struct req			*req;
	double				t_send;
	double				t_winupd;
	pthread_cond_t			*cond;
	VTAILQ_ENTRY(h2_req)		list;
	int64_t				t_window;
	int64_t				r_window;

	/* Where to wake this stream up */
	struct worker			*wrk;

	struct h2_rxbuf			*rxbuf;

	VTAILQ_ENTRY(h2_req)		tx_list;
	h2_error			error;
};

VTAILQ_HEAD(h2_req_s, h2_req);

struct h2_sess {
	unsigned			magic;
#define H2_SESS_MAGIC			0xa16f7e4b

	pthread_t			rxthr;
	pthread_cond_t			*cond;
	pthread_cond_t			winupd_cond[1];

	struct sess			*sess;
	int				refcnt;
	unsigned			open_streams;
	uint32_t			highest_stream;
	int				goaway;
	int				bogosity;
	int				do_sweep;

	struct h2_req			*req0;

	struct h2_req_s			streams;

	struct req			*srq;
	struct ws			*ws;
	struct http_conn		*htc;
	struct vsl_log			*vsl;
	struct h2h_decode		*decode;
	struct vht_table		dectbl[1];

	unsigned			rxf_len;
	unsigned			rxf_type;
	unsigned			rxf_flags;
	unsigned			rxf_stream;
	uint8_t				*rxf_data;

	struct h2_settings		remote_settings;
	struct h2_settings		local_settings;

	struct req			*new_req;
	uint32_t			goaway_last_stream;

	VTAILQ_HEAD(,h2_req)		txqueue;

	h2_error			error;
};

#define ASSERT_RXTHR(h2) do {assert(h2->rxthr == pthread_self());} while(0)

/* http2/cache_http2_panic.c */
#ifdef TRANSPORT_MAGIC
vtr_sess_panic_f h2_sess_panic;
#endif

/* http2/cache_http2_deliver.c */
#ifdef TRANSPORT_MAGIC
vtr_deliver_f h2_deliver;
vtr_minimal_response_f h2_minimal_response;
#endif /* TRANSPORT_MAGIC */

/* http2/cache_http2_hpack.c */
struct h2h_decode {
	unsigned			magic;
#define H2H_DECODE_MAGIC		0xd092bde4

	h2_error			error;
	enum vhd_ret_e			vhd_ret;
	char				*out;
	char				*reset;
	size_t				out_l;
	size_t				out_u;
	size_t				namelen;
	struct vhd_decode		vhd[1];
};

void h2h_decode_init(const struct h2_sess *h2);
h2_error h2h_decode_fini(const struct h2_sess *h2);
h2_error h2h_decode_bytes(struct h2_sess *h2, const uint8_t *ptr,
    size_t len);

/* cache_http2_send.c */
void H2_Send_Get(struct worker *, struct h2_sess *, struct h2_req *);
void H2_Send_Rel(struct h2_sess *, const struct h2_req *);

void H2_Send_Frame(struct worker *, struct h2_sess *,
    h2_frame type, uint8_t flags, uint32_t len, uint32_t stream,
    const void *);

void H2_Send_RST(struct worker *wrk, struct h2_sess *h2,
    const struct h2_req *r2, uint32_t stream, h2_error h2e);

void H2_Send(struct worker *, struct h2_req *, h2_frame type, uint8_t flags,
    uint32_t len, const void *, uint64_t *acct);

/* cache_http2_proto.c */
struct h2_req * h2_new_req(struct h2_sess *, unsigned stream, struct req *);
int h2_stream_tmo(struct h2_sess *, const struct h2_req *, vtim_real);
void h2_del_req(struct worker *, struct h2_req *);
void h2_kill_req(struct worker *, struct h2_sess *, struct h2_req *, h2_error);
int h2_rxframe(struct worker *, struct h2_sess *);
h2_error h2_set_setting(struct h2_sess *, const uint8_t *);
void h2_req_body(struct req*);
task_func_t h2_do_req;
#ifdef TRANSPORT_MAGIC
vtr_req_fail_f h2_req_fail;
#endif
