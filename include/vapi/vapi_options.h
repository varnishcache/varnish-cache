/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2013 Varnish Software AS
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

/* VSM options */

#define VSM_OPT_n							\
	VOPT("n:", "[-n name]", "Varnish instance name",		\
	    "Specify the name of the varnishd instance to get logs"	\
	    " from. If -n is not specified, the host name is used."	\
	)

#define VSM_OPT_N							\
	VOPT("N:", "[-N filename]", "VSM filename",			\
	    "Specify the filename of a stale VSM instance. When using"	\
	    " this option the abandonment checking is disabled."	\
	)

/* VSL options */

#define VSL_iI_PS							\
	"If a tag include option is the first of any tag selection"	\
	" options, all tags are first marked excluded."

#define VSL_OPT_a							\
	VOPT("a", "[-a]", "Append binary file output",			\
	    "When writing binary output to a file, append to it rather"	\
	    " than overwrite it."					\
	)

#define VSL_OPT_d \
	VOPT("d", "[-d]", "Process old log entries on startup",		\
	    "Start processing log records at the head of the log"	\
	    " instead of the tail."					\
	)

#define VSL_OPT_g							\
	VOPT("g:", "[-g <session|request|vxid|raw>]", "Grouping mode",	\
	    "The grouping of the log records. The default is to group"	\
	    " by request."						\
	)

#define VSL_OPT_i							\
	VOPT("i:", "[-i taglist]", "Include tags",			\
	    "Include log records of these tags in output. Taglist is"   \
	    " a comma-separated list of tag globs. Multiple -i"		\
	    " options may be given.\n"					\
	    "\n"							\
	    VSL_iI_PS							\
	)

#define VSL_OPT_I							\
	VOPT("I:", "[-I <[tag:]regex>]", "Include by regex",		\
	    "Include by regex matching. Output only records matching"	\
	    " tag and regular expression. Applies to any tag if tag"	\
	    " is * or empty.\n"						\
	    "\n"							\
	    VSL_iI_PS							\
	)

#define VSL_OPT_r							\
	VOPT("r:", "[-r filename]", "Binary file input",		\
	    "Read log in binary file format from this file."		\
	)

#define VSL_OPT_u							\
	VOPT("u", "[-u]", "Binary file output unbuffered",		\
	    "Unbuffered binary file output mode."			\
	)

#define VSL_OPT_v							\
	VOPT("v", "[-v]", "Verbose record printing",			\
	    "Use verbose output on record set printing, giving the"	\
	    " VXID on every log line. Without this option, the VXID"	\
	    " will only be given on the header of that transaction."	\
	)

#define VSL_OPT_w							\
	VOPT("w:", "[-w filename]", "Binary output filename",		\
	    "Write log entries to this file instead of displaying"	\
	    " them. The file will be overwritten unless the -a option"	\
	    " was specified. If the application receives a SIGHUP"	\
	    " while writing to a file, it will reopen the file"		\
	    " allowing the old one to be rotated away."			\
	)

#define VSL_OPT_x							\
	VOPT("x:", "[-x taglist]", "Exclude tags",			\
	    "Exclude log records of these tags in output. Taglist is"   \
	    " a comma-separated list of tag globs. Multiple -x"		\
	    " options may be given.\n"					\
	)

#define VSL_OPT_X							\
	VOPT("X:", "[-X <[tag:]regex>]", "Exclude by regex",		\
	    "Exclude by regex matching. Do not output records matching"	\
	    " tag and regular expression. Applies to any tag if tag"	\
	    " is * or empty."						\
	)
