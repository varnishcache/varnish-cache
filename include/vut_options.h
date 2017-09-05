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

/* VUT options */

#define VUT_GLOBAL_OPT_D						\
	VOPT("D", "[-D]", "Daemonize",					\
	    "Daemonize."						\
	)

#define VUT_GLOBAL_OPT_P						\
	VOPT("P:", "[-P <file>]", "PID file",				\
		"Write the process' PID to the specified file."		\
	)

#define VUT_GLOBAL_OPT_V						\
	VOPT("V", "[-V]", "Version",					\
	    "Print version information and exit."			\
	)

#define VUT_OPT_d							\
	VOPT("d", "[-d]", "Process old log entries and exit",		\
	    "Process log records at the head of the log and exit."	\
	)

#define VUT_OPT_g							\
	VOPT("g:", "[-g <session|request|vxid|raw>]", "Grouping mode (default: vxid)",	\
	    "The grouping of the log records. The default is to group"	\
	    " by vxid."							\
	)

#define VUT_OPT_h							\
	VOPT("h", "[-h]", "Usage help",					\
	    "Print program usage and exit"				\
	)

#define VUT_OPT_k							\
	VOPT("k:", "[-k <num>]", "Limit transactions",			\
	    "Process this number of matching log transactions before"	\
	    " exiting."							\
	)

#define VUT_OPT_n							\
	VOPT("n:", "[-n <dir>]", "varnishd working directory",		\
	    "Specify the varnishd working directory (also known as"	\
	    " instance name) to get logs from. If -n is not specified,"	\
	    " the host name is used."					\
	)

#define VUT_OPT_q							\
	VOPT("q:", "[-q <query>]", "VSL query",				\
		"Specifies the VSL query to use."			\
	)

#define VUT_OPT_r							\
	VOPT("r:", "[-r <filename>]", "Binary file input",		\
	    "Read log in binary file format from this file. The file"	\
	    " can be created with ``varnishlog -w filename``."		\
	)

#define VUT_OPT_t							\
	VOPT("t:", "[-t <seconds|off>]", "VSM connection timeout",	\
	    "Timeout before returning error on initial VSM connection."	\
	    " If set the VSM connection is retried every 0.5 seconds"	\
	    " for this many seconds. If zero the connection is"		\
	    " attempted only once and will fail immediately if"		\
	    " unsuccessful. If set to \"off\", the connection will not"	\
	    " fail, allowing the utility to start and wait"		\
	    " indefinetely for the Varnish instance to appear. "	\
	    " Defaults to 5 seconds."					\
	)
