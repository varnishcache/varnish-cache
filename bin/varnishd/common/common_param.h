/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 * This file contains the heritage passed when mgt forks cache
 */

#ifdef COMMON_COMMON_PARAM_H
#error "Multiple includes of common/common_param.h"
#endif
#define COMMON_COMMON_PARAM_H

#include "vre.h"

#define VSM_CLASS_PARAM		"Params"

enum debug_bits {
#define DEBUG_BIT(U, l, d) DBG_##U,
#include "tbl/debug_bits.h"
       DBG_Reserved
};

static inline int
COM_DO_DEBUG(const volatile uint8_t *p, enum debug_bits x)
{
	return ((p[(unsigned)x>>3] & (0x80U >> ((unsigned)x & 7))) != 0);
}

enum experimental_bits {
#define EXPERIMENTAL_BIT(U, l, d) EXPERIMENT_##U,
#include "tbl/experimental_bits.h"
       EXPERIMENT_Reserved
};

static inline int
COM_EXPERIMENT(const volatile uint8_t *p, enum experimental_bits x)
{
	return ((p[(unsigned)x>>3] & (0x80U >> ((unsigned)x & 7))) != 0);
}

enum feature_bits {
#define FEATURE_BIT(U, l, d) FEATURE_##U,
#include "tbl/feature_bits.h"
       FEATURE_Reserved
};

static inline int
COM_FEATURE(const volatile uint8_t *p, enum feature_bits x)
{
	return ((p[(unsigned)x>>3] & (0x80U >> ((unsigned)x & 7))) != 0);
}

enum vcc_feature_bits {
#define VCC_FEATURE_BIT(U, l, d) VCC_FEATURE_##U,
#include "tbl/vcc_feature_bits.h"
       VCC_FEATURE_Reserved
};

static inline int
COM_VCC_FEATURE(const volatile uint8_t *p, enum vcc_feature_bits x)
{
	return ((p[(unsigned)x>>3] & (0x80U >> ((unsigned)x & 7))) != 0);
}

struct poolparam {
	unsigned		min_pool;
	unsigned		max_pool;
	vtim_dur		max_age;
};

#define PARAM_BITMAP(name, len) typedef uint8_t name[(len + 7)>>3]

PARAM_BITMAP(vsl_mask_t,	256);
PARAM_BITMAP(debug_t,		DBG_Reserved);
PARAM_BITMAP(experimental_t,	EXPERIMENT_Reserved);
PARAM_BITMAP(feature_t,		FEATURE_Reserved);
PARAM_BITMAP(vcc_feature_t,	VCC_FEATURE_Reserved);
#undef PARAM_BITMAP

struct params {

#define ptyp_boolean		unsigned
#define ptyp_bytes		ssize_t
#define ptyp_bytes_u		unsigned
#define ptyp_debug		debug_t
#define ptyp_double		double
#define ptyp_experimental	experimental_t
#define ptyp_feature		feature_t
#define ptyp_poolparam		struct poolparam
#define ptyp_thread_pool_max	unsigned
#define ptyp_thread_pool_min	unsigned
#define ptyp_timeout		vtim_dur
#define ptyp_uint		unsigned
#define ptyp_vcc_feature	vcc_feature_t
#define ptyp_vsl_buffer		unsigned
#define ptyp_vsl_mask		vsl_mask_t
#define ptyp_vsl_reclen		unsigned
#define PARAM(typ, fld, nm, ...)		\
	ptyp_##typ		fld;
#include <tbl/params.h>
#undef ptyp_boolean
#undef ptyp_bytes
#undef ptyp_bytes_u
#undef ptyp_debug
#undef ptyp_double
#undef ptyp_experimental
#undef ptyp_feature
#undef ptyp_poolparam
#undef ptyp_thread_pool_max
#undef ptyp_thread_pool_min
#undef ptyp_timeout
#undef ptyp_uint
#undef ptyp_vsl_buffer
#undef ptyp_vsl_mask
#undef ptyp_vsl_reclen

	struct vre_limits	vre_limits;

	/* Traffic management */
	unsigned		accept_traffic;
};
