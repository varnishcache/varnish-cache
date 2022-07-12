/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
	"  See :ref:`vcl(7)_ban` for details",
	3, -1
)

CLI_CMD(BAN_LIST,
	"ban.list",
	"ban.list [-j]",
	"List the active bans.",

	"  Unless ``-j`` is specified for JSON output, "
	" the output format is:\n\n"
	"  * Time the ban was issued.\n\n"
	"  * Objects referencing this ban.\n\n"
	"  * ``C`` if ban is completed = no further testing against it.\n\n"
	"  * if ``lurker`` debugging is enabled:\n\n"
	"    * ``R`` for req.* tests\n\n"
	"    * ``O`` for obj.* tests\n\n"
	"    * Pointer to ban object\n\n"
	"  * Ban specification\n\n"
	"  Durations of ban specifications get normalized, for example \"7d\""
	" gets changed into \"1w\".",

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
	"vcl.state <configname> auto|cold|warm",
	"  Force the state of the named configuration.",
	"",
	2, 2
)

CLI_CMD(VCL_DISCARD,
	"vcl.discard",
	"vcl.discard <name_pattern>...",
	"Unload the named configurations (when possible).",
	"  Unload the named configurations and labels matching at least"
	" one name pattern. All matching configurations and labels"
	" are discarded in the correct order with respect to potential"
	" dependencies. If one configuration or label could not be"
	" discarded because one of its dependencies would remain,"
	" nothing is discarded."
	" Each individual name pattern must match at least one named"
	" configuration or label.",
	1, -1
)

CLI_CMD(VCL_LIST,
	"vcl.list",
	"vcl.list [-j]",
	"List all loaded configuration.",
	"  Unless ``-j`` is specified for JSON output, "
	" the output format is five or seven columns of dynamic width, "
	" separated by white space with the fields:\n\n"
	"  * status: active, available or discarded\n\n"
	"  * state: label, cold, warm, or auto\n\n"
	"  * temperature: init, cold, warm, busy or cooling\n\n"
	"  * busy: number of references to this vcl (integer)\n\n"
	"  * name: the name given to this vcl or label\n\n"
	"  * [ ``<-`` | ``->`` ] and label info last two fields)\n\n"
	"    * ``->`` <vcl> : label \"points to\" the named <vcl>\n\n"
	"    * ``<-`` (<n> label[s]): the vcl has <n> label(s)\n\n",
	0, 0
)

CLI_CMD(VCL_DEPS,
	"vcl.deps",
	"vcl.deps [-j]",
	"List all loaded configuration and their dependencies.",
	"  Unless ``-j`` is specified for JSON output, the"
	" output format is up to two columns of dynamic width"
	" separated by white space with the fields:\n\n"
	"  * VCL: a VCL program\n\n"
	"  * Dependency: another VCL program it depends on\n\n"
	"  Only direct dependencies are listed, and VCLs with"
	" multiple dependencies are listed multiple times.",
	0, 0
)

CLI_CMD(VCL_SHOW,
	"vcl.show",
	"vcl.show [-v] [<configname>]",
	"Display the source code for the specified configuration.",
	"",
	0, 2
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
	"  A VCL label is like a UNIX symbolic link, "
	" a name without substance, which points to another VCL.\n\n"
	"  Labels are mandatory whenever one VCL references another.",
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

	"  The long form with ``-l`` shows additional information, including"
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
	"param.set [-j] <param> <value>",
	"Set parameter value.",
	"  The JSON output is the same as ``param.show -j <param>`` and"
	" contains the updated value as it would be represented by a"
	" subsequent execution of ``param.show``.\n\n"
	"  This can be useful to later verify that a parameter value didn't"
	" change and to use the value from the JSON output to reset the"
	" parameter to the desired value.",
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
	"  The response is formatted as JSON if ``-j`` is specified.",
	0, 1
)

CLI_CMD(HELP,
	"help",
	"help [-j|<command>]",
	"Show command/protocol help.",
	"  ``-j`` specifies JSON output.",
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
	"  ``-j`` specifies JSON output.",
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
	"  ``-j`` specifies JSON output -- the panic message is returned as an"
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
	"List backends.\n",
	"  ``-p`` also shows probe status.\n\n"
	"  ``-j`` specifies JSON output.\n\n"
	"  Unless ``-j`` is specified for JSON output, "
	" the output format is five columns of dynamic width, "
	" separated by white space with the fields:\n\n"
	"  * Backend name\n\n"
	"  * Admin: How health state is determined:\n\n"
	"    * ``healthy``: Set ``healthy`` through ``backend.set_health``.\n\n"
	"    * ``sick``: Set ``sick`` through ``backend.set_health``.\n\n"
	"    * ``probe``: Health state determined by a probe or some other\n"
	"      dynamic mechanism.\n\n"
	"    * ``deleted``: Backend has been deleted, but not yet cleaned\n"
	"      up.\n\n"
	"    Admin has precedence over Health\n\n"
	"  * Probe ``X/Y``: *X* out of *Y* checks have succeeded\n\n"
	"    *X* and *Y* are backend specific and may represent probe checks,\n"
	"    other backends or any other metric.\n\n"
	"    If there is no probe or the director does not provide details on\n"
	"    probe check results, ``0/0`` is output.\n\n"
	"  * Health: Probe health state\n\n"
	"    * ``healthy``\n\n"
	"    * ``sick``\n\n"
	"    If there is no probe, ``healthy`` is output.\n"
	"  * Last change: Timestamp when the health state last changed.\n\n"
	"  The health state reported here is generic. A backend's health "
	"may also depend on the context it is being used in (e.g. "
	"the object's hash), so the actual health state as visible "
	"from VCL (e.g. using ``std.healthy()``) may differ.\n\n"
	"  For ``-j``, the object members should be self explanatory,\n"
	"  matching the fields described above. ``probe_message`` has the\n"
	"  format ``[X, Y, \"state\"]`` as described above for Probe. JSON\n"
	"  Probe details (``-j -p`` arguments) are director specific.",
	0, 2
)

CLI_CMD(BACKEND_SET_HEALTH,
	"backend.set_health",
	"backend.set_health <backend_pattern> [auto|healthy|sick]",
	"Set health status of backend(s) matching <backend_pattern>.",
	"  * With ``auto``, the health status is determined by a probe\n"
	"    or some other dynamic mechanism, if any\n"
	"  * ``healthy`` sets the backend as usable\n"
	"  * ``sick`` sets the backend as unsable\n",
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
	"\tparam.set debug.reqpoolfail F__F_F\n\n"
	"In the example above the first, fourth and sixth attempted\n"
	"allocations will fail.",
	1, 1
)

CLI_CMD(DEBUG_SHUTDOWN_DELAY,
	"debug.shutdown.delay",
	"debug.shutdown.delay",
	"Add a delay to the child process shutdown.",
	"",
	1, 1
)

CLI_CMD(DEBUG_XID,
	"debug.xid",
	"debug.xid [<xid> [<cachesize>]]",
	"Examine or set XID. <cachesize> defaults to 1.",
	"",
	0, 2
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

CLI_CMD(DEBUG_VCL_SYMTAB,
	"vcl.symtab",
	"vcl.symtab",
	"Dump the VCL symbol-tables.",
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
	"  ``-j`` specifies JSON output.",
	0, 0
)

CLI_CMD(PID,
	"pid",
	"pid [-j]",
	"Show the pid of the master process, and the worker if it's running.",
	"  ``-j`` specifies JSON output.",
	0, 0
)

CLI_CMD(TRAFFIC_ACCEPT,
    "traffic.accept",
    "traffic.accept",
    "Accept new client connections and requests.",
    "Accepting client traffic is the normal mode of operations. Listen "
    "addresses must all be available to succeed, which may not be the "
    "case after a traffic.refuse command until all ongoing connections "
    "are closed.",
    0, 0
)

CLI_CMD(TRAFFIC_REFUSE,
    "traffic.refuse",
    "traffic.refuse",
    "Refuse new client connections and requests.",
    "When a Varnish instance is taken offline, for example to be removed "
    "from a cluster, new traffic can be refused without affecting ongoing "
    "transactions.\n\n"
    "Listen sockets are closed and it is no longer possible to establish "
    "new connections for clients. This means that traffic.accept may fail "
    "to bind listen addresses again, if meanwhile they end up already in "
    "use.\n\n"
    "Refusing new traffic also implies refusing new requests for exsiting "
    "connections, disabling HTTP/1 keep-alive. Responses initiated after "
    "client traffic started being refused will have a 'Connection: close' "
    "header. If a request is received on a keep-alive session while traffic "
    "is being refused, it results in a minimal 503 response.\n\n"
    "For h2 traffic, a GOAWAY frame is sent to clients to notify them that "
    "ongoing streams can complete, but new streams will be refused.",
    0, 0
)

CLI_CMD(TRAFFIC_STATUS,
    "traffic.status",
    "traffic.status [-j]",
    "Check the status for new client connections and requests.",
    "",
    0, 0
)

#undef CLI_CMD

/*lint -restore */
