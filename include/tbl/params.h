/*-
 * Copyright (c) 2015 Varnish Software AS
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
 * PARAM(nm, ty, mi, ma, de, un, fl, st, lt, fn)
 */

/*lint -save -e525 -e539 */

#ifdef HAVE_ACCEPT_FILTERS
PARAM(
	/* name */	accept_filter,
	/* typ */	bool,
	/* min */	NULL,
	/* max */	NULL,
	/* default */	"on",
	/* units */	"bool",
	/* flags */	MUST_RESTART,
	/* s-text */
	"Enable kernel accept-filters, (if available in the kernel).",
	/* l-text */	NULL,
	/* func */	NULL
)
#endif /* HAVE_ACCEPT_FILTERS */

PARAM(
	/* name */	acceptor_sleep_decay,
	/* typ */	double,
	/* min */	"0",
	/* max */	"1",
	/* default */	"0.9",
	/* units */	NULL,
	/* flags */	EXPERIMENTAL,
	/* s-text */
	"If we run out of resources, such as file descriptors or worker "
	"threads, the acceptor will sleep between accepts.\n"
	"This parameter (multiplicatively) reduce the sleep duration for "
	"each successful accept. (ie: 0.9 = reduce by 10%)",
	/* l-text */	"",
	/* func */	NULL
)

PARAM(
	/* name */	acceptor_sleep_incr,
	/* typ */	timeout,
	/* min */	"0",
	/* max */	"1",
	/* default */	"0",
	/* units */	"seconds",
	/* flags */	EXPERIMENTAL,
	/* s-text */
	"If we run out of resources, such as file descriptors or worker "
	"threads, the acceptor will sleep between accepts.\n"
	"This parameter control how much longer we sleep, each time we "
	"fail to accept a new connection.",
	/* l-text */	"",
	/* func */	NULL
)

PARAM(
	/* name */	acceptor_sleep_max,
	/* typ */	timeout,
	/* min */	"0",
	/* max */	"10",
	/* default */	"0.05",
	/* units */	"seconds",
	/* flags */	EXPERIMENTAL,
	/* s-text */
	"If we run out of resources, such as file descriptors or worker "
	"threads, the acceptor will sleep between accepts.\n"
	"This parameter limits how long it can sleep between attempts to "
	"accept new connections.",
	/* l-text */	"",
	/* func */	NULL
)

PARAM(
	/* name */	auto_restart,
	/* typ */	bool,
	/* min */	NULL,
	/* max */	NULL,
	/* default */	"on",
	/* units */	"bool",
	/* flags */	0,
	/* s-text */
	"Automatically restart the child/worker process if it dies.",
	/* l-text */	"",
	/* func */	NULL
)

PARAM(
	/* name */	ban_dups,
	/* typ */	bool,
	/* min */	NULL,
	/* max */	NULL,
	/* default */	"on",
	/* units */	"bool",
	/* flags */	0,
	/* s-text */
	"Eliminate older identical bans when a new ban is added.  This saves "
	"CPU cycles by not comparing objects to identical bans.\n"
	"This is a waste of time if you have many bans which are never "
	"identical.",
	/* l-text */	"",
	/* func */	NULL
)

PARAM(
	/* name */	ban_lurker_age,
	/* tweak */	timeout,
	/* min */	"0",
	/* max */	NULL,
	/* default */	"60",
	/* units */	"seconds",
	/* flags */	0,
	/* s-text */
	"The ban lurker only process bans when they are this old.  "
	"When a ban is added, the most frequently hit objects will "
	"get tested against it as part of object lookup.  This parameter "
	"prevents the ban-lurker from kicking in, until the rush is over.",
	/* l-text */	"",
	/* func */	NULL
)

PARAM(
	/* name */	ban_lurker_batch,
	/* tweak */	uint,
	/* min */	"1",
	/* max */	NULL,
	/* default */	"1000",
	/* units */	NULL,
	/* flags */	0,
	/* s-text */
	"The ban lurker slees ${ban_lurker_sleep} after examining this "
	"many objects.  Use this to pace the ban-lurker if it eats too "
	"many resources.",
	/* l-text */	"",
	/* func */	NULL
)

PARAM(
	/* name */	ban_lurker_sleep,
	/* tweak */	timeout,
	/* min */	"0",
	/* max */	NULL,
	/* default */	"0.010",
	/* units */	"seconds",
	/* flags */	0,
	/* s-text */
	"How long the ban lurker sleeps after examining ${ban_lurker_batch} "
	"objects.\n"
	"A value of zero will disable the ban lurker entirely.",
	/* l-text */	"",
	/* func */	NULL
)

PARAM(
	/* name */	first_byte_timeout,
	/* tweak */	timeout,
	/* min */	"0",
	/* max */	NULL,
	/* default */	"60",
	/* units */	"seconds",
	/* flags */	0,
	/* s-text */
	"Default timeout for receiving first byte from backend. We only "
	"wait for this many seconds for the first byte before giving up. A "
	"value of 0 means it will never time out. VCL can override this "
	"default value for each backend and backend request. This "
	"parameter does not apply to pipe.",
	/* l-text */	"",
	/* func */	NULL
)

PARAM(
	/* name */	between_bytes_timeout,
	/* tweak */	timeout,
	/* min */	"0",
	/* max */	NULL,
	/* default */	"60",
	/* units */	"seconds",
	/* flags */	0,
	/* s-text */
	"We only wait for this many seconds between bytes received from "
	"the backend before giving up the fetch.\n"
	"A value of zero means never give up.\n"
	"VCL values, per backend or per backend request take precedence.\n"
	"This parameter does not apply to pipe'ed requests.",
	/* l-text */	"",
	/* func */	NULL
)

PARAM(
	/* name */	backend_idle_timeout,
	/* tweak */	timeout,
	/* min */	"1",
	/* max */	NULL,
	/* default */	"60",
	/* units */	"seconds",
	/* flags */	0,
	/* s-text */
	"Timeout before we close unused backend connections.",
	/* l-text */	"",
	/* func */	NULL
)

/**********************************************************************/
#if 0 /* NOT YET */

PARAM(
	/* name */	cli_buffer,
	/* tweak */	tweak_bytes_u,
	/* var */	cli_buffer,
	/* min */	4k,
	/* max */	none,
	/* default */	8k,
	/* units */	bytes,
	/* flags */	00,
	/* s-text */
	"Size of buffer for CLI command input.\n"
	"You may need to increase this if you have big VCL files and use "
	"the vcl.inline CLI command.\n"
	"NB: Must be specified with -p to have effect.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	cli_limit,
	/* tweak */	tweak_bytes_u,
	/* var */	cli_limit,
	/* min */	128b,
	/* max */	99999999b,
	/* default */	48k,
	/* units */	bytes,
	/* flags */	00,
	/* s-text */
	"Maximum size of CLI response.  If the response exceeds this "
	"limit, the response code will be 201 instead of 200 and the last "
	"line will indicate the truncation.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	cli_timeout,
	/* tweak */	tweak_timeout,
	/* var */	cli_timeout,
	/* min */	0.000,
	/* max */	none,
	/* default */	60.000,
	/* units */	seconds,
	/* flags */	00,
	/* s-text */
	"Timeout for the childs replies to CLI requests from the "
	"mgt_param.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	clock_skew,
	/* tweak */	tweak_uint,
	/* var */	clock_skew,
	/* min */	0,
	/* max */	none,
	/* default */	10,
	/* units */	seconds,
	/* flags */	00,
	/* s-text */
	"How much clockskew we are willing to accept between the backend "
	"and our own clock.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	connect_timeout,
	/* tweak */	tweak_timeout,
	/* var */	connect_timeout,
	/* min */	0.000,
	/* max */	none,
	/* default */	3.500,
	/* units */	seconds,
	/* flags */	00,
	/* s-text */
	"Default connection timeout for backend connections. We only try "
	"to connect to the backend for this many seconds before giving up. "
	"VCL can override this default value for each backend and backend "
	"request.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	critbit_cooloff,
	/* tweak */	tweak_timeout,
	/* var */	critbit_cooloff,
	/* min */	60.000,
	/* max */	254.000,
	/* default */	180.000,
	/* units */	seconds,
	/* flags */	0| WIZARD,
	/* s-text */
	"How long the critbit hasher keeps deleted objheads on the cooloff "
	"list.\n",
	/* l-text */	"",
	/* func */	NULL
)
#if 0
PARAM(
	/* name */	debug,
	/* tweak */	tweak_mask,
	/* var */	debug,
	/* min */	none,
	/* max */	none,
	/* default */	none,
	/* units */	,
	/* flags */	0,
	/* s-text */
	"Enable/Disable various kinds of debugging.\n"
	"	none	Disable all debugging\n"
	"\n"
	"Use +/- prefix to set/reset individual bits:\n"
	"	req_state	VSL Request state engine\n"
	"	workspace	VSL Workspace operations\n"
	"	waiter	VSL Waiter internals\n"
	"	waitinglist	VSL Waitinglist events\n"
	"	syncvsl	Make VSL synchronous\n"
	"	hashedge	Edge cases in Hash\n"
	"	vclrel	Rapid VCL release\n"
	"	lurker	VSL Ban lurker\n"
	"	esi_chop	Chop ESI fetch to bits\n"
	"	flush_head	Flush after http1 head\n"
	"	vtc_mode	Varnishtest Mode\n",
	/* l-text */	"",
	/* func */	NULL
)
#endif
PARAM(
	/* name */	default_grace,
	/* tweak */	tweak_timeout,
	/* var */	default_grace,
	/* min */	0.000,
	/* max */	none,
	/* default */	10.000,
	/* units */	seconds,
	/* flags */	0,
	/* s-text */
	"Default grace period.  We will deliver an object this long after "
	"it has expired, provided another thread is attempting to get a "
	"new copy.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	default_keep,
	/* tweak */	tweak_timeout,
	/* var */	default_keep,
	/* min */	0.000,
	/* max */	none,
	/* default */	0.000,
	/* units */	seconds,
	/* flags */	0,
	/* s-text */
	"Default keep period.  We will keep a useless object around this "
	"long, making it available for conditional backend fetches.  That "
	"means that the object will be removed from the cache at the end "
	"of ttl+grace+keep.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	default_ttl,
	/* tweak */	tweak_timeout,
	/* var */	default_ttl,
	/* min */	0.000,
	/* max */	none,
	/* default */	120.000,
	/* units */	seconds,
	/* flags */	0,
	/* s-text */
	"The TTL assigned to objects if neither the backend nor the VCL "
	"code assigns one.\n",
	/* l-text */	"",
	/* func */	NULL
)
#if 0
PARAM(
	/* name */	feature,
	/* tweak */	tweak_mask
	/* var */	feature,
	/* min */	none,
	/* max */	none,
	/* default */	none,
	/* units */	,
	/* flags */	00,
	/* s-text */
	"Enable/Disable various minor features.\n"
	"	none	Disable all features.\n"
	"\n"
	"Use +/- prefix to enable/disable individual feature:\n"
	"	short_panic	Short panic message.\n"
	"	wait_silo	Wait for persistent silo.\n"
	"	no_coredump	No coredumps.\n"
	"	esi_ignore_https	Treat HTTPS as HTTP in ESI:includes\n"
	"	esi_disable_xml_check	Don't check of body looks like XML\n"
	"	esi_ignore_other_elements	Ignore non-esi XML-elements\n"
	"	esi_remove_bom	Remove UTF-8 BOM\n",
	/* l-text */	"",
	/* func */	NULL
)
#endif
PARAM(
	/* name */	fetch_chunksize,
	/* tweak */	tweak_bytes,
	/* var */	fetch_chunksize,
	/* min */	4k,
	/* max */	none,
	/* default */	16k,
	/* units */	bytes,
	/* flags */	0| EXPERIMENTAL,
	/* s-text */
	"The default chunksize used by fetcher. This should be bigger than "
	"the majority of objects with short TTLs.\n"
	"Internal limits in the storage_file module makes increases above "
	"128kb a dubious idea.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	fetch_maxchunksize,
	/* tweak */	tweak_bytes,
	/* var */	fetch_maxchunksize,
	/* min */	64k,
	/* max */	none,
	/* default */	0.25G,
	/* units */	bytes,
	/* flags */	0| EXPERIMENTAL,
	/* s-text */
	"The maximum chunksize we attempt to allocate from storage. Making "
	"this too large may cause delays and storage fragmentation.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	gzip_buffer,
	/* tweak */	tweak_bytes_u,
	/* var */	gzip_buffer,
	/* min */	2k,
	/* max */	none,
	/* default */	32k,
	/* units */	bytes,
	/* flags */	0| EXPERIMENTAL,
	/* s-text */
	"Size of malloc buffer used for gzip processing.\n"
	"These buffers are used for in-transit data, for instance "
	"gunzip'ed data being sent to a client.Making this space to small "
	"results in more overhead, writes to sockets etc, making it too "
	"big is probably just a waste of memory.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	gzip_level,
	/* tweak */	tweak_uint,
	/* var */	gzip_level,
	/* min */	0,
	/* max */	9,
	/* default */	6,
	/* units */	,
	/* flags */	00,
	/* s-text */
	"Gzip compression level: 0=debug, 1=fast, 9=best\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	gzip_memlevel,
	/* tweak */	tweak_uint,
	/* var */	gzip_memlevel,
	/* min */	1,
	/* max */	9,
	/* default */	8,
	/* units */	,
	/* flags */	00,
	/* s-text */
	"Gzip memory level 1=slow/least, 9=fast/most compression.\n"
	"Memory impact is 1=1k, 2=2k, ... 9=256k.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	http_gzip_support,
	/* tweak */	tweak_bool,
	/* var */	http_gzip_support,
	/* min */	none,
	/* max */	none,
	/* default */	on,
	/* units */	bool,
	/* flags */	00,
	/* s-text */
	"Enable gzip support. When enabled Varnish request compressed "
	"objects from the backend and store them compressed. If a client "
	"does not support gzip encoding Varnish will uncompress compressed "
	"objects on demand. Varnish will also rewrite the Accept-Encoding "
	"header of clients indicating support for gzip to:\n"
	"  Accept-Encoding: gzip\n"
	"\n"
	"Clients that do not support gzip will have their Accept-Encoding "
	"header removed. For more information on how gzip is implemented "
	"please see the chapter on gzip in the Varnish reference.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	http_max_hdr,
	/* tweak */	tweak_uint,
	/* var */	http_max_hdr,
	/* min */	32,
	/* max */	65535,
	/* default */	64,
	/* units */	header lines,
	/* flags */	00,
	/* s-text */
	"Maximum number of HTTP header lines we allow in "
	"{req|resp|bereq|beresp}.http (obj.http is autosized to the exact "
	"number of headers).\n"
	"Cheap, ~20 bytes, in terms of workspace memory.\n"
	"Note that the first line occupies five header lines.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	http_range_support,
	/* tweak */	tweak_bool,
	/* var */	http_range_support,
	/* min */	none,
	/* max */	none,
	/* default */	on,
	/* units */	bool,
	/* flags */	00,
	/* s-text */
	"Enable support for HTTP Range headers.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	http_req_hdr_len,
	/* tweak */	tweak_bytes_u,
	/* var */	http_req_hdr_len,
	/* min */	40b,
	/* max */	none,
	/* default */	8k,
	/* units */	bytes,
	/* flags */	00,
	/* s-text */
	"Maximum length of any HTTP client request header we will allow.  "
	"The limit is inclusive its continuation lines.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	http_req_size,
	/* tweak */	tweak_bytes_u,
	/* var */	http_req_size,
	/* min */	0.25k,
	/* max */	none,
	/* default */	32k,
	/* units */	bytes,
	/* flags */	00,
	/* s-text */
	"Maximum number of bytes of HTTP client request we will deal with. "
	" This is a limit on all bytes up to the double blank line which "
	"ends the HTTP request.\n"
	"The memory for the request is allocated from the client workspace "
	"(param: workspace_client) and this parameter limits how much of "
	"that the request is allowed to take up.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	http_resp_hdr_len,
	/* tweak */	tweak_bytes_u,
	/* var */	http_resp_hdr_len,
	/* min */	40b,
	/* max */	none,
	/* default */	8k,
	/* units */	bytes,
	/* flags */	00,
	/* s-text */
	"Maximum length of any HTTP backend response header we will allow. "
	" The limit is inclusive its continuation lines.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	http_resp_size,
	/* tweak */	tweak_bytes_u,
	/* var */	http_resp_size,
	/* min */	0.25k,
	/* max */	none,
	/* default */	32k,
	/* units */	bytes,
	/* flags */	00,
	/* s-text */
	"Maximum number of bytes of HTTP backend response we will deal "
	"with.  This is a limit on all bytes up to the double blank line "
	"which ends the HTTP request.\n"
	"The memory for the request is allocated from the worker workspace "
	"(param: thread_pool_workspace) and this parameter limits how much "
	"of that the request is allowed to take up.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	idle_send_timeout,
	/* tweak */	tweak_timeout,
	/* var */	idle_send_timeout,
	/* min */	0.000,
	/* max */	none,
	/* default */	60.000,
	/* units */	seconds,
	/* flags */	0| DELAYED_EFFECT,
	/* s-text */
	"Time to wait with no data sent. If no data has been transmitted "
	"in this many\n"
	"seconds the session is closed.\n"
	"See setsockopt(2) under SO_SNDTIMEO for more information.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	listen_depth,
	/* tweak */	tweak_uint,
	/* var */	listen_depth,
	/* min */	0,
	/* max */	none,
	/* default */	1024,
	/* units */	connections,
	/* flags */	0| MUST_RESTART,
	/* s-text */
	"Listen queue depth.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	lru_interval,
	/* tweak */	tweak_timeout,
	/* var */	lru_interval,
	/* min */	0.000,
	/* max */	none,
	/* default */	2.000,
	/* units */	seconds,
	/* flags */	0| EXPERIMENTAL,
	/* s-text */
	"Grace period before object moves on LRU list.\n"
	"Objects are only moved to the front of the LRU list if they have "
	"not been moved there already inside this timeout period.  This "
	"reduces the amount of lock operations necessary for LRU list "
	"access.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	max_esi_depth,
	/* tweak */	tweak_uint,
	/* var */	max_esi_depth,
	/* min */	0,
	/* max */	none,
	/* default */	5,
	/* units */	levels,
	/* flags */	00,
	/* s-text */
	"Maximum depth of esi:include processing.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	max_restarts,
	/* tweak */	tweak_uint,
	/* var */	max_restarts,
	/* min */	0,
	/* max */	none,
	/* default */	4,
	/* units */	restarts,
	/* flags */	00,
	/* s-text */
	"Upper limit on how many times a request can restart.\n"
	"Be aware that restarts are likely to cause a hit against the "
	"backend, so don't increase thoughtlessly.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	max_retries,
	/* tweak */	tweak_uint,
	/* var */	max_retries,
	/* min */	0,
	/* max */	none,
	/* default */	4,
	/* units */	retries,
	/* flags */	00,
	/* s-text */
	"Upper limit on how many times a backend fetch can retry.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	nuke_limit,
	/* tweak */	tweak_uint,
	/* var */	nuke_limit,
	/* min */	0,
	/* max */	none,
	/* default */	50,
	/* units */	allocations,
	/* flags */	0| EXPERIMENTAL,
	/* s-text */
	"Maximum number of objects we attempt to nuke in orderto make "
	"space for a object body.\n",
	/* l-text */	"",
	/* func */	NULL
)
#if 0
PARAM(
	/* name */	pcre_match_limit,
	/* tweak */	tweak_uint,
	/* var */	pcre_match_limit,
	/* min */	1,
	/* max */	none,
	/* default */	10000,
	/* units */	,
	/* flags */	00,
	/* s-text */
	"The limit for the  number of internal matching function calls in "
	"a pcre_exec() execution.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	pcre_match_limit_recursion,
	/* tweak */	tweak_uint,
	/* var */	pcre_match_limit_recursion,
	/* min */	1,
	/* max */	none,
	/* default */	10000,
	/* units */	,
	/* flags */	00,
	/* s-text */
	"The limit for the  number of internal matching function "
	"recursions in a pcre_exec() execution.\n",
	/* l-text */	"",
	/* func */	NULL
)
#endif
PARAM(
	/* name */	ping_interval,
	/* tweak */	tweak_uint,
	/* var */	ping_interval,
	/* min */	0,
	/* max */	none,
	/* default */	3,
	/* units */	seconds,
	/* flags */	0| MUST_RESTART,
	/* s-text */
	"Interval between pings from parent to child.\n"
	"Zero will disable pinging entirely, which makes it possible to "
	"attach a debugger to the child.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	pipe_timeout,
	/* tweak */	tweak_timeout,
	/* var */	pipe_timeout,
	/* min */	0.000,
	/* max */	none,
	/* default */	60.000,
	/* units */	seconds,
	/* flags */	00,
	/* s-text */
	"Idle timeout for PIPE sessions. If nothing have been received in "
	"either direction for this many seconds, the session is closed.\n",
	/* l-text */	"",
	/* func */	NULL
)
#if 0
PARAM(
	/* name */	pool_req,
	/* tweak */	tweak_poolparam,
	/* var */	pool_req,
	/* min */	none,
	/* max */	none,
	/* default */	10\,100\,10,
	/* units */	none,
	/* flags */	00,
	/* s-text */
	"Parameters for per worker pool request memory pool.\n"
	"The three numbers are:\n"
	"	min_pool	minimum size of free pool.\n"
	"	max_pool	maximum size of free pool.\n"
	"	max_age		max age of free element.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	pool_sess,
	/* tweak */	tweak_poolparam,
	/* var */	pool_sess,
	/* min */	none,
	/* max */	none,
	/* default */	10,100,10,
	/* units */	,
	/* flags */	00,
	/* s-text */
	"Parameters for per worker pool session memory pool.\n"
	"The three numbers are:\n"
	"	min_pool	minimum size of free pool.\n"
	"	max_pool	maximum size of free pool.\n"
	"	max_age	max age of free element.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	pool_vbo,
	/* tweak */	tweak_poolparam,
	/* var */	pool_vbo,
	/* min */	none,
	/* max */	none,
	/* default */	10,100,10,
	/* units */	,
	/* flags */	00,
	/* s-text */
	"Parameters for backend object fetch memory pool.\n"
	"The three numbers are:\n"
	"	min_pool	minimum size of free pool.\n"
	"	max_pool	maximum size of free pool.\n"
	"	max_age	max age of free element.\n",
	/* l-text */	"",
	/* func */	NULL
)
#endif
PARAM(
	/* name */	prefer_ipv6,
	/* tweak */	tweak_bool,
	/* var */	prefer_ipv6,
	/* min */	none,
	/* max */	none,
	/* default */	off,
	/* units */	bool,
	/* flags */	00,
	/* s-text */
	"Prefer IPv6 address when connecting to backends which have both "
	"IPv4 and IPv6 addresses.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	rush_exponent,
	/* tweak */	tweak_uint,
	/* var */	rush_exponent,
	/* min */	2,
	/* max */	none,
	/* default */	3,
	/* units */	requests per request,
	/* flags */	0| EXPERIMENTAL,
	/* s-text */
	"How many parked request we start for each completed request on "
	"the object.\n"
	"NB: Even with the implict delay of delivery, this parameter "
	"controls an exponential increase in number of worker threads.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	send_timeout,
	/* tweak */	tweak_timeout,
	/* var */	send_timeout,
	/* min */	0.000,
	/* max */	none,
	/* default */	600.000,
	/* units */	seconds,
	/* flags */	0| DELAYED_EFFECT,
	/* s-text */
	"Send timeout for client connections. If the HTTP response hasn't "
	"been transmitted in this many\n"
	"seconds the session is closed.\n"
	"See setsockopt(2) under SO_SNDTIMEO for more information.\n",
	/* l-text */	"",
	/* func */	NULL
)
#if 0
PARAM(
	/* name */	session_max,
	/* tweak */	tweak_uint,
	/* var */	session_max,
	/* min */	1000,
	/* max */	none,
	/* default */	100000,
	/* units */	sessions,
	/* flags */	00,
	/* s-text */
	"Maximum number of sessions we will allocate from one pool before "
	"just dropping connections.\n"
	"This is mostly an anti-DoS measure, and setting it plenty high "
	"should not hurt, as long as you have the memory for it.\n",
	/* l-text */	"",
	/* func */	NULL
)

PARAM(
	/* name */	shm_reclen,
	/* tweak */	tweak_vsl_reclen,
	/* var */	shm_reclen,
	/* min */	16b,
	/* max */	4084,
	/* default */	255b,
	/* units */	bytes,
	/* flags */	00,
	/* s-text */
	"Old name for vsl_reclen, use that instead.\n",
	/* l-text */	"",
	/* func */	NULL
)
#endif
PARAM(
	/* name */	shortlived,
	/* tweak */	tweak_timeout,
	/* var */	shortlived,
	/* min */	0.000,
	/* max */	none,
	/* default */	10.000,
	/* units */	seconds,
	/* flags */	00,
	/* s-text */
	"Objects created with (ttl+grace+keep) shorter than this are "
	"always put in transient storage.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	sigsegv_handler,
	/* tweak */	tweak_bool,
	/* var */	sigsegv_handler,
	/* min */	none,
	/* max */	none,
	/* default */	on,
	/* units */	bool,
	/* flags */	0| MUST_RESTART,
	/* s-text */
	"Install a signal handler which tries to dump debug information on "
	"segmentation faults, bus errors and abort signals.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	syslog_cli_traffic,
	/* tweak */	tweak_bool,
	/* var */	syslog_cli_traffic,
	/* min */	none,
	/* max */	none,
	/* default */	on,
	/* units */	bool,
	/* flags */	00,
	/* s-text */
	"Log all CLI traffic to syslog(LOG_INFO).\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	tcp_keepalive_intvl,
	/* tweak */	tweak_timeout,
	/* var */	tcp_keepalive_intvl,
	/* min */	1.000,
	/* max */	100.000,
	/* default */	5.000,
	/* units */	seconds,
	/* flags */	0| EXPERIMENTAL,
	/* s-text */
	"The number of seconds between TCP keep-alive probes.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	tcp_keepalive_probes,
	/* tweak */	tweak_uint,
	/* var */	tcp_keepalive_probes,
	/* min */	1,
	/* max */	100,
	/* default */	5,
	/* units */	probes,
	/* flags */	0| EXPERIMENTAL,
	/* s-text */
	"The maximum number of TCP keep-alive probes to send before giving "
	"up and killing the connection if no response is obtained from the "
	"other end.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	tcp_keepalive_time,
	/* tweak */	tweak_timeout,
	/* var */	tcp_keepalive_time,
	/* min */	1.000,
	/* max */	7200.000,
	/* default */	600.000,
	/* units */	seconds,
	/* flags */	0| EXPERIMENTAL,
	/* s-text */
	"The number of seconds a connection needs to be idle before TCP "
	"begins sending out keep-alive probes.\n",
	/* l-text */	"",
	/* func */	NULL
)
#if 0
PARAM(
	/* name */	thread_pool_add_delay,
	/* tweak */	tweak_timeout,
	/* var */	thread_pool_add_delay,
	/* min */	0.000,
	/* max */	none,
	/* default */	0.000,
	/* units */	seconds,
	/* flags */	0| EXPERIMENTAL,
	/* s-text */
	"Wait at least this long after creating a thread.\n"
	"\n"
	"Some (buggy) systems may need a short (sub-second) delay between "
	"creating threads.\n"
	"Set this to a few milliseconds if you see the 'threads_failed' "
	"counter grow too much.\n"
	"Setting this too high results in insuffient worker threads.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	thread_pool_destroy_delay,
	/* tweak */	tweak_timeout,
	/* var */	thread_pool_destroy_delay,
	/* min */	0.010,
	/* max */	none,
	/* default */	1.000,
	/* units */	seconds,
	/* flags */	0| DELAYED_EFFECT| EXPERIMENTAL,
	/* s-text */
	"Wait this long after destroying a thread.\n"
	"This controls the decay of thread pools when idle(-ish).\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	thread_pool_fail_delay,
	/* tweak */	tweak_timeout,
	/* var */	thread_pool_fail_delay,
	/* min */	0.010,
	/* max */	none,
	/* default */	0.200,
	/* units */	seconds,
	/* flags */	0| EXPERIMENTAL,
	/* s-text */
	"Wait at least this long after a failed thread creation before "
	"trying to create another thread.\n"
	"\n"
	"Failure to create a worker thread is often a sign that  the end "
	"is near, because the process is running out of some resource.  "
	"This delay tries to not rush the end on needlessly.\n"
	"\n"
	"If thread creation failures are a problem, check that "
	"thread_pool_max is not too high.\n"
	"\n"
	"It may also help to increase thread_pool_timeout and "
	"thread_pool_min, to reduce the rate at which treads are destroyed "
	"and later recreated.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	thread_pool_max,
	/* tweak */	tweak_***,
	/* var */	thread_pool_max,
	/* min */	100,
	/* max */	none,
	/* default */	5000,
	/* units */	threads,
	/* flags */	0| DELAYED_EFFECT,
	/* s-text */
	"The maximum number of worker threads in each pool.\n"
	"\n"
	"Do not set this higher than you have to, since excess worker "
	"threads soak up RAM and CPU and generally just get in the way of "
	"getting work done.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	thread_pool_min,
	/* tweak */	tweak_***,
	/* var */	thread_pool_min,
	/* min */	none,
	/* max */	5000,
	/* default */	100,
	/* units */	threads,
	/* flags */	0| DELAYED_EFFECT,
	/* s-text */
	"The minimum number of worker threads in each pool.\n"
	"\n"
	"Increasing this may help ramp up faster from low load situations "
	"or when threads have expired."
	"Minimum is 10 threads.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	thread_pool_stack,
	/* tweak */	tweak_bytes,
	/* var */	thread_pool_stack,
	/* min */	2k,
	/* max */	none,
	/* default */	48k,
	/* units */	bytes,
	/* flags */	0| EXPERIMENTAL,
	/* s-text */
	"Worker thread stack size.\n"
	"This will likely be rounded up to a multiple of 4k (or whatever "
	"the page_size might be) by the kernel.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	thread_pool_timeout,
	/* tweak */	tweak_timeout,
	/* var */	thread_pool_timeout,
	/* min */	10.000,
	/* max */	none,
	/* default */	300.000,
	/* units */	seconds,
	/* flags */	0| DELAYED_EFFECT| EXPERIMENTAL,
	/* s-text */
	"Thread idle threshold.\n"
	"\n"
	"Threads in excess of thread_pool_min, which have been idle for at "
	"least this long, will be destroyed.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	thread_pools,
	/* tweak */	tweak_uint,
	/* var */	thread_pools,
	/* min */	1,
	/* max */	none,
	/* default */	2,
	/* units */	pools,
	/* flags */	0| DELAYED_EFFECT| EXPERIMENTAL,
	/* s-text */
	"Number of worker thread pools.\n"
	"\n"
	"Increasing number of worker pools decreases lock contention.\n"
	"\n"
	"Too many pools waste CPU and RAM resources, and more than one "
	"pool for each CPU is probably detrimal to performance.\n"
	"\n"
	"Can be increased on the fly, but decreases require a restart to "
	"take effect.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	thread_queue_limit,
	/* tweak */	tweak_uint,
	/* var */	thread_queue_limit,
	/* min */	0,
	/* max */	none,
	/* default */	20,
	/* units */	,
	/* flags */	0| EXPERIMENTAL,
	/* s-text */
	"Permitted queue length per thread-pool.\n"
	"\n"
	"This sets the number of requests we will queue, waiting for an "
	"available thread.  Above this limit sessions will be dropped "
	"instead of queued.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	thread_stats_rate,
	/* tweak */	tweak_uint,
	/* var */	thread_stats_rate,
	/* min */	0,
	/* max */	none,
	/* default */	10,
	/* units */	requests,
	/* flags */	0| EXPERIMENTAL,
	/* s-text */
	"Worker threads accumulate statistics, and dump these into the "
	"global stats counters if the lock is free when they finish a job "
	"(request/fetch etc.)\n"
	"This parameters defines the maximum number of jobs a worker "
	"thread may handle, before it is forced to dump its accumulated "
	"stats into the global counters.\n",
	/* l-text */	"",
	/* func */	NULL
)
#endif
PARAM(
	/* name */	timeout_idle,
	/* tweak */	tweak_timeout,
	/* var */	timeout_idle,
	/* min */	0.000,
	/* max */	none,
	/* default */	5.000,
	/* units */	seconds,
	/* flags */	00,
	/* s-text */
	"Idle timeout for client connections.\n"
	"A connection is considered idle, until we have received the full "
	"request headers.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	timeout_linger,
	/* tweak */	tweak_timeout,
	/* var */	timeout_linger,
	/* min */	0.000,
	/* max */	none,
	/* default */	0.050,
	/* units */	seconds,
	/* flags */	0| EXPERIMENTAL,
	/* s-text */
	"How long the worker thread lingers on an idle session before "
	"handing it over to the waiter.\n"
	"When sessions are reused, as much as half of all reuses happen "
	"within the first 100 msec of the previous request completing.\n"
	"Setting this too high results in worker threads not doing "
	"anything for their keep, setting it too low just means that more "
	"sessions take a detour around the waiter.\n",
	/* l-text */	"",
	/* func */	NULL
)
#if 0
PARAM(
	/* name */	vcc_allow_inline_c,
	/* tweak */	tweak_bool,
	/* var */	vcc_allow_inline_c,
	/* min */	none,
	/* max */	none,
	/* default */	off,
	/* units */	bool,
	/* flags */	00,
	/* s-text */
	"Allow inline C code in VCL.\n",
	/* l-text */	"",
	/* func */	NULL
)
#endif
#if 0
PARAM(
	/* name */	vcc_err_unref,
	/* tweak */	tweak_bool,
	/* var */	vcc_err_unref,
	/* min */	none,
	/* max */	none,
	/* default */	on,
	/* units */	bool,
	/* flags */	00,
	/* s-text */
	"Unreferenced VCL objects result in error.\n",
	/* l-text */	"",
	/* func */	NULL
)
#endif
#if 0
PARAM(
	/* name */	vcc_unsafe_path,
	/* tweak */	tweak_bool,
	/* var */	vcc_unsafe_path,
	/* min */	none,
	/* max */	none,
	/* default */	on,
	/* units */	bool,
	/* flags */	00,
	/* s-text */
	"Allow '/' in vmod & include paths.\n"
	"Allow 'import ... from ...'.\n",
	/* l-text */	"",
	/* func */	NULL
)
#endif
PARAM(
	/* name */	vcl_cooldown,
	/* tweak */	tweak_timeout,
	/* var */	vcl_cooldown,
	/* min */	0.000,
	/* max */	none,
	/* default */	600.000,
	/* units */	seconds,
	/* flags */	00,
	/* s-text */
	"How long time a VCL is kept warm after being replaced as the "
	"active VCL.  (Granularity approximately 30 seconds.)\n",
	/* l-text */	"",
	/* func */	NULL
)
#if 0
PARAM(
	/* name */	vcl_dir,
	/* tweak */	tweak_string,
	/* var */	vcl_dir,
	/* min */	none,
	/* max */	none,
	/* default */	/opt/varnish/etc/varnish,
	/* units */	(null),
	/* flags */	00,
	/* s-text */
	"Directory from which relative VCL filenames (vcl.load and "
	"include) are opened.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	vmod_dir,
	/* tweak */	tweak_string,
	/* var */	vmod_dir,
	/* min */	none,
	/* max */	none,
	/* default */	/opt/varnish/lib/varnish/vmods,
	/* units */	(null),
	/* flags */	00,
	/* s-text */
	"Directory where VCL modules are to be found.\n",
	/* l-text */	"",
	/* func */	NULL
)
#endif
PARAM(
	/* name */	vsl_buffer,
	/* tweak */	tweak_vsl_buffer,
	/* var */	vsl_buffer,
	/* min */	267,
	/* max */	none,
	/* default */	4k,
	/* units */	bytes,
	/* flags */	00,
	/* s-text */
	"Bytes of (req-/backend-)workspace dedicated to buffering VSL "
	"records.\n"
	"Setting this too high costs memory, setting it too low will cause "
	"more VSL flushes and likely increase lock-contention on the VSL "
	"mutex.\n"
	"The minimum tracks the vsl_reclen parameter + 12 bytes.\n",
	/* l-text */	"",
	/* func */	NULL
)
#if 0
PARAM(
	/* name */	vsl_mask,
	/* tweak */	tweak_mask
	/* var */	vsl_mask,
	/* min */	none,
	/* max */	none,
	/* default */	-VCL_trace,-WorkThread,-Hash,-VfpAcct,
	/* units */	,
	/* flags */	00,
	/* s-text */
	"Mask individual VSL messages from being logged.\n"
	"	default	Set default value\n"
	"\n"
	"Use +/- prefixe in front of VSL tag name, to mask/unmask "
	"individual VSL messages.\n",
	/* l-text */	"",
	/* func */	NULL
)
#endif
PARAM(
	/* name */	vsl_reclen,
	/* tweak */	tweak_vsl_reclen,
	/* var */	vsl_reclen,
	/* min */	16b,
	/* max */	4084b,
	/* default */	255b,
	/* units */	bytes,
	/* flags */	00,
	/* s-text */
	"Maximum number of bytes in SHM log record.\n"
	"The maximum tracks the vsl_buffer parameter - 12 bytes.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	vsl_space,
	/* tweak */	tweak_bytes,
	/* var */	vsl_space,
	/* min */	1M,
	/* max */	none,
	/* default */	80M,
	/* units */	bytes,
	/* flags */	0| MUST_RESTART,
	/* s-text */
	"The amount of space to allocate for the VSL fifo buffer in the "
	"VSM memory segment.  If you make this too small, "
	"varnish{ncsa|log} etc will not be able to keep up.  Making it too "
	"large just costs memory resources.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	vsm_space,
	/* tweak */	tweak_bytes,
	/* var */	vsm_space,
	/* min */	1M,
	/* max */	none,
	/* default */	1M,
	/* units */	bytes,
	/* flags */	0| MUST_RESTART,
	/* s-text */
	"The amount of space to allocate for stats counters in the VSM "
	"memory segment.  If you make this too small, some counters will "
	"be invisible.  Making it too large just costs memory resources.\n",
	/* l-text */	"",
	/* func */	NULL
)
#if 0
PARAM(
	/* name */	waiter,
	/* tweak */	tweak_waiter,
	/* var */	waiter,
	/* min */	none,
	/* max */	none,
	/* default */	kqueue (possible values: kqueue, poll),
	/* units */	(null),
	/* flags */	0| MUST_RESTART| WIZARD,
	/* s-text */
	"Select the waiter kernel interface.\n",
	/* l-text */	"",
	/* func */	NULL
)
#endif
PARAM(
	/* name */	workspace_backend,
	/* tweak */	tweak_bytes_u,
	/* var */	workspace_backend,
	/* min */	1k,
	/* max */	none,
	/* default */	64k,
	/* units */	bytes,
	/* flags */	0| DELAYED_EFFECT,
	/* s-text */
	"Bytes of HTTP protocol workspace for backend HTTP req/resp.  If "
	"larger than 4k, use a multiple of 4k for VM efficiency.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	workspace_client,
	/* tweak */	tweak_bytes_u,
	/* var */	workspace_client,
	/* min */	9k,
	/* max */	none,
	/* default */	64k,
	/* units */	bytes,
	/* flags */	0| DELAYED_EFFECT,
	/* s-text */
	"Bytes of HTTP protocol workspace for clients HTTP req/resp.  If "
	"larger than 4k, use a multiple of 4k for VM efficiency.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	workspace_session,
	/* tweak */	tweak_bytes_u,
	/* var */	workspace_session,
	/* min */	0.25k,
	/* max */	none,
	/* default */	0.50k,
	/* units */	bytes,
	/* flags */	0| DELAYED_EFFECT,
	/* s-text */
	"Allocation size for session structure and workspace.    The "
	"workspace is primarily used for TCP connection addresses.  If "
	"larger than 4k, use a multiple of 4k for VM efficiency.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	workspace_thread,
	/* tweak */	tweak_bytes_u,
	/* var */	workspace_thread,
	/* min */	0.25k,
	/* max */	8k,
	/* default */	2k,
	/* units */	bytes,
	/* flags */	0| DELAYED_EFFECT,
	/* s-text */
	"Bytes of auxiliary workspace per thread.\n"
	"This workspace is used for certain temporary data structures "
	"during the operation of a worker thread.\n"
	"One use is for the io-vectors for writing requests and responses "
	"to sockets, having too little space will result in more writev(2) "
	"system calls, having too much just wastes the space.\n",
	/* l-text */	"",
	/* func */	NULL
)
#endif
/*lint -restore */
