/*-
 * Copyright 2025 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Author: Nils Goroll <slink@uplex.de>
 *
 * Includes code by Bruce Dawson
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
 */

#include "config.h"

#include <float.h>
#include <math.h>

#include "miniobj.h"
#include "cache/cache.h"
#include "vcc_math_if.h"

/*
 * this is AlmostEqualRelativeAndAbs() by Bruce Dawson
 * https://randomascii.wordpress.com/2012/02/25/comparing-floating-point-numbers-2012-edition/
 */
VCL_BOOL
vmod_approx(VRT_CTX, VCL_REAL a, VCL_REAL b, VCL_REAL maxDiff, VCL_REAL maxRelDiff)
{
	(void)ctx;

	if (fpclassify(maxDiff) == FP_ZERO)
		maxDiff = 4 * DBL_EPSILON;

	if (fpclassify(maxRelDiff) == FP_ZERO)
		maxRelDiff = DBL_EPSILON;

	VCL_REAL diff = fabs(a - b);
	if (diff <= maxDiff)
		return (1);

	a = fabs(a);
	b = fabs(b);
	VCL_REAL largest = (b > a) ? b : a;

	if (diff <= largest * maxRelDiff)
		return (1);

	return (0);
}

static int
valid_fmt(VCL_STRING fmt)
{
	if (fmt == NULL || *fmt != '%')
		return (0);
	fmt++;
	if (*fmt == '.') {
		fmt++;
		while (*fmt >= '0' && *fmt <= '9')
			fmt++;
	}
	switch (*fmt) {
	case 'a':
	case 'A':
	case 'e':
	case 'E':
	case 'f':
	case 'F':
	case 'g':
	case 'G':
		return (1);
	default:
		return (0);
	}
}

VCL_STRING
vmod_strfromd(VRT_CTX, VCL_STRING fmt, VCL_REAL fp)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (! valid_fmt(fmt)) {
		VRT_fail(ctx, "math.strfromd(\"%s\", ...): Invalid format",
		    fmt ? fmt : "<NULL>");
		return (NULL);
	}

	return (WS_Printf(ctx->ws, fmt, fp));
}
