/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
	return (p[(unsigned)x>>3] & (0x80U >> ((unsigned)x & 7)));
}

enum feature_bits {
#define FEATURE_BIT(U, l, d, ld) FEATURE_##U,
#include "tbl/feature_bits.h"
       FEATURE_Reserved
};

static inline int
COM_FEATURE(const volatile uint8_t *p, enum feature_bits x)
{
	return (p[(unsigned)x>>3] & (0x80U >> ((unsigned)x & 7)));
}


struct poolparam {
	unsigned		min_pool;
	unsigned		max_pool;
	double			max_age;
};

struct params {

#define	ptyp_bool	unsigned
#define	ptyp_bytes	ssize_t
#define	ptyp_bytes_u	unsigned
#define	ptyp_double	double
#define	ptyp_poolparam	struct poolparam
#define	ptyp_timeout	double
#define	ptyp_uint	unsigned
#define	ptyp_vsl_buffer	unsigned
#define	ptyp_vsl_reclen	unsigned
#define PARAM(nm, ty, mi, ma, de, un, fl, st, lt, fn) ptyp_##ty nm;
#include <tbl/params.h>
#undef ptyp_bool
#undef ptyp_bytes
#undef ptyp_bytes_u
#undef ptyp_double
#undef ptyp_poolparam
#undef ptyp_timeout
#undef ptyp_uint
#undef ptyp_vsl_buffer
#undef ptyp_vsl_reclen

	/* Unprivileged user / group */
	uid_t			uid;
	gid_t			gid;

	/* Worker threads and pool */
	unsigned		wthread_min;
	unsigned		wthread_max;
	unsigned		wthread_reserve;
	double			wthread_timeout;
	unsigned		wthread_pools;
	double			wthread_add_delay;
	double			wthread_fail_delay;
	double			wthread_destroy_delay;
	double			wthread_watchdog;
	unsigned		wthread_stats_rate;
	ssize_t			wthread_stacksize;
	unsigned		wthread_queue_limit;

	struct vre_limits	vre_limits;

	struct poolparam	req_pool;
	struct poolparam	sess_pool;
	struct poolparam	vbo_pool;

	uint8_t			vsl_mask[256>>3];
	uint8_t			debug_bits[(DBG_Reserved+7)>>3];
	uint8_t			feature_bits[(FEATURE_Reserved+7)>>3];
};
