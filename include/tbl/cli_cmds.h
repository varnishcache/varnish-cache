/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 * These macros define the common data for requests in the CLI protocol.
 * The fields are:
 *	const char *	upper-case C-ident request_name
 *	const char *	request_name
 *	const char *	request_syntax (for short help)
 *	const char *	request_help (for long help)
 *	const char *	documentation (for sphinx)
 *	int		minimum_arguments
 *	int		maximum_arguments
 */

/*lint -save -e525 -e539 */

CLI_CMD(BAN,
	"ban",
	"ban <field> <operator> <arg> [&& <field> <oper> <arg> ...]",
	"Mark obsolete all objects where all the conditions match.",
	"See :ref:`vcl(7)_ban` for details",
	3, -1
)

CLI_CMD(BAN_LIST,
	"ban.list",
	"ban.list [-j]",
	"List the active bans.",

	" Unless ``-j`` is specified (for JSON output), "
	" the output format is:\n\n"
	"  * Time the ban was issued.\n\n"
	"  * Objects referencing this ban.\n\n"
	"  * ``C`` if ban is completed = no further testing against it.\n\n"
	"  * if ``lurker`` debugging is enabled:\n\n"
	"    * ``R`` for req.* tests\n\n"
	"    * ``O`` for obj.* tests\n\n"
	"    * Pointer to ban object\n\n"
	"  * Ban specification",

	0, 0
)

CLI_CMD(VCL_LOAD,
	"vcl.load",
	"vcl.load <configname> <filename> [auto|cold|warm]",
	"Compile and load the VCL file under the name provided.",
	"",
	2, 3
)

CLI_CMD(VCL_INLINE,
	"vcl.inline",
	"vcl.inline <configname> <quoted_VCLstring> [auto|cold|warm]",
	"Compile and load the VCL data under the name provided.",

	"  Multi-line VCL can be input using the here document"
	" :ref:`ref_syntax`.",

	2, 3
)

CLI_CMD(VCL_STATE,
	"vcl.state",
	"vcl.state <configname> [auto|cold|warm]",
	"Force the state of the named configuration.",
	"",
	2, 2
)

CLI_CMD(VCL_DISCARD,
	"vcl.discard",
	"vcl.discard <configname|label>",
	"Unload the named configuration (when possible).",
	"",
	1, 1
)

CLI_CMD(VCL_LIST,
	"vcl.list",
	"vcl.list [-j]",
	"List all loaded configuration.",
	"``-j`` specifies JSON output.",
	0, 0
)

CLI_CMD(VCL_SHOW,
	"vcl.show",
	"vcl.show [-v] <configname>",
	"Display the source code for the specified configuration.",
	"",
	1, 2
)

CLI_CMD(VCL_USE,
	"vcl.use",
	"vcl.use <configname|label>",
	"Switch to the named configuration immediately.",
	"",
	1, 1
)

CLI_CMD(VCL_LABEL,
	"vcl.label",
	"vcl.label <label> <configname>",
	"Apply label to configuration.",
	"",
	2, 2
)

CLI_CMD(PARAM_RESET,
	"param.reset",
	"param.reset <param>",
	"Reset parameter to default value.",
	"",
	1,1
)

CLI_CMD(PARAM_SHOW,
	"param.show",
	"param.show [-l|-j] [<param>|changed]",
	"Show parameters and their values.",

	"The long form with ``-l`` shows additional information, including"
	" documentation and minimum, maximum and default values, if defined"
	" for the parameter. JSON output is specified with ``-j``, in which"
	" the information for the long form is included; only one of ``-l`` or"
	" ``-j`` is permitted. If a parameter is specified with ``<param>``,"
	" show only that parameter. If ``changed`` is specified, show only"
	" those parameters whose values differ from their defaults.",
	0, 2
)

CLI_CMD(PARAM_SET,
	"param.set",
	"param.set <param> <value>",
	"Set parameter value.",
	"",
	2,2
)

CLI_CMD(SERVER_STOP,
	"stop",
	"stop",
	"Stop the Varnish cache process.",
	"",
	0, 0
)

CLI_CMD(SERVER_START,
	"start",
	"start",
	"Start the Varnish cache process.",
	"",
	0, 0
)

CLI_CMD(PING,
	"ping",
	"ping [-j] [<timestamp>]",
	"Keep connection alive.",
	"The response is formatted as JSON if ``-j`` is specified.",
	0, 1
)

CLI_CMD(HELP,
	"help",
	"help [-j] [<command>]",
	"Show command/protocol help.",
	"``-j`` specifies JSON output.",
	0, 1
)

CLI_CMD(QUIT,
	"quit",
	"quit",
	"Close connection.",
	"",
	0, 0
)

CLI_CMD(SERVER_STATUS,
	"status",
	"status [-j]",
	"Check status of Varnish cache process.",
	"``-j`` specifies JSON output.",
	0, 0
)

CLI_CMD(BANNER,
	"banner",
	"banner",
	"Print welcome banner.",
	"",
	0, 0
)

CLI_CMD(AUTH,
	"auth",
	"auth <response>",
	"Authenticate.",
	"",
	1, 1
)

CLI_CMD(PANIC_SHOW,
	"panic.show",
	"panic.show [-j]",
	"Return the last panic, if any.",
	"``-j`` specifies JSON output -- the panic message is returned as an"
	" unstructured JSON string.",
	0, 0
)

CLI_CMD(PANIC_CLEAR,
	"panic.clear",
	"panic.clear [-z]",
	"Clear the last panic, if any,"
	" -z will clear related varnishstat counter(s)",
	"",
	0, 1
)

CLI_CMD(DEBUG_LISTEN_ADDRESS,
	"debug.listen_address",
	"debug.listen_address",
	"Report the actual listen address.",
	"",
	0, 0
)

CLI_CMD(BACKEND_LIST,
	"backend.list",
	"backend.list [-j] [-p] [<backend_pattern>]",
	"List backends.  -p also shows probe status.",
	"``-j`` specifies JSON output.",
	0, 2
)

CLI_CMD(BACKEND_SET_HEALTH,
	"backend.set_health",
	"backend.set_health <backend_pattern> [auto|healthy|sick]",
	"Set health status on the backends.",
	"",
	2, 2
)

CLI_CMD(DEBUG_FRAGFETCH,
	"debug.fragfetch",
	"debug.fragfetch",
	"Enable fetch fragmentation.",
	"",
	1, 1
)

CLI_CMD(DEBUG_REQPOOLFAIL,
	"debug.reqpool.fail",
	"debug.reqpool.fail",
	"Schedule req-pool failures.",
	"The argument is read L-R and 'f' means fail:\n\n"
	"\tparam.set debug.reqpoolfail F__F\n\n"
	"Means that the frist and the third attempted allocation will fail",
	1, 1
)

CLI_CMD(DEBUG_XID,
	"debug.xid",
	"debug.xid",
	"Examine or set XID.",
	"",
	0, 1
)

CLI_CMD(DEBUG_SRANDOM,
	"debug.srandom",
	"debug.srandom",
	"Seed the random(3) function.",
	"",
	0, 1
)

CLI_CMD(DEBUG_PANIC_WORKER,
	"debug.panic.worker",
	"debug.panic.worker",
	"Panic the worker process.",
	"",
	0, 0
)

CLI_CMD(DEBUG_PANIC_MASTER,
	"debug.panic.master",
	"debug.panic.master",
	"Panic the master process.",
	"",
	0, 0
)

CLI_CMD(DEBUG_VMOD,
	"debug.vmod",
	"debug.vmod",
	"Show loaded vmods.",
	"",
	0, 0
)

CLI_CMD(DEBUG_PERSISTENT,
	"debug.persistent",
	"debug.persistent [<stevedore>] [<cmd>]",
	"Persistent debugging magic:\n"
	"With no cmd arg, a summary of the silo is returned.\n"
	"Possible commands:\n"
	"\tsync\tClose current segment, open a new one\n"
	"\tdump\tinclude objcores in silo summary",
	"",
	0, 2
)

CLI_CMD(STORAGE_LIST,
	"storage.list",
	"storage.list [-j]",
	"List storage devices.",
	"``-j`` specifies JSON output.",
	0, 0
)

CLI_CMD(PID,
	"pid",
	"pid [-j]",
	"Show the pid of the master process, and the worker if it's running.",
	"  ``-j`` specifies JSON output.",
	0, 0
)

#undef CLI_CMD

/*lint -restore */
