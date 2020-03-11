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
#define PARAM(ty, nm, ...) { #nm, __VA_ARGS__ },
#include "tbl/params.h"

	{
		.name = "cc_command",
		.func = tweak_string,
		.priv = &mgt_cc_cmd,
		.def = VCC_CC,
		.descr =
		"Command used for compiling the C source code to a "
		"dlopen(3) loadable object.  Any occurrence of %s in "
		"the string will be replaced with the source file name, "
		"and %o will be replaced with the output file name.",
		.flags = MUST_RELOAD
	}, {
		.name = "vcl_path",
		.func = tweak_string,
		.priv = &mgt_vcl_path,
		.def = VARNISH_VCL_DIR,
		.descr =
		"Directory (or colon separated list of directories) "
		"from which relative VCL filenames (vcl.load and "
		"include) are to be found.  By default Varnish searches "
		"VCL files in both the system configuration and shared "
		"data directories to allow packages to drop their VCL "
		"files in a standard location where relative includes "
		"would work."
	}, {
		.name = "vmod_path",
		.func = tweak_string,
		.priv = &mgt_vmod_path,
		.def = VARNISH_VMOD_DIR,
		.descr =
		"Directory (or colon separated list of directories) "
		"where VMODs are to be found."
	}, {
		.name = "vcc_err_unref",
		.func = tweak_bool,
		.priv = &mgt_vcc_err_unref,
		.def = "on",
		.units = "bool",
		.descr =
		"Unreferenced VCL objects result in error."
	}, {
		.name = "vcc_allow_inline_c",
		.func = tweak_bool,
		.priv = &mgt_vcc_allow_inline_c,
		.def = "off",
		.units = "bool",
		.descr =
		"Allow inline C code in VCL."
	}, {
		.name = "vcc_unsafe_path",
		.func = tweak_bool,
		.priv = &mgt_vcc_unsafe_path,
		.def = "on",
		.units = "bool",
		.descr =
		"Allow '/' in vmod & include paths.\n"
		"Allow 'import ... from ...'."
	}, {
		.name = "pcre_match_limit",
		.func = tweak_uint,
		.priv = &mgt_param.vre_limits.match,
		.min = "1",
		.def = "10000",
		.descr =
		"The limit for the number of calls to the internal match()"
		" function in pcre_exec().\n\n"
		"(See: PCRE_EXTRA_MATCH_LIMIT in pcre docs.)\n\n"
		"This parameter limits how much CPU time"
		" regular expression matching can soak up."
	}, {
		.name = "pcre_match_limit_recursion",
		.func = tweak_uint,
		.priv = &mgt_param.vre_limits.match_recursion,
		.min = "1",
		.def = "20",
		.descr =
		"The recursion depth-limit for the internal match() function"
		" in a pcre_exec().\n\n"
		"(See: PCRE_EXTRA_MATCH_LIMIT_RECURSION in pcre docs.)\n\n"
		"This puts an upper limit on the amount of stack used"
		" by PCRE for certain classes of regular expressions.\n\n"
		"We have set the default value low in order to"
		" prevent crashes, at the cost of possible regexp"
		" matching failures.\n\n"
		"Matching failures will show up in the log as VCL_Error"
		" messages with regexp errors -27 or -21.\n\n"
		"Testcase r01576 can be useful when tuning this parameter."
	}, {
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
