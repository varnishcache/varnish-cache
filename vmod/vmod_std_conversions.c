/*-
 * Copyright (c) 2010-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@FreeBSD.org>
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

#include "config.h"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>

#include <netdb.h>

#include "cache/cache.h"

#include "vnum.h"
#include "vsa.h"
#include "vss.h"
#include "vtim.h"
#include "vcc_std_if.h"

/*
 * technically, as our VCL_INT is int64_t, its limits are INT64_MIN
 * .. INT64_MAX.
 *
 * We redistrict to VRT_INTEGER_MIN .. VRT_INTEGER_MAX
 */

/* limited by using double for conversions */
#define VCL_BYTES_MAX ((INT64_C(1)<<53)-1)

static
int onearg(VRT_CTX, const char *f, int nargs)
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

	if (!onearg(ctx, "duration", nargs))
		return (0);

	if (a->valid_real)
		return ((VCL_DURATION)a->real);

	if (a->valid_integer)
		return ((VCL_DURATION)a->integer);

	if (a->valid_s) {
		r = VNUM_duration(a->s);
		if (!isnan(r))
			return (r);
	}

	if (a->valid_fallback)
		return (a->fallback);

	VRT_fail(ctx, "std.duration: conversion failed");
	return (0);
}

VCL_BYTES v_matchproto_(td_std_bytes)
vmod_bytes(VRT_CTX, struct VARGS(bytes) *a)
{
	uintmax_t r;
	VCL_REAL rr;
	int nargs;
	const char *errtxt;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	nargs = a->valid_s + a->valid_real + a->valid_integer;

	if (!onearg(ctx, "bytes", nargs))
		return (0);

	if (a->valid_s) {
		errtxt = VNUM_2bytes(a->s, &r, 0);
		if (errtxt == NULL && r <= VCL_BYTES_MAX)
			return ((VCL_BYTES)r);
	}

	if (a->valid_real && !isnan(a->real) && a->real >= 0) {
		rr = trunc(a->real);
		if (rr <= (VCL_REAL)VCL_BYTES_MAX)
			return ((VCL_BYTES)rr);
	}

	if (a->valid_integer && a->integer >= 0)
		return ((VCL_BYTES)a->integer);

	if (a->valid_fallback)
		return (a->fallback);

	VRT_fail(ctx, "std.bytes: conversion failed");
	return (0);
}

VCL_INT v_matchproto_(td_std_integer)
vmod_integer(VRT_CTX, struct VARGS(integer) *a)
{
	const char *p, *errtxt = NULL;
	double r, tmp;
	int nargs;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	nargs = a->valid_s + a->valid_boolean + a->valid_bytes +
	    a->valid_duration + a->valid_real + a->valid_time;

	if (!onearg(ctx, "integer", nargs))
		return (0);

	r = NAN;
	if (a->valid_boolean)
		return (a->boolean ? 1 : 0);

	if (a->valid_bytes)
		return (a->bytes);

	if (a->valid_s && a->s != NULL) {
		p = a->s;
		r = SF_Parse_Number(&p, 0, &errtxt);
		if (!errno && *p == '\0' && modf(r, &tmp) == 0.0)
			return ((VCL_INT)r);
		r = NAN;
	}

	if (a->valid_duration)
		r = a->duration;

	if (a->valid_real)
		r = a->real;

	if (a->valid_time)
		r = a->time;

	if (!isnan(r)) {
		r = trunc(r);
		if (r >= VRT_INTEGER_MIN && r <= VRT_INTEGER_MAX)
			return ((VCL_INT)r);
	}

	if (a->valid_fallback)
		return (a->fallback);

	if (errtxt != NULL)
		VRT_fail(ctx, "std.integer: conversion failed: %s", errtxt);
	else
		VRT_fail(ctx, "std.integer: conversion failed");
	return (0);
}

VCL_IP
vmod_ip(VRT_CTX, struct VARGS(ip) *a)
{
	uintptr_t sn;
	void *p;
	VCL_IP retval = NULL, fb = bogo_ip;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (a->valid_fallback) {
		if (a->fallback == NULL || !VSA_Sane(a->fallback)) {
			VRT_fail(ctx, "std.ip: invalid fallback");
			return (fb);
		}
		fb = a->fallback;
	}

	sn = WS_Snapshot(ctx->ws);
	p = WS_Alloc(ctx->ws, vsa_suckaddr_len);
	if (p == NULL) {
		VRT_fail(ctx, "std.ip: insufficient workspace");
		return (fb);
	}

	if (a->s != NULL)
		retval = VSS_ResolveFirst(
		    p, a->s, a->valid_p ? a->p : "80",
		    AF_UNSPEC, SOCK_STREAM,
		    a->resolve ? 0 : AI_NUMERICHOST|AI_NUMERICSERV);

	if (retval != NULL)
		return (retval);

	WS_Reset(ctx->ws, sn);

	if (!a->valid_fallback)
		VRT_fail(ctx, "std.ip: conversion failed");

	return (fb);
}

VCL_REAL v_matchproto_(td_std_real)
vmod_real(VRT_CTX, struct VARGS(real) *a)
{
	VCL_REAL r;
	const char *p, *errtxt = NULL;
	int nargs;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	nargs = a->valid_s + a->valid_integer + a->valid_boolean + a->valid_bytes +
	    a->valid_duration + a->valid_time;

	if (!onearg(ctx, "real", nargs))
		return (0);

	if (a->valid_integer)
		return ((VCL_REAL)a->integer);

	if (a->valid_boolean)
		return ((VCL_REAL)(a->boolean ? 1 : 0));

	if (a->valid_bytes)
		return ((VCL_REAL)a->bytes);

	if (a->valid_duration)
		return ((VCL_REAL)a->duration);

	if (a->valid_time)
		return ((VCL_REAL)a->time);

	if (a->valid_s && a->s != NULL) {
		p = a->s;
		r = SF_Parse_Decimal(&p, 0, &errtxt);
		if (!errno && *p == '\0')
			return (r);
	}

	if (a->valid_fallback)
		return (a->fallback);

	if (errtxt != NULL)
		VRT_fail(ctx, "std.real: conversion failed: %s", errtxt);
	else
		VRT_fail(ctx, "std.real: conversion failed");
	return (0);
}

VCL_REAL v_matchproto_(td_std_round)
vmod_round(VRT_CTX, VCL_REAL r)
{
	(void) ctx;
	return (round(r));
}

VCL_TIME v_matchproto_(td_std_time)
vmod_time(VRT_CTX, struct VARGS(time)* a)
{
	double r;
	int nargs;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	nargs = a->valid_s + a->valid_real + a->valid_integer;

	if (!onearg(ctx, "time", nargs))
		return (0);

	if (a->valid_integer)
		return ((VCL_REAL)a->integer);

	if (a->valid_real)
		return ((VCL_REAL)a->real);

	if (a->valid_s && a->s != NULL) {
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

VCL_STRING v_matchproto_(td_std_strftime)
vmod_strftime(VRT_CTX, VCL_TIME t, VCL_STRING fmt)
{
	struct tm tm;
	time_t tt;
	size_t r;
	unsigned spc;
	char *s;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	tt = (time_t)(intmax_t)t;
	if (gmtime_r(&tt, &tm) == NULL)
		return ("");

	spc = WS_ReserveAll(ctx->ws);
	s = WS_Reservation(ctx->ws);
	r = strftime(s, spc, fmt, &tm);
	if (r == 0) {
		WS_Release(ctx->ws, 0);
		return ("");
	}
	r++;
	WS_Release(ctx->ws, r);
	return (s);
}

/* These functions are deprecated as of 2019-03-15 release */

VCL_INT v_matchproto_(td_std_real2integer)
vmod_real2integer(VRT_CTX, VCL_REAL r, VCL_INT i)
{
	VCL_INT retval;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (!VRT_REAL_is_valid(r))
		return (i);
	retval = (VCL_INT)round(r);
	if (!VRT_INT_is_valid(retval))
		return (i);
	return (retval);
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
	if (t > VRT_INTEGER_MAX || t < VRT_INTEGER_MIN)
		return (i);
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
