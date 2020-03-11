/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 */

#include "config.h"

#include <stdio.h>

#include "mgt/mgt.h"

#include "mgt/mgt_param.h"


#define MEMPOOL_TEXT							\
	"The three numbers are:\n"					\
	"\tmin_pool\tminimum size of free pool.\n"			\
	"\tmax_pool\tmaximum size of free pool.\n"			\
	"\tmax_age\tmax age of free element."

struct parspec mgt_parspec[] = {
#define PARAM_ALL
#define PARAM(ty, nm, ...) { #nm, __VA_ARGS__ },
#include "tbl/params.h"
	{
		.name = "pool_req",
		.func = tweak_poolparam,
		.priv = &mgt_param.req_pool,
		.def = "10,100,10",
		.descr =
		"Parameters for per worker pool request memory pool.\n\n"
		MEMPOOL_TEXT
	}, {
		.name = "pool_sess",
		.func = tweak_poolparam,
		.priv = &mgt_param.sess_pool,
		.def = "10,100,10",
		.descr =
		"Parameters for per worker pool session memory pool.\n\n"
		MEMPOOL_TEXT
	}, {
		.name = "pool_vbo",
		.func = tweak_poolparam,
		.priv = &mgt_param.vbo_pool,
		.def = "10,100,10",
		.descr =
		"Parameters for backend object fetch memory pool.\n\n"
		MEMPOOL_TEXT
	}, {
		.name = NULL
	}
};
