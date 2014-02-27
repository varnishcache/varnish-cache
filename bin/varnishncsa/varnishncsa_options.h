/*-
 * Copyright (c) 2013 Varnish Software AS
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
 */

#include "vapi/vapi_options.h"
#include "vut_options.h"

#define NCSA_OPT_a							\
	VOPT("a", "[-a]", "Append to file",				\
	    "When writing output to a file, append to it rather than"	\
	    " overwrite it."						\
	)

#define NCSA_OPT_F							\
	VOPT("F:", "[-F format]", "Set output format",			\
	    "Set the output log format string."				\
	)

#define NCSA_OPT_g							\
	VOPT("g:", "[-g <request|vxid>]", "Grouping mode (default: vxid)",		\
	    "The grouping of the log records. The default is to group"	\
	    " by vxid."							\
	)

#define NCSA_OPT_w							\
	VOPT("w:", "[-w filename]", "Output filename",			\
	    "Redirect output to file. The file will be overwritten"	\
	    " unless the -a option was specified. If the application"	\
	    " receives a SIGHUP the file will be reopened allowing"	\
	    " the old one to be rotated away."				\
	)

NCSA_OPT_a
VSL_OPT_C
VUT_OPT_d
VUT_OPT_D
NCSA_OPT_F
NCSA_OPT_g
VUT_OPT_h
VUT_OPT_n
VUT_OPT_N
VUT_OPT_P
VUT_OPT_q
VUT_OPT_V
NCSA_OPT_w
