/*-
 * Copyright (c) 2016 Varnish Software
 * All rights reserved.
 *
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
 * Author: Dridi Boukelmoune <dridi.boukelmoune@gmail.com>
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

#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>

#include "vdef.h"
#include "vas.h"
#include "miniobj.h"

#include "hpack/vhp.h"

#include "vhp_hufdec.h"

struct vhd_ctx {
	struct vhd_decode *d;
	struct vht_table *tbl;
	const uint8_t *in;
	const uint8_t *in_e;
	char *out;
	char *out_e;
};

typedef enum vhd_ret_e vhd_state_f(struct vhd_ctx *ctx, unsigned first);

/* Function flags */
#define VHD_INCREMENTAL	(1U << 0)

/* Functions */
enum vhd_func_e {
#define VHD_FSM_FUNC(NAME, func)		\
	VHD_F_##NAME,
#include "tbl/vhd_fsm_funcs.h"
	VHD_F__MAX,
};
#define VHD_FSM_FUNC(NAME, func)		\
	static vhd_state_f func;
#include "tbl/vhd_fsm_funcs.h"

/* States */
enum vhd_state_e {
	VHD_S__MIN = -1,
#define VHD_FSM(STATE, FUNC, arg1, arg2)	\
	VHD_S_##STATE,
#include "tbl/vhd_fsm.h"
	VHD_S__MAX,
};
static const struct vhd_state {
	const char		*name;
	enum vhd_func_e		func;
	unsigned		arg1;
	unsigned		arg2;
} vhd_states[VHD_S__MAX] = {
#define VHD_FSM(STATE, FUNC, arg1, arg2)	\
	[VHD_S_##STATE] = { #STATE, VHD_F_##FUNC, arg1, arg2 },
#include "tbl/vhd_fsm.h"
};

/* Utility functions */
static void
vhd_set_state(struct vhd_decode *d, enum vhd_state_e state)
{
	AN(d);
	assert(state > VHD_S__MIN && state < VHD_S__MAX);
	d->state = state;
	d->first = 1;
}

static void
vhd_next_state(struct vhd_decode *d)
{
	AN(d);
	assert(d->state + 1 < VHD_S__MAX);
	vhd_set_state(d, d->state + 1);
}

/* State functions */
static enum vhd_ret_e __match_proto__(vhd_state_f)
vhd_skip(struct vhd_ctx *ctx, unsigned first)
{
	AN(ctx);
	AN(first);
	vhd_next_state(ctx->d);
	return (VHD_AGAIN);
}

static enum vhd_ret_e __match_proto__(vhd_state_f)
vhd_goto(struct vhd_ctx *ctx, unsigned first)
{
	const struct vhd_state *s;

	AN(ctx);
	AN(first);
	assert(ctx->d->state < VHD_S__MAX);
	s = &vhd_states[ctx->d->state];
	assert(s->arg1 < VHD_S__MAX);
	vhd_set_state(ctx->d, s->arg1);
	return (VHD_AGAIN);
}

static enum vhd_ret_e __match_proto__(vhd_state_f)
vhd_idle(struct vhd_ctx *ctx, unsigned first)
{
	uint8_t c;

	AN(ctx);
	(void)first;

	while (ctx->in < ctx->in_e) {
		c = *ctx->in;
		if ((c & 0x80) == 0x80)
			vhd_set_state(ctx->d, VHD_S_HP61_START);
		else if ((c & 0xc0) == 0x40)
			vhd_set_state(ctx->d, VHD_S_HP621_START);
		else if ((c & 0xf0) == 0x00)
			vhd_set_state(ctx->d, VHD_S_HP622_START);
		else if ((c & 0xf0) == 0x10)
			vhd_set_state(ctx->d, VHD_S_HP623_START);
		else if ((c & 0xe0) == 0x20)
			vhd_set_state(ctx->d, VHD_S_HP63_START);
		else
			return (VHD_ERR_ARG);
		return (VHD_AGAIN);
	}

	return (VHD_OK);
}

static enum vhd_ret_e __match_proto__(vhd_state_f)
vhd_integer(struct vhd_ctx *ctx, unsigned first)
{
	const struct vhd_state *s;
	struct vhd_int *i;
	uint8_t c;
	unsigned mask;

	assert(UINT_MAX >= UINT32_MAX);

	AN(ctx);
	assert(ctx->d->state < VHD_S__MAX);
	s = &vhd_states[ctx->d->state];
	i = ctx->d->integer;

	if (first) {
		INIT_OBJ(i, VHD_INT_MAGIC);
		i->pfx = s->arg1;
		assert(i->pfx >= 4 && i->pfx <= 7);
	}
	CHECK_OBJ_NOTNULL(i, VHD_INT_MAGIC);

	while (ctx->in < ctx->in_e) {
		c = *ctx->in;
		ctx->in++;
		if (i->pfx) {
			mask = (1U << i->pfx) - 1;
			i->pfx = 0;
			i->v = c & mask;
			if (i->v < mask) {
				vhd_next_state(ctx->d);
				return (VHD_AGAIN);
			}
		} else {
			if ((i->m == 28 && (c & 0x78)) || i->m > 28)
				return (VHD_ERR_INT);
			i->v += (c & 0x7f) * ((uint32_t)1 << i->m);
			i->m += 7;
			if (!(c & 0x80)) {
				vhd_next_state(ctx->d);
				return (VHD_AGAIN);
			}
		}
	}
	return (VHD_MORE);
}

static enum vhd_ret_e __match_proto__(vhd_state_f)
vhd_set_max(struct vhd_ctx *ctx, unsigned first)
{
	AN(ctx);
	AN(first);
	CHECK_OBJ_NOTNULL(ctx->d->integer, VHD_INT_MAGIC);
	if (ctx->tbl == NULL)
		return (VHD_ERR_UPD);
	if (VHT_SetMaxTableSize(ctx->tbl, ctx->d->integer->v))
		return (VHD_ERR_UPD);
	vhd_next_state(ctx->d);
	return (VHD_AGAIN);
}

static enum vhd_ret_e __match_proto__(vhd_state_f)
vhd_set_idx(struct vhd_ctx *ctx, unsigned first)
{
	AN(ctx);
	AN(first);
	CHECK_OBJ_NOTNULL(ctx->d->integer, VHD_INT_MAGIC);
	ctx->d->index = ctx->d->integer->v;
	vhd_next_state(ctx->d);
	return (VHD_AGAIN);
}

static enum vhd_ret_e __match_proto__(vhd_state_f)
vhd_lookup(struct vhd_ctx *ctx, unsigned first)
{
	const struct vhd_state *s;
	struct vhd_lookup *lu;
	const char *p;
	size_t l;

	AN(ctx);
	assert(ctx->d->state < VHD_S__MAX);
	s = &vhd_states[ctx->d->state];
	lu = ctx->d->lookup;

	if (first)
		INIT_OBJ(lu, VHD_LOOKUP_MAGIC);
	CHECK_OBJ_NOTNULL(lu, VHD_LOOKUP_MAGIC);

	switch (s->arg1) {
	case VHD_NAME:
	case VHD_NAME_SEC:
		p = VHT_LookupName(ctx->tbl, ctx->d->index, &l);
		break;
	case VHD_VALUE:
	case VHD_VALUE_SEC:
		p = VHT_LookupValue(ctx->tbl, ctx->d->index, &l);
		break;
	default:
		WRONG("vhd_lookup wrong arg1");
		break;
	}
	if (first && p == NULL)
		return (VHD_ERR_IDX);
	AN(p);
	assert(l <= UINT_MAX);
	if (first)
		lu->l = l;

	assert(lu->l <= l);
	p += l - lu->l;
	l = lu->l;
	if (l > ctx->out_e - ctx->out)
		l = ctx->out_e - ctx->out;
	memcpy(ctx->out, p, l);
	ctx->out += l;
	lu->l -= l;

	if (lu->l == 0) {
		vhd_next_state(ctx->d);
		return (s->arg1);
	}
	assert(ctx->out == ctx->out_e);
	return (VHD_BUF);
}

static enum vhd_ret_e __match_proto__(vhd_state_f)
vhd_new(struct vhd_ctx *ctx, unsigned first)
{
	AN(ctx);
	AN(first);
	if (ctx->tbl != NULL)
		VHT_NewEntry(ctx->tbl);
	vhd_next_state(ctx->d);
	return (VHD_AGAIN);
}

static enum vhd_ret_e __match_proto__(vhd_state_f)
vhd_new_idx(struct vhd_ctx *ctx, unsigned first)
{
	AN(ctx);
	AN(first);
	if (ctx->tbl != NULL) {
		if (VHT_NewEntry_Indexed(ctx->tbl, ctx->d->index))
			return (VHD_ERR_IDX);
	}
	vhd_next_state(ctx->d);
	return (VHD_AGAIN);
}

static enum vhd_ret_e __match_proto__(vhd_state_f)
vhd_branch_zidx(struct vhd_ctx *ctx, unsigned first)
{
	const struct vhd_state *s;

	AN(ctx);
	(void)first;
	assert(ctx->d->state < VHD_S__MAX);
	s = &vhd_states[ctx->d->state];
	assert(s->arg1 < VHD_S__MAX);

	if (ctx->d->index == 0)
		vhd_set_state(ctx->d, s->arg1);
	else
		vhd_next_state(ctx->d);
	return (VHD_AGAIN);
}

static enum vhd_ret_e __match_proto__(vhd_state_f)
vhd_branch_bit0(struct vhd_ctx *ctx, unsigned first)
{
	const struct vhd_state *s;

	AN(ctx);
	(void)first;
	assert(ctx->d->state < VHD_S__MAX);
	s = &vhd_states[ctx->d->state];
	assert(s->arg1 < VHD_S__MAX);

	if (ctx->in == ctx->in_e)
		return (VHD_MORE);

	if (*ctx->in & 0x80)
		vhd_set_state(ctx->d, s->arg1);
	else
		vhd_next_state(ctx->d);
	return (VHD_AGAIN);
}

static enum vhd_ret_e __match_proto__(vhd_state_f)
vhd_raw(struct vhd_ctx *ctx, unsigned first)
{
	const struct vhd_state *s;
	struct vhd_raw *raw;
	size_t l2;

	AN(ctx);
	assert(ctx->d->state < VHD_S__MAX);
	s = &vhd_states[ctx->d->state];

	raw = ctx->d->raw;
	if (first) {
		CHECK_OBJ_NOTNULL(ctx->d->integer, VHD_INT_MAGIC);
		l2 = ctx->d->integer->v;
		INIT_OBJ(raw, VHD_RAW_MAGIC);
		raw->l = l2;
	}
	CHECK_OBJ_NOTNULL(raw, VHD_RAW_MAGIC);

	while (raw->l > 0) {
		l2 = raw->l;
		if (l2 > (ctx->in_e - ctx->in))
			l2 = ctx->in_e - ctx->in;
		if (l2 == 0)
			return (VHD_MORE);
		if (l2 > (ctx->out_e - ctx->out))
			l2 = ctx->out_e - ctx->out;
		if (l2 == 0)
			return (VHD_BUF);
		memcpy(ctx->out, ctx->in, l2);
		ctx->in += l2;
		if (ctx->tbl != NULL && (s->arg2 & VHD_INCREMENTAL)) {
			switch (s->arg1) {
			case VHD_NAME:
				VHT_AppendName(ctx->tbl, ctx->out, l2);
				break;
			case VHD_VALUE:
				VHT_AppendValue(ctx->tbl, ctx->out, l2);
				break;
			default:
				WRONG("vhd_raw wrong arg1");
				break;
			}
		}
		ctx->out += l2;
		raw->l -= l2;
	}
	vhd_next_state(ctx->d);
	return (s->arg1);
}

static enum vhd_ret_e __match_proto__(vhd_state_f)
vhd_huffman(struct vhd_ctx *ctx, unsigned first)
{
	const struct vhd_state *s;
	struct vhd_huffman *huf;
	enum vhd_ret_e r;
	unsigned u, l;

	AN(ctx);
	assert(ctx->d->state < VHD_S__MAX);
	s = &vhd_states[ctx->d->state];

	huf = ctx->d->huffman;
	if (first) {
		CHECK_OBJ_NOTNULL(ctx->d->integer, VHD_INT_MAGIC);
		l = ctx->d->integer->v;
		INIT_OBJ(huf, VHD_HUFFMAN_MAGIC);
		huf->len = l;
	}
	CHECK_OBJ_NOTNULL(huf, VHD_HUFFMAN_MAGIC);

	r = VHD_OK;
	l = 0;
	while (1) {
		assert(huf->pos < HUFDEC_LEN);
		assert(hufdec[huf->pos].mask > 0);
		assert(hufdec[huf->pos].mask <= 8);

		if (huf->len > 0 && huf->blen < hufdec[huf->pos].mask) {
			/* Refill from input */
			if (ctx->in == ctx->in_e) {
				r = VHD_MORE;
				break;
			}
			huf->bits = (huf->bits << 8) | *ctx->in;
			huf->blen += 8;
			huf->len--;
			ctx->in++;
		}

		if (huf->len == 0 && huf->pos == 0 && huf->blen <= 7 &&
		    huf->bits == (1U << huf->blen) - 1U) {
			/* End of stream */
			r = s->arg1;
			vhd_next_state(ctx->d);
			break;
		}

		if (ctx->out + l == ctx->out_e) {
			r = VHD_BUF;
			break;
		}

		if (huf->blen >= hufdec[huf->pos].mask)
			u = huf->bits >> (huf->blen - hufdec[huf->pos].mask);
		else
			u = huf->bits << (hufdec[huf->pos].mask - huf->blen);
		huf->pos += u;
		assert(huf->pos < HUFDEC_LEN);

		if (hufdec[huf->pos].len == 0 ||
		    hufdec[huf->pos].len > huf->blen) {
			/* Invalid or incomplete code */
			r = VHD_ERR_HUF;
			break;
		}

		huf->blen -= hufdec[huf->pos].len;
		huf->bits &= (1U << huf->blen) - 1U;

		if (hufdec[huf->pos].jump) {
			huf->pos += hufdec[huf->pos].jump;
			assert(huf->pos < HUFDEC_LEN);
		} else {
			ctx->out[l++] = hufdec[huf->pos].chr;
			huf->pos = 0;
		}
	}

	if (l > 0 && ctx->tbl != NULL && (s->arg2 & VHD_INCREMENTAL)) {
		switch (s->arg1) {
		case VHD_NAME:
			VHT_AppendName(ctx->tbl, ctx->out, l);
			break;
		case VHD_VALUE:
			VHT_AppendValue(ctx->tbl, ctx->out, l);
			break;
		default:
			WRONG("vhd_raw wrong arg1");
			break;
		}
	}
	ctx->out += l;

	assert(r != VHD_OK);
	return (r);
}

/* Public interface */

const char *
VHD_Error(enum vhd_ret_e r)
{
	switch (r) {
#define VHD_RET(NAME, VAL, DESC)			\
	case VHD_##NAME:				\
		return ("VHD_" #NAME " (" DESC ")");
#include "tbl/vhd_return.h"
	default:
		return ("VHD_UNKNOWN");
	}
}

enum vhd_ret_e
VHD_Decode(struct vhd_decode *d, struct vht_table *tbl,
    const uint8_t *in, size_t inlen, size_t *p_inused,
    char *out, size_t outlen, size_t *p_outused)
{
	const struct vhd_state *s;
	struct vhd_ctx ctx[1];
	enum vhd_ret_e ret;
	unsigned first;

	CHECK_OBJ_NOTNULL(d, VHD_DECODE_MAGIC);
	CHECK_OBJ_ORNULL(tbl, VHT_TABLE_MAGIC);
	AN(in);
	AN(p_inused);
	AN(out);
	AN(p_outused);

	if (d->error < 0)
		return (d->error);

	assert(*p_inused <= inlen);
	assert(*p_outused <= outlen);

	ctx->d = d;
	ctx->tbl = tbl;
	ctx->in = in + *p_inused;
	ctx->in_e = in + inlen;
	ctx->out = out + *p_outused;
	ctx->out_e = out + outlen;

	do {
		first = d->first;
		d->first = 0;
		assert(d->state < VHD_S__MAX);
		s = &vhd_states[d->state];
		switch (s->func) {
#define VHD_FSM_FUNC(NAME, func)		\
		case VHD_F_##NAME:		\
			ret = func(ctx, first);	\
			break;
#include "tbl/vhd_fsm_funcs.h"
		default:
			WRONG("Undefined vhd function");
			break;
		}
	} while (ret == VHD_AGAIN);

	if (ret < 0)
		d->error = ret;

	assert(in + *p_inused <= ctx->in);
	*p_inused += ctx->in - (in + *p_inused);
	assert(out + *p_outused <= ctx->out);
	*p_outused += ctx->out - (out + *p_outused);

	return (ret);
}

void
VHD_Init(struct vhd_decode *d)
{

	AN(d);
	assert(VHD_S__MAX <= UINT16_MAX);
	assert(HUFDEC_LEN <= UINT16_MAX);
	INIT_OBJ(d, VHD_DECODE_MAGIC);
	d->state = VHD_S_IDLE;
	d->first = 1;
}

/* Test driver */

#ifdef DECODE_TEST_DRIVER

#include <ctype.h>
#include <stdarg.h>

static int verbose = 0;

static size_t
hexbuf(uint8_t *buf, size_t buflen, const char *h)
{
	size_t l;
	uint8_t u;

	AN(h);
	AN(buf);

	l = 0;
	for (; *h != '\0'; h++) {
		if (l == buflen * 2)
			WRONG("Too small buffer");
		if (isspace(*h))
			continue;
		if (*h >= '0' && *h <= '9')
			u = *h - '0';
		else if (*h >= 'a' && *h <= 'f')
			u = 0xa + *h - 'a';
		else if (*h >= 'A' && *h <= 'F')
			u = 0xa + *h - 'A';
		else
			WRONG("Bad input character");
		assert(u <= 0xf);
		if (l % 2 == 0) {
			u <<= 4;
			buf[l / 2] = u;
		} else {
			buf[l / 2] |= u;
		}
		l++;
	}
	AZ(l % 2);
	return (l / 2);
}

static int
match(const char *b, size_t l, ...)
{
	va_list ap;
	const char *e;
	const char *m;
	int r = 0;

	va_start(ap, l);
	e = b + l;
	while (1) {
		m = va_arg(ap, const char *);
		if (m == NULL)
			break;
		l = strlen(m);
		if (e - b <= l || b[l] != '\0' || strncmp(b, m, l)) {
			printf("%.*s != %s\n", (int)(e - b), b, m);
			r = -1;
			break;
		} else if (verbose) {
			printf("%s == %s\n", b, m);
		}
		b += l + 1;
	}
	va_end(ap);
	return (r);
}

#define M_1IN (1U << 0)
#define M_1OUT (1U << 1)

static enum vhd_ret_e
decode(struct vhd_decode *d, struct vht_table *tbl, uint8_t *in, size_t in_l,
    char *out, size_t out_l, unsigned m)
{
	size_t in_u, out_u;
	enum vhd_ret_e r;

	CHECK_OBJ_NOTNULL(d, VHD_DECODE_MAGIC);
	AN(in);
	AN(out);

	in_u = 0;
	out_u = 0;

	while (1) {
		r = VHD_Decode(d, tbl, in,
		    (m & M_1IN ? (in_l > in_u ? in_u + 1 : in_u) : in_l),
		    &in_u,
		    out,
		    (m & M_1OUT ? (out_l > out_u ? out_u + 1 : out_u) : out_l),
		    &out_u);
		assert(in_u <= in_l);
		assert(out_u <= out_l);
		if (r < VHD_OK)
			return (r);

		switch (r) {
		case VHD_OK:
			return (r);

		case VHD_MORE:
			if (in_u == in_l)
				return (r);
			break;

		case VHD_BUF:
			if (out_u == out_l)
				return (r);
			break;

		case VHD_NAME:
		case VHD_NAME_SEC:
			assert(out_l - out_u > 0);
			out[out_u++] = '\0';
			if (verbose)
				printf("Name%s: '%s'\n",
				    (r == VHD_NAME_SEC ? " (sec)" : ""),
				    out);
			out += out_u;
			out_l -= out_u;
			out_u = 0;
			break;

		case VHD_VALUE:
		case VHD_VALUE_SEC:
			assert(out_l - out_u > 0);
			out[out_u++] = '\0';
			if (verbose)
				printf("Value%s: '%s'\n",
				    (r == VHD_VALUE_SEC ? " (sec)" : ""),
				    out);
			out += out_u;
			out_l -= out_u;
			out_u = 0;
			break;

		default:
			WRONG("Wrong return code");
			break;
		}
	}

	NEEDLESS(return (VHD_OK));
}

#define CHECK_RET(r, e)					\
	do {						\
		if (verbose || r != e) {		\
			printf("%s %s %s\n",		\
			    VHD_Error(r),		\
			    (r == e ? "==" : "!="),	\
			    VHD_Error(e));		\
		}					\
		assert(r == e);				\
	} while (0)

#define CHECK_INT(d, u)							\
	do {								\
		CHECK_OBJ_NOTNULL(d->integer, VHD_INT_MAGIC);		\
		if (verbose || d->integer->v != u) {			\
			printf("%u %s %u\n", d->integer->v,		\
			    (d->integer->v == u ? "==" : "!="),		\
			    u);						\
		}							\
		assert(d->integer->v == u);				\
	} while (0)

static void
test_integer(unsigned mode)
{
	struct vhd_decode d[1];
	uint8_t in[128];
	size_t in_l;
	char out[128];
	enum vhd_ret_e r;

	/* Test single byte decoding */
	VHD_Init(d);
	vhd_set_state(d, VHD_S_TEST_INT5);
	in_l = hexbuf(in, sizeof in, "1e");
	r = decode(d, NULL, in, in_l, out, sizeof out, mode);
	CHECK_RET(r, VHD_OK);
	CHECK_INT(d, 30);

	/* Test multibyte decoding */
	VHD_Init(d);
	vhd_set_state(d, VHD_S_TEST_INT5);
	in_l = hexbuf(in, sizeof in, "ff 9a 0a");
	r = decode(d, NULL, in, in_l, out, sizeof out, mode);
	CHECK_RET(r, VHD_OK);
	CHECK_INT(d, 1337);

	/* Test max size we allow */
	VHD_Init(d);
	vhd_set_state(d, VHD_S_TEST_INT5);
	in_l = hexbuf(in, sizeof in, "1f ff ff ff ff 07");
	r = decode(d, NULL, in, in_l, out, sizeof out, mode);
	CHECK_RET(r, VHD_OK);
	CHECK_INT(d, 0x8000001E);

	/* Test overflow */
	VHD_Init(d);
	vhd_set_state(d, VHD_S_TEST_INT5);
	in_l = hexbuf(in, sizeof in, "1f ff ff ff ff 08");
	r = decode(d, NULL, in, in_l, out, sizeof out, mode);
	CHECK_RET(r, VHD_ERR_INT);
}

static void
test_raw(unsigned mode)
{
	struct vhd_decode d[1];
	uint8_t in[128];
	size_t in_l;
	char out[128];
	enum vhd_ret_e r;

	/* Test raw encoding */
	VHD_Init(d);
	vhd_set_state(d, VHD_S_TEST_LITERAL);
	in_l = hexbuf(in, sizeof in,
	    "0a63 7573 746f 6d2d 6b65 790d 6375 7374 6f6d 2d68 6561 6465 72");
	r = decode(d, NULL, in, in_l, out, sizeof out, mode);
	CHECK_RET(r, VHD_OK);
	AZ(match(out, sizeof out, "custom-key", "custom-header", NULL));

	/* Test too short input */
	VHD_Init(d);
	vhd_set_state(d, VHD_S_TEST_LITERAL);
	in_l = hexbuf(in, sizeof in,
	    "02");
	r = decode(d, NULL, in, in_l, out, sizeof out, mode);
	CHECK_RET(r, VHD_MORE);
}

static void
test_huffman(unsigned mode)
{
	struct vhd_decode d[1];
	uint8_t in[256];
	size_t in_l;
	char out[256];
	enum vhd_ret_e r;

	/* Decode a huffman encoded value */
	VHD_Init(d);
	in_l = hexbuf(in, sizeof in,
	    "0141 8cf1 e3c2 e5f2 3a6b a0ab 90f4 ff");
	vhd_set_state(d, VHD_S_TEST_LITERAL);
	r = decode(d, NULL, in, in_l, out, sizeof out, mode);
	CHECK_RET(r, VHD_OK);
	AZ(match(out, sizeof out, "A", "www.example.com", NULL));

	/* Decode an incomplete input buffer */
	VHD_Init(d);
	in_l = hexbuf(in, sizeof in,
	    "0141 81");
	vhd_set_state(d, VHD_S_TEST_LITERAL);
	r = decode(d, NULL, in, in_l, out, sizeof out, mode);
	CHECK_RET(r, VHD_MORE);

	/* Decode an incomplete huffman code */
	VHD_Init(d);
	in_l = hexbuf(in, sizeof in,
	    "0141 81 fe");
	vhd_set_state(d, VHD_S_TEST_LITERAL);
	r = decode(d, NULL, in, in_l, out, sizeof out, mode);
	CHECK_RET(r, VHD_ERR_HUF);

	/* Decode an invalid huffman code */
	VHD_Init(d);
	in_l = hexbuf(in, sizeof in,
	    "0141 84 ff ff ff ff");
	vhd_set_state(d, VHD_S_TEST_LITERAL);
	r = decode(d, NULL, in, in_l, out, sizeof out, mode);
	CHECK_RET(r, VHD_ERR_HUF);
}

static void
test_c2(unsigned mode)
{
	struct vhd_decode d[1];
	uint8_t in[256];
	size_t in_l;
	char out[256];
	enum vhd_ret_e r;

	/* See RFC 7541 Appendix C.2 */

	VHD_Init(d);

	/* C.2.1 */
	in_l = hexbuf(in, sizeof in,
	    "400a 6375 7374 6f6d 2d6b 6579 0d63 7573"
	    "746f 6d2d 6865 6164 6572");
	r = decode(d, NULL, in, in_l, out, sizeof out, mode);
	CHECK_RET(r, VHD_OK);
	AZ(match(out, sizeof out,
		"custom-key", "custom-header",
		NULL));

	/* C.2.2 */
	in_l = hexbuf(in, sizeof in,
	    "040c 2f73 616d 706c 652f 7061 7468");
	r = decode(d, NULL, in, in_l, out, sizeof out, mode);
	CHECK_RET(r, VHD_OK);
	AZ(match(out, sizeof out,
		":path", "/sample/path",
		NULL));

	/* C.2.3 */
	in_l = hexbuf(in, sizeof in,
	    "1008 7061 7373 776f 7264 0673 6563 7265"
	    "74");
	r = decode(d, NULL, in, in_l, out, sizeof out, mode);
	CHECK_RET(r, VHD_OK);
	AZ(match(out, sizeof out,
		"password", "secret",
		NULL));

	/* C.2.4 */
	in_l = hexbuf(in, sizeof in,
	    "82");
	r = decode(d, NULL, in, in_l, out, sizeof out, mode);
	CHECK_RET(r, VHD_OK);
	AZ(match(out, sizeof out,
		":method", "GET",
		NULL));
}

static void
test_c3(unsigned mode)
{
	struct vht_table t[1];
	struct vhd_decode d[1];
	uint8_t in[256];
	size_t in_l;
	char out[256];
	enum vhd_ret_e r;

	/* See RFC 7541 Appendix C.3 */

	AZ(VHT_Init(t, 4096));
	VHD_Init(d);

	/* C.3.1 */
	in_l = hexbuf(in, sizeof in,
	    "8286 8441 0f77 7777 2e65 7861 6d70 6c65"
	    "2e63 6f6d");
	r = decode(d, t, in, in_l, out, sizeof out, mode);
	CHECK_RET(r, VHD_OK);
	AZ(match(out, sizeof out,
		":method", "GET",
		":scheme", "http",
		":path", "/",
		":authority", "www.example.com",
		NULL));

	/* C.3.2 */
	in_l = hexbuf(in, sizeof in,
	    "8286 84be 5808 6e6f 2d63 6163 6865");
	r = decode(d, t, in, in_l, out, sizeof out, mode);
	CHECK_RET(r, VHD_OK);
	AZ(match(out, sizeof out,
		":method", "GET",
		":scheme", "http",
		":path", "/",
		":authority", "www.example.com",
		"cache-control", "no-cache",
		NULL));

	/* C.3.3 */
	in_l = hexbuf(in, sizeof in,
	    "8287 85bf 400a 6375 7374 6f6d 2d6b 6579"
	    "0c63 7573 746f 6d2d 7661 6c75 65");
	r = decode(d, t, in, in_l, out, sizeof out, mode);
	CHECK_RET(r, VHD_OK);
	AZ(match(out, sizeof out,
		":method", "GET",
		":scheme", "https",
		":path", "/index.html",
		":authority", "www.example.com",
		"custom-key", "custom-value",
		NULL));

	VHT_Fini(t);
}

static void
test_c4(unsigned mode)
{
	struct vht_table t[1];
	struct vhd_decode d[1];
	uint8_t in[256];
	size_t in_l;
	char out[256];
	enum vhd_ret_e r;

	/* See RFC 7541 Appendix C.4 */

	AZ(VHT_Init(t, 4096));
	VHD_Init(d);

	/* C.4.1 */
	in_l = hexbuf(in, sizeof in,
	    "8286 8441 8cf1 e3c2 e5f2 3a6b a0ab 90f4 ff");
	r = decode(d, t, in, in_l, out, sizeof out, mode);
	CHECK_RET(r, VHD_OK);
	AZ(match(out, sizeof out,
		":method", "GET",
		":scheme", "http",
		":path", "/",
		":authority", "www.example.com",
		NULL));

	/* C.4.2 */
	in_l = hexbuf(in, sizeof in,
	    "8286 84be 5886 a8eb 1064 9cbf");
	r = decode(d, t, in, in_l, out, sizeof out, mode);
	CHECK_RET(r, VHD_OK);
	AZ(match(out, sizeof out,
		":method", "GET",
		":scheme", "http",
		":path", "/",
		":authority", "www.example.com",
		"cache-control", "no-cache",
		NULL));

	/* C.4.3 */
	in_l = hexbuf(in, sizeof in,
	    "8287 85bf 4088 25a8 49e9 5ba9 7d7f 8925"
	    "a849 e95b b8e8 b4bf");
	r = decode(d, t, in, in_l, out, sizeof out, mode);
	CHECK_RET(r, VHD_OK);
	AZ(match(out, sizeof out,
		":method", "GET",
		":scheme", "https",
		":path", "/index.html",
		":authority", "www.example.com",
		"custom-key", "custom-value",
		NULL));

	VHT_Fini(t);
}

static void
test_c5(unsigned mode)
{
	struct vht_table t[1];
	struct vhd_decode d[1];
	uint8_t in[256];
	size_t in_l;
	char out[256];
	enum vhd_ret_e r;

	/* See RFC 7541 Appendix C.5 */

	AZ(VHT_Init(t, 256));
	VHD_Init(d);

	/* C.5.1 */
	in_l = hexbuf(in, sizeof in,
	    "4803 3330 3258 0770 7269 7661 7465 611d"
	    "4d6f 6e2c 2032 3120 4f63 7420 3230 3133"
	    "2032 303a 3133 3a32 3120 474d 546e 1768"
	    "7474 7073 3a2f 2f77 7777 2e65 7861 6d70"
	    "6c65 2e63 6f6d");
	r = decode(d, t, in, in_l, out, sizeof out, mode);
	CHECK_RET(r, VHD_OK);
	AZ(match(out, sizeof out,
		":status", "302",
		"cache-control", "private",
		"date", "Mon, 21 Oct 2013 20:13:21 GMT",
		"location", "https://www.example.com",
		NULL));

	/* C.5.2 */
	in_l = hexbuf(in, sizeof in,
	    "4803 3330 37c1 c0bf");
	r = decode(d, t, in, in_l, out, sizeof out, mode);
	CHECK_RET(r, VHD_OK);
	AZ(match(out, sizeof out,
		":status", "307",
		"cache-control", "private",
		"date", "Mon, 21 Oct 2013 20:13:21 GMT",
		"location", "https://www.example.com",
		NULL));

	/* C.5.3 */
	in_l = hexbuf(in, sizeof in,
	    "88c1 611d 4d6f 6e2c 2032 3120 4f63 7420"
	    "3230 3133 2032 303a 3133 3a32 3220 474d"
	    "54c0 5a04 677a 6970 7738 666f 6f3d 4153"
	    "444a 4b48 514b 425a 584f 5157 454f 5049"
	    "5541 5851 5745 4f49 553b 206d 6178 2d61"
	    "6765 3d33 3630 303b 2076 6572 7369 6f6e"
	    "3d31");
	r = decode(d, t, in, in_l, out, sizeof out, mode);
	CHECK_RET(r, VHD_OK);
	AZ(match(out, sizeof out,
		":status", "200",
		"cache-control", "private",
		"date", "Mon, 21 Oct 2013 20:13:22 GMT",
		"location", "https://www.example.com",
		"content-encoding", "gzip",
		"set-cookie",
		"foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1",
		NULL));

	VHT_Fini(t);
}

static void
test_c6(unsigned mode)
{
	struct vht_table t[1];
	struct vhd_decode d[1];
	uint8_t in[256];
	size_t in_l;
	char out[256];
	enum vhd_ret_e r;

	/* See RFC 7541 Appendix C.6 */

	AZ(VHT_Init(t, 256));
	VHD_Init(d);

	/* C.6.1 */
	in_l = hexbuf(in, sizeof in,
	    "4882 6402 5885 aec3 771a 4b61 96d0 7abe"
	    "9410 54d4 44a8 2005 9504 0b81 66e0 82a6"
	    "2d1b ff6e 919d 29ad 1718 63c7 8f0b 97c8"
	    "e9ae 82ae 43d3");
	r = decode(d, t, in, in_l, out, sizeof out, mode);
	CHECK_RET(r, VHD_OK);
	AZ(match(out, sizeof out,
		":status", "302",
		"cache-control", "private",
		"date", "Mon, 21 Oct 2013 20:13:21 GMT",
		"location", "https://www.example.com",
		NULL));

	/* C.6.2 */
	in_l = hexbuf(in, sizeof in,
	    "4883 640e ffc1 c0bf");
	r = decode(d, t, in, in_l, out, sizeof out, mode);
	CHECK_RET(r, VHD_OK);
	AZ(match(out, sizeof out,
		":status", "307",
		"cache-control", "private",
		"date", "Mon, 21 Oct 2013 20:13:21 GMT",
		"location", "https://www.example.com",
		NULL));

	/* C.6.3 */
	in_l = hexbuf(in, sizeof in,
	    "88c1 6196 d07a be94 1054 d444 a820 0595"
	    "040b 8166 e084 a62d 1bff c05a 839b d9ab"
	    "77ad 94e7 821d d7f2 e6c7 b335 dfdf cd5b"
	    "3960 d5af 2708 7f36 72c1 ab27 0fb5 291f"
	    "9587 3160 65c0 03ed 4ee5 b106 3d50 07");
	r = decode(d, t, in, in_l, out, sizeof out, mode);
	CHECK_RET(r, VHD_OK);
	AZ(match(out, sizeof out,
		":status", "200",
		"cache-control", "private",
		"date", "Mon, 21 Oct 2013 20:13:22 GMT",
		"location", "https://www.example.com",
		"content-encoding", "gzip",
		"set-cookie",
		"foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1",
		NULL));

	VHT_Fini(t);
}

#define do_test(name)						\
	do {							\
		printf("Doing test: %s\n", #name);		\
		name(0);					\
		printf("Doing test: %s 1IN\n", #name);		\
		name(M_1IN);					\
		printf("Doing test: %s 1OUT\n", #name);		\
		name(M_1OUT);					\
		printf("Doing test: %s 1IN|1OUT\n", #name);	\
		name(M_1IN|M_1OUT);				\
		printf("Test finished: %s\n\n", #name);		\
	} while (0)

int
main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "-v"))
		verbose = 1;
	else if (argc != 1) {
		fprintf(stderr, "Usage: %s [-v]\n", argv[0]);
		return (1);
	}

	if (verbose) {
		printf("sizeof (struct vhd_int)=%zu\n",
		    sizeof (struct vhd_int));
		printf("sizeof (struct vhd_lookup)=%zu\n",
		    sizeof (struct vhd_lookup));
		printf("sizeof (struct vhd_raw)=%zu\n",
		    sizeof (struct vhd_raw));
		printf("sizeof (struct vhd_huffman)=%zu\n",
		    sizeof (struct vhd_huffman));
		printf("sizeof (struct vhd_decode)=%zu\n",
		    sizeof (struct vhd_decode));
	}

	do_test(test_integer);
	do_test(test_raw);
	do_test(test_huffman);

	do_test(test_c2);
	do_test(test_c3);
	do_test(test_c4);
	do_test(test_c5);
	do_test(test_c6);

	return (0);
}

#endif	/* DECODE_TEST_DRIVER */
