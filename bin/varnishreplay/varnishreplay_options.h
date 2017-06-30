/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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

#define RPL_OPT_a							\
	VOPT("a:", "[-a <address:port>]", "address",				\
	    "Address to connect."	\
	)

#define RPL_OPT_D							\
	VOPT("D", "[-D]", "Debug",				\
	    "Increase debug level"	\
	    " may be used few times."					\
	)

#define RPL_OPT_f							\
	VOPT("f:", "[-f <filename>]", "token file",				\
	    "Token file, format:"	\
	    "\t tok=val1\n"	\
	    "\t   ...\n"	\
	    "\t tok=valn\n"	\
	)

#define RPL_OPT_m							\
	VOPT("m:", "[-m <int>]", "max thread number",				\
	    "Max thread number."	\
	)

#define RPL_OPT_t							\
	VOPT("t:", "[-t <name=token>]", "token",				\
	    "Token"	\
	)

#define RPL_OPT_w							\
	VOPT("w:", "[-w delay]", "thread sleep delay",				\
	    "Thread sleep delay "	\
	)

#define RPL_OPT_y							\
	VOPT("y", "[-y]", "use 'Y-Varnish' header",				\
	    "Use 'Y-Varnish' header"	\
	)

#define RPL_OPT_z							\
	VOPT("z", "[-z]", "use new thread choice algorithm",				\
	    "Use new thread choice algorithm"	\
	)


RPL_OPT_a
RPL_OPT_D
RPL_OPT_f
VUT_OPT_h
VUT_OPT_n
VUT_OPT_r
RPL_OPT_t
RPL_OPT_w
RPL_OPT_y
RPL_OPT_z
