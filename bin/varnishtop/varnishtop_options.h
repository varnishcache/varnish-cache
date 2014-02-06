/*-
 * Copyright (c) 2014 Varnish Software AS
 * All rights reserved.
 *
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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
 * Option definitions for varnishtop
 */

#include "vapi/vapi_options.h"
#include "vut_options.h"

#define TOP_OPT_1							\
	VOPT("1", "[-1]", "Run once",					\
	    "Instead of a continously updated display, print the"	\
	    " statistics once and exit. Implies ``-d``."		\
	)

#define TOP_OPT_f							\
	VOPT("f", "[-f]", "First field only",				\
	    "Sort and group only on the first field of each log entry."	\
	    " This is useful when displaying e.g. stataddr entries,"	\
	    " where the first field is the client IP address."		\
	)

#define TOP_OPT_p							\
	VOPT("p:", "[-p period]", "Sampling period",			\
	    "Specified the number of seconds to measure over, the"	\
	    " default is 60 seconds. The first number in the list is"	\
	    " the average number of requests seen over this time"	\
	    " period."							\
	)

TOP_OPT_1
VSL_OPT_b
VSL_OPT_c
VSL_OPT_C
VUT_OPT_d
TOP_OPT_f
VUT_OPT_g
VUT_OPT_h
VSL_OPT_i
VSL_OPT_I
VSL_OPT_L
VUT_OPT_n
VUT_OPT_N
TOP_OPT_p
VUT_OPT_q
VUT_OPT_r
VSL_OPT_T
VSL_OPT_x
VSL_OPT_X
VUT_OPT_V
