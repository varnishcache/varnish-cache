/*-
 * Copyright (c) 2010-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@FreeBSD.org>
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

#include <ctype.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>

#include <netdb.h>

#include "cache/cache.h"

#include "vnum.h"
#include "vsa.h"
#include "vtim.h"
#include "vcc_if.h"

static inline int onearg(VRT_CTX, const char *f, int nargs)
{
	if (nargs == 1)
		return (1);
	VRT_fail(ctx, "std.%s: %s arguments", f,
	    nargs > 1 ? "too many" : "not enough");
	return (0);
}

/*
 * not handling real arg isfinite() / nan() : caller error
 * always trunc, never round
 */

VCL_DURATION v_matchproto_(td_std_duration)
vmod_duration(VRT_CTX, struct VARGS(duration) *a)
{
	double r;
	int nargs;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	nargs = a->valid_s + a->valid_real + a->valid_integer;

	if (! onearg(ctx, "duration", nargs))
		return (0);

	if (a->valid_real)
		return ((VCL_DURATION)a->real);
	if (a->valid_integer)
		return ((VCL_DURATION)a->integer);

	assert(a->valid_s);

	r = VNUM_duration(a->s);

	if (! isnan(r))
		return (r);

	if (a->valid_fallback)
		return (a->fallback);

	VRT_fail(ctx, "std.duration: conversion failed");
	return (0);
}

VCL_BYTES v_matchproto_(td_std_bytes)
vmod_bytes(VRT_CTX, struct VARGS(bytes) *a)
{
	uintmax_t r;
	VCL_BYTES b;
	VCL_REAL rr;
	int nargs;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	nargs = a->valid_s + a->valid_real + a->valid_integer;

	if (! onearg(ctx, "bytes", nargs))
		return (0);

	b = -1;
	if (a->valid_s) {
		if (VNUM_2bytes(a->s, &r, 0) == NULL &&
		    r <= VCL_BYTES_MAX)
			b = (VCL_BYTES)r;
	} else if (a->valid_real) {
		rr = trunc(a->real);
		if (rr <= (VCL_REAL)VCL_BYTES_MAX)
			b = (VCL_BYTES)rr;
	} else if (a->valid_integer) {
		b = (VCL_BYTES)a->integer;
	} else {
		INCOMPL();
	}

	if (b >= 0)
		return (b);

	if (a->valid_fallback)
		return (a->fallback);

	VRT_fail(ctx, "std.bytes: conversion failed");
	return (0);
}

VCL_INT v_matchproto_(td_std_integer)
vmod_integer(VRT_CTX, struct VARGS(integer) *a)
{
	const char *e;
	double r;
	int nargs;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	nargs = a->valid_s + a->valid_bool + a->valid_bytes +
	    a->valid_duration + a->valid_real + a->valid_time;

	if (! onearg(ctx, "integer", nargs))
		return (0);

	r = NAN;
	if (a->valid_bool) {
		return (!! a->bool);
	} else if (a->valid_bytes) {
		return (a->bytes);
	} else if (a->valid_s) {
		if (a->s) {
			r = VNUMpfx(a->s, &e);
			if (e != NULL)
				r = NAN;
		}
	} else if (a->valid_duration) {
		r = a->duration;
	} else if (a->valid_real) {
		r = a->real;
	} else if (a->valid_time) {
		r = a->time;
	} else {
		INCOMPL();
	}

	if (! isnan(r)) {
		r = trunc(r);
		if (r >= VCL_INT_MIN && r <= VCL_INT_MAX)
			return ((VCL_INT)r);
	}

	if (a->valid_fallback)
		return (a->fallback);

	VRT_fail(ctx, "std.integer: conversion failed");
	return (0);
}

VCL_IP
vmod_ip(VRT_CTX, VCL_STRING s, VCL_IP d, VCL_BOOL n)
{
	struct addrinfo hints, *res0 = NULL;
	const struct addrinfo *res;
	int error;
	void *p;
	const struct suckaddr *r;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(d);
	assert(VSA_Sane(d));

	p = WS_Alloc(ctx->ws, vsa_suckaddr_len);
	if (p == NULL) {
		VSLb(ctx->vsl, SLT_VCL_Error,
		    "vmod std.ip(): insufficient workspace");
		return (d);
	}
	r = NULL;

	if (s != NULL) {
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = PF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		if (!n)
			hints.ai_flags |= AI_NUMERICHOST;
		error = getaddrinfo(s, "80", &hints, &res0);
		if (!error) {
			for (res = res0; res != NULL; res = res->ai_next) {
				r = VSA_Build(p, res->ai_addr, res->ai_addrlen);
				if (r != NULL)
					break;
			}
		}
	}
	if (r == NULL) {
		WS_Reset(ctx->ws, (uintptr_t)p);
		r = d;
	}
	if (res0 != NULL)
		freeaddrinfo(res0);
	return (r);
}

VCL_REAL v_matchproto_(td_std_real)
vmod_real(VRT_CTX, struct VARGS(real) *a)
{
	VCL_REAL r;
	int nargs;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	nargs = a->valid_s + a->valid_integer + a->valid_bool + a->valid_bytes +
	    a->valid_duration + a->valid_time;

	if (! onearg(ctx, "real", nargs))
		return (0);

	if (a->valid_integer)
		return ((VCL_REAL)a->integer);
	else if (a->valid_bool)
		return ((VCL_REAL)(!! a->bool));
	else if (a->valid_bytes)
		return ((VCL_REAL)a->bytes);
	else if (a->valid_duration)
		return ((VCL_REAL)a->duration);
	else if (a->valid_time)
		return ((VCL_REAL)a->time);

	assert(a->valid_s);

	r = NAN;
	if (a->s != NULL)
		r = VNUM(a->s);

	if (!isnan(r))
		return (r);

	if (a->valid_fallback)
		return (a->fallback);

	VRT_fail(ctx, "std.real: conversion failed");
	return (0);
}

VCL_REAL v_matchproto_(td_std_round)
vmod_round(VRT_CTX, VCL_REAL r)
{
	(void) ctx;
	return (round(r));
}


VCL_INT v_matchproto_(td_std_real2integer)
vmod_real2integer(VRT_CTX, VCL_REAL r, VCL_INT i)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (!isfinite(r))
		return (i);
	r = round(r);
	if (r > VCL_INT_MAX || r < VCL_INT_MIN)
		return(i);
	return ((VCL_INT)r);
}

VCL_TIME v_matchproto_(td_std_real2time)
vmod_real2time(VRT_CTX, VCL_REAL r, VCL_TIME t)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (!isfinite(r))
		return (t);

	return (r);
}

VCL_INT v_matchproto_(td_std_time2integer)
vmod_time2integer(VRT_CTX, VCL_TIME t, VCL_INT i)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (!isfinite(t))
		return (i);
	t = round(t);
	if (t > VCL_INT_MAX || t < VCL_INT_MIN)
		return(i);
	return ((VCL_INT)t);
}

VCL_REAL v_matchproto_(td_std_time2real)
vmod_time2real(VRT_CTX, VCL_TIME t, VCL_REAL r)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (!isfinite(t))
		return (r);

	return (t);
}

VCL_TIME v_matchproto_(td_std_time)
vmod_time(VRT_CTX, struct VARGS(time)* a)
{
	double r;
	int nargs;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	nargs = a->valid_s + a->valid_real + a->valid_integer;

	if (! onearg(ctx, "time", nargs))
		return (0);

	if (a->valid_integer)
		return ((VCL_REAL)a->integer);
	else if (a->valid_real)
		return ((VCL_REAL)a->real);

	assert(a->valid_s);

	if (a->s) {
		r = VTIM_parse(a->s);
		if (r)
			return (r);

		r = VNUM(a->s);

		if (!isnan(r) && r > 0)
			return (r);
	}

	if (a->valid_fallback)
		return (a->fallback);

	VRT_fail(ctx, "std.time: conversion failed");
	return (0);
}
