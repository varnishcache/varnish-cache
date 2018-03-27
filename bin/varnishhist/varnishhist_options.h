/*-
 * Copyright (c) 2014-2015 Varnish Software AS
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
	VOPT("g:", "[-g <request|vxid>]",				\
	    "Grouping mode (default: vxid)",				\
	    "The grouping of the log records. The default is to group"	\
	    " by vxid."							\
	)

#define HIS_OPT_p							\
	VOPT("p:", "[-p <period>]", "Refresh period",			\
	    "Specified the number of seconds between screen refreshes."	\
	    " Default is 1 second, and can be changed at runtime by"	\
	    " pressing the [0-9] keys (powers of 2 in seconds"		\
	    " or + and - (double/halve the speed)."			\
	)

#define HIS_OPT_P							\
	VOPT("P:", "[-P <[cb:]tag:[prefix]:field_num[:min:max]>]",	\
	    "Custom profile definition",				\
	    "Graph the given custom definition defined as: an optional" \
	    " (c)lient or (b)ackend filter (defaults to client), the"	\
	    " tag we'll look for, a prefix to look for (can be empty,"	\
	    " but must be terminated by a colon) and the field number"	\
	    " of the value we are interested in. min and max are the"	\
	    " boundaries of the graph in powers of ten and default to"	\
	    " -6 and 3."						\
	)

#define HIS_OPT_B							\
	VOPT("B:", "[-B <factor>]",					\
	    "Time bending",						\
	    "Factor to bend time by. Particularly useful when"		\
	    " [-r]eading from a vsl file. =1 process in near real"	\
	    " time, <1 slow-motion, >1 time-lapse (useless unless"	\
	    " reading from a file). At runtime, < halves and"		\
	    " > doubles."						\
	    )

HIS_OPT_B
VSL_OPT_C
VUT_OPT_d
HIS_OPT_g
VUT_OPT_h
VSL_OPT_L
VUT_OPT_n
HIS_OPT_p
#define HIS_CLIENT	"client"
#define HIS_BACKEND	"backend"
#define HIS_NO_PREFIX	""
#define HIS_PROF(name,cb,tg,prefix,fld,hist_low,high_high,doc)		\
	VOPT("P:", "[-P " name "]",					\
	     "Predefined " cb " profile",				\
	     "Predefined " cb " profile: " doc				\
	     " (field " #fld " of " #tg " " prefix " VSL tag)."		\
	    )
#include "varnishhist_profiles.h"
#undef HIS_NO_PREFIX
#undef HIS_BACKEND
#undef HIS_CLIENT
#undef HIS_PROF
HIS_OPT_P
VUT_OPT_q
VUT_OPT_r
VUT_OPT_t
VSL_OPT_T
VUT_GLOBAL_OPT_V
