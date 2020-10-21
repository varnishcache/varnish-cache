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


struct parspec mgt_parspec[] = {
#define PARAM_ALL
#define PARAM(ty, nm, ...) { #nm, __VA_ARGS__ },
#include "tbl/params.h"

	{ "pcre_match_limit", tweak_uint,
		&mgt_param.vre_limits.match,
		"1", NULL, "10000",
		NULL,
		"The limit for the number of calls to the internal match()"
		" function in pcre_exec().\n\n"
		"(See: PCRE_EXTRA_MATCH_LIMIT in pcre docs.)\n\n"
		"This parameter limits how much CPU time"
		" regular expression matching can soak up." },
	{ "pcre_match_limit_recursion", tweak_uint,
		&mgt_param.vre_limits.match_recursion,
		"1", NULL, "20",
		NULL,
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
		"Testcase r01576 can be useful when tuning this parameter." },

	{ NULL, NULL, NULL }
};
