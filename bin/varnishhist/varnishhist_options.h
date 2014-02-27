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
 * Option definitions for varnishhist
 */

#include "vapi/vapi_options.h"
#include "vut_options.h"

#define HIS_OPT_g							\
	VOPT("g:", "[-g <request|vxid>]", "Grouping mode (default: vxid)",		\
	    "The grouping of the log records. The default is to group"	\
	    " by vxid."							\
	)

#define HIS_OPT_p							\
	VOPT("p:", "[-p period]", "Refresh period",			\
	    "Specified the number of seconds between screen refreshes."	\
	    " Default is 1 second, and can be changed at runtime by"	\
	    " pressing the [1-9] keys."					\
	)

#define HIS_OPT_P							\
	VOPT("P:", "[-P <size|responsetime|tag:field_num:min:max>]",	\
	    "Profile definition",					\
	    "Either specify \"size\" or \"responstime\" profile or"	\
	    " create a new one. Define the tag we'll look for, and the"	\
	    " field number of the value we are interested in. min and"	\
	    " max are the boundaries of the graph (these are power of"	\
	    " tens)."							\
	)

VSL_OPT_C
VUT_OPT_d
HIS_OPT_g
VUT_OPT_h
VSL_OPT_L
VUT_OPT_n
VUT_OPT_N
HIS_OPT_p
HIS_OPT_P
VUT_OPT_q
VUT_OPT_r
VSL_OPT_T
VUT_OPT_V
