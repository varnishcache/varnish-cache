/*-
 * Copyright (c) 2016 Varnish Software AS
 * All rights reserved.
 *
 * Author: Federico G. Schwindt <fgsch@lodoss.net>
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
 * Option definitions for varnishstat
 */

#include "vapi/vapi_options.h"
#include "vut_options.h"

#define STAT_OPT_1							\
	VOPT("1", "[-1]", "Print the statistics to stdout",		\
	    "Instead of presenting a continuously updated display,"	\
	    " print the statistics to stdout."				\
	)
#define STAT_OPT_f							\
	VOPT("f:", "[-f <glob>]", "Field inclusion glob",		\
	    "Field inclusion glob."					\
	    " Use backslash to escape characters. If the argument"	\
	    " starts with '^' it is used as an exclusive glob."		\
	    " Multiple -f arguments may be given. Inclusive globs"	\
	    " are accumulative and are run before exclusive ones."	\
	)
#define STAT_OPT_j							\
	VOPT("j", "[-j]", "Print statistics to stdout as JSON",		\
	    "Print statistics to stdout as JSON."			\
	)
#define STAT_OPT_l							\
	VOPT("l", "[-l]",						\
	    "Lists the available fields to use with the -f option",	\
	    "Lists the available fields to use with the -f option."	\
	)
#define STAT_OPT_x							\
	VOPT("x", "[-x]", "Print statistics to stdout as XML",		\
	    "Print statistics to stdout as XML."			\
	)

STAT_OPT_1
STAT_OPT_f
VUT_OPT_h
STAT_OPT_j
STAT_OPT_l
VUT_OPT_n
VUT_OPT_t
VUT_GLOBAL_OPT_V
STAT_OPT_x
