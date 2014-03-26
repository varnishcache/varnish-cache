/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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
 */

#include "config.h"

#include <stdio.h>

#include "mgt/mgt.h"
#include "common/params.h"

#include "mgt/mgt_param.h"
#include "waiter/waiter.h"


#define MEMPOOL_TEXT							\
	"The three numbers are:\n"					\
	"\tmin_pool\tminimum size of free pool.\n"			\
	"\tmax_pool\tmaximum size of free pool.\n"			\
	"\tmax_age\tmax age of free element."

/*
 * Remember to update varnishd.1 whenever you add / remove a parameter or
 * change its default value.
 * XXX: we should generate the relevant section of varnishd.1 from here.
 */

struct parspec mgt_parspec[] = {
	{ "user", tweak_user, NULL, NULL, NULL,
		"The unprivileged user to run as.",
		MUST_RESTART,
		"" },
	{ "group", tweak_group, NULL, NULL, NULL,
		"The unprivileged group to run as.",
		MUST_RESTART,
		"" },
	{ "default_ttl", tweak_timeout, &mgt_param.default_ttl,
		"0", NULL,
		"The TTL assigned to objects if neither the backend nor "
		"the VCL code assigns one.",
		OBJ_STICKY,
		"120", "seconds" },
	{ "default_grace", tweak_timeout, &mgt_param.default_grace,
		"0", NULL,
		"Default grace period.  We will deliver an object "
		"this long after it has expired, provided another thread "
		"is attempting to get a new copy.",
		OBJ_STICKY,
		"10", "seconds" },
	{ "default_keep", tweak_timeout, &mgt_param.default_keep,
		"0", NULL,
		"Default keep period.  We will keep a useless object "
		"around this long, making it available for conditional "
		"backend fetches.  "
		"That means that the object will be removed from the "
		"cache at the end of ttl+grace+keep.",
		OBJ_STICKY,
		"0", "seconds" },
	{ "workspace_session",
		tweak_bytes_u, &mgt_param.workspace_session,
		"256", NULL,
		"Bytes of workspace for session and TCP connection addresses."
		"  If larger than 4k, use a multiple of 4k for VM efficiency.",
		DELAYED_EFFECT,
		"384", "bytes" },
	{ "workspace_client",
		tweak_bytes_u, &mgt_param.workspace_client,
		"3072", NULL,
		"Bytes of HTTP protocol workspace for clients HTTP req/resp."
		"  If larger than 4k, use a multiple of 4k for VM efficiency.",
		DELAYED_EFFECT,
		"64k", "bytes" },
	{ "workspace_backend",
		tweak_bytes_u, &mgt_param.workspace_backend,
		"1024", NULL,
		"Bytes of HTTP protocol workspace for backend HTTP req/resp."
		"  If larger than 4k, use a multiple of 4k for VM efficiency.",
		DELAYED_EFFECT,
		"64k", "bytes" },
	{ "workspace_thread",
		tweak_bytes_u, &mgt_param.workspace_thread,
		"256", "8192",
		"Bytes of auxillary workspace per thread.\n"
		"This workspace is used for certain temporary data structures"
		" during the operation of a worker thread.\n"
		"One use is for the io-vectors for writing requests and"
		" responses to sockets, having too little space will"
		" result in more writev(2) system calls, having too much"
		" just wastes the space.",
		DELAYED_EFFECT,
		"2048", "bytes" },
	{ "http_req_hdr_len",
		tweak_bytes_u, &mgt_param.http_req_hdr_len,
		"40", NULL,
		"Maximum length of any HTTP client request header we will "
		"allow.  The limit is inclusive its continuation lines.",
		0,
		"8k", "bytes" },
	{ "http_req_size",
		tweak_bytes_u, &mgt_param.http_req_size,
		"256", NULL,
		"Maximum number of bytes of HTTP client request we will deal "
		"with.  This is a limit on all bytes up to the double blank "
		"line which ends the HTTP request.\n"
		"The memory for the request is allocated from the client "
		"workspace (param: workspace_client) and this parameter limits "
		"how much of that the request is allowed to take up.",
		0,
		"32k", "bytes" },
	{ "http_resp_hdr_len",
		tweak_bytes_u, &mgt_param.http_resp_hdr_len,
		"40", NULL,
		"Maximum length of any HTTP backend response header we will "
		"allow.  The limit is inclusive its continuation lines.",
		0,
		"8k", "bytes" },
	{ "http_resp_size",
		tweak_bytes_u, &mgt_param.http_resp_size,
		"256", NULL,
		"Maximum number of bytes of HTTP backend resonse we will deal "
		"with.  This is a limit on all bytes up to the double blank "
		"line which ends the HTTP request.\n"
		"The memory for the request is allocated from the worker "
		"workspace (param: thread_pool_workspace) and this parameter "
		"limits how much of that the request is allowed to take up.",
		0,
		"32k", "bytes" },
	{ "http_max_hdr", tweak_uint, &mgt_param.http_max_hdr,
		"32", "65535",
		"Maximum number of HTTP header lines we allow in "
		"{req|resp|bereq|beresp}.http "
		"(obj.http is autosized to the exact number of headers).\n"
		"Cheap, ~20 bytes, in terms of workspace memory.\n"
		"Note that the first line occupies five header lines.",
		0,
		"64", "header lines" },
	{ "vsl_buffer",
		tweak_bytes_u, &mgt_param.vsl_buffer,
		"1024", NULL,
		"Bytes of (req-/backend-)workspace dedicated to buffering"
		" VSL records.\n"
		"At a bare minimum, this must be longer than"
		" the longest HTTP header to be logged.\n"
		"Setting this too high costs memory, setting it too low"
		" will cause more VSL flushes and likely increase"
		" lock-contention on the VSL mutex.\n"
		"Minimum is 1k bytes.",
		0,
		"4k", "bytes" },
	{ "shm_reclen",
		tweak_bytes_u, &mgt_param.shm_reclen,
		"16", "65535",
		"Maximum number of bytes in SHM log record.\n"
		"Maximum is 65535 bytes.",
		0,
		"255", "bytes" },
	{ "timeout_idle", tweak_timeout, &mgt_param.timeout_idle,
		"0", NULL,
		"Idle timeout for client connections.\n"
		"A connection is considered idle, until we receive"
		" a non-white-space character on it.",
		0,
		"5", "seconds" },
	{ "timeout_req", tweak_timeout, &mgt_param.timeout_req,
		"0", NULL,
		"Max time to receive clients request header, measured"
		" from first non-white-space character to double CRNL.",
		0,
		"2", "seconds" },
	{ "pipe_timeout", tweak_timeout, &mgt_param.pipe_timeout,
		"0", NULL,
		"Idle timeout for PIPE sessions. "
		"If nothing have been received in either direction for "
		"this many seconds, the session is closed.",
		0,
		"60", "seconds" },
	{ "send_timeout", tweak_timeout, &mgt_param.send_timeout,
		"0", NULL,
		"Send timeout for client connections. "
		"If the HTTP response hasn't been transmitted in this many\n"
                "seconds the session is closed.\n"
		"See setsockopt(2) under SO_SNDTIMEO for more information.",
		DELAYED_EFFECT,
		"600", "seconds" },
	{ "idle_send_timeout", tweak_timeout, &mgt_param.idle_send_timeout,
		"0", NULL,
		"Time to wait with no data sent. "
		"If no data has been transmitted in this many\n"
                "seconds the session is closed.\n"
		"See setsockopt(2) under SO_SNDTIMEO for more information.",
		DELAYED_EFFECT,
		"60", "seconds" },
	{ "auto_restart", tweak_bool, &mgt_param.auto_restart,
		NULL, NULL,
		"Restart child process automatically if it dies.",
		0,
		"on", "bool" },
	{ "nuke_limit",
		tweak_uint, &mgt_param.nuke_limit,
		"0", NULL,
		"Maximum number of objects we attempt to nuke in order"
		"to make space for a object body.",
		EXPERIMENTAL,
		"50", "allocations" },
	{ "fetch_chunksize",
		tweak_bytes, &mgt_param.fetch_chunksize,
		"4096", NULL,
		"The default chunksize used by fetcher. "
		"This should be bigger than the majority of objects with "
		"short TTLs.\n"
		"Internal limits in the storage_file module makes increases "
		"above 128kb a dubious idea.",
		EXPERIMENTAL,
		"128k", "bytes" },
	{ "fetch_maxchunksize",
		tweak_bytes, &mgt_param.fetch_maxchunksize,
		"65536", NULL,
		"The maximum chunksize we attempt to allocate from storage. "
		"Making this too large may cause delays and storage "
		"fragmentation.",
		EXPERIMENTAL,
		"256m", "bytes" },
#ifdef HAVE_ACCEPT_FILTERS
	{ "accept_filter", tweak_bool, &mgt_param.accept_filter,
		NULL, NULL,
		"Enable kernel accept-filters, (if available in the kernel).",
		MUST_RESTART,
		"on", "bool" },
#endif
	{ "listen_address", tweak_listen_address, NULL,
		NULL, NULL,
		"Whitespace separated list of network endpoints where "
		"Varnish will accept requests.\n"
		"Possible formats: host, host:port, :port",
		MUST_RESTART,
		":80" },
	{ "listen_depth", tweak_uint, &mgt_param.listen_depth,
		"0", NULL,
		"Listen queue depth.",
		MUST_RESTART,
		"1024", "connections" },
	{ "cli_buffer",
		tweak_bytes_u, &mgt_param.cli_buffer,
		"4096", NULL,
		"Size of buffer for CLI command input."
		"\nYou may need to increase this if you have big VCL files "
		"and use the vcl.inline CLI command.\n"
		"NB: Must be specified with -p to have effect.",
		0,
		"8k", "bytes" },
	{ "cli_limit",
		tweak_bytes_u, &mgt_param.cli_limit,
		"128", "99999999",
		"Maximum size of CLI response.  If the response exceeds"
		" this limit, the reponse code will be 201 instead of"
		" 200 and the last line will indicate the truncation.",
		0,
		"48k", "bytes" },
	{ "cli_timeout", tweak_timeout, &mgt_param.cli_timeout,
		"0", NULL,
		"Timeout for the childs replies to CLI requests from "
		"the mgt_param.",
		0,
		"60", "seconds" },
	{ "ping_interval", tweak_uint, &mgt_param.ping_interval,
		"0", NULL,
		"Interval between pings from parent to child.\n"
		"Zero will disable pinging entirely, which makes "
		"it possible to attach a debugger to the child.",
		MUST_RESTART,
		"3", "seconds" },
	{ "lru_interval", tweak_timeout, &mgt_param.lru_interval,
		"0", NULL,
		"Grace period before object moves on LRU list.\n"
		"Objects are only moved to the front of the LRU "
		"list if they have not been moved there already inside "
		"this timeout period.  This reduces the amount of lock "
		"operations necessary for LRU list access.",
		EXPERIMENTAL,
		"2", "seconds" },
	{ "cc_command", tweak_string, &mgt_cc_cmd,
		NULL, NULL,
		"Command used for compiling the C source code to a "
		"dlopen(3) loadable object.  Any occurrence of %s in "
		"the string will be replaced with the source file name, "
		"and %o will be replaced with the output file name.",
		MUST_RELOAD,
		VCC_CC , NULL },
	{ "max_restarts", tweak_uint, &mgt_param.max_restarts,
		"0", NULL,
		"Upper limit on how many times a request can restart."
		"\nBe aware that restarts are likely to cause a hit against "
		"the backend, so don't increase thoughtlessly.",
		0,
		"4", "restarts" },
	{ "max_retries", tweak_uint, &mgt_param.max_retries,
		"0", NULL,
		"Upper limit on how many times a backend fetch can retry.",
		0,
		"4", "retries" },
	{ "max_esi_depth", tweak_uint, &mgt_param.max_esi_depth,
		"0", NULL,
		"Maximum depth of esi:include processing.",
		0,
		"5", "levels" },
	{ "connect_timeout", tweak_timeout, &mgt_param.connect_timeout,
		"0", NULL,
		"Default connection timeout for backend connections. "
		"We only try to connect to the backend for this many "
		"seconds before giving up. "
		"VCL can override this default value for each backend and "
		"backend request.",
		0,
		"3.5", "s" },
	{ "first_byte_timeout", tweak_timeout,
		&mgt_param.first_byte_timeout,
		"0", NULL,
		"Default timeout for receiving first byte from backend. "
		"We only wait for this many seconds for the first "
		"byte before giving up. A value of 0 means it will never time "
		"out. "
		"VCL can override this default value for each backend and "
		"backend request. This parameter does not apply to pipe.",
		0,
		"60", "s" },
	{ "between_bytes_timeout", tweak_timeout,
		&mgt_param.between_bytes_timeout,
		"0", NULL,
		"Default timeout between bytes when receiving data from "
		"backend. "
		"We only wait for this many seconds between bytes "
		"before giving up. A value of 0 means it will never time out. "
		"VCL can override this default value for each backend request "
		"and backend request. This parameter does not apply to pipe.",
		0,
		"60", "s" },
	{ "acceptor_sleep_max", tweak_timeout,
		&mgt_param.acceptor_sleep_max,
		"0", "10",
		"If we run out of resources, such as file descriptors or "
		"worker threads, the acceptor will sleep between accepts.\n"
		"This parameter limits how long it can sleep between "
		"attempts to accept new connections.",
		EXPERIMENTAL,
		"0.050", "s" },
	{ "acceptor_sleep_incr", tweak_timeout,
		&mgt_param.acceptor_sleep_incr,
		"0", "1",
		"If we run out of resources, such as file descriptors or "
		"worker threads, the acceptor will sleep between accepts.\n"
		"This parameter control how much longer we sleep, each time "
		"we fail to accept a new connection.",
		EXPERIMENTAL,
		"0.001", "s" },
	{ "acceptor_sleep_decay", tweak_double,
		&mgt_param.acceptor_sleep_decay,
		"0", "1",
		"If we run out of resources, such as file descriptors or "
		"worker threads, the acceptor will sleep between accepts.\n"
		"This parameter (multiplicatively) reduce the sleep duration "
		"for each succesfull accept. (ie: 0.9 = reduce by 10%)",
		EXPERIMENTAL,
		"0.900", "" },
	{ "clock_skew", tweak_uint, &mgt_param.clock_skew,
		"0", NULL,
		"How much clockskew we are willing to accept between the "
		"backend and our own clock.",
		0,
		"10", "s" },
	{ "prefer_ipv6", tweak_bool, &mgt_param.prefer_ipv6,
		NULL, NULL,
		"Prefer IPv6 address when connecting to backends which "
		"have both IPv4 and IPv6 addresses.",
		0,
		"off", "bool" },
	{ "session_max", tweak_uint,
		&mgt_param.max_sess,
		"1000", NULL,
		"Maximum number of sessions we will allocate from one pool "
		"before just dropping connections.\n"
		"This is mostly an anti-DoS measure, and setting it plenty "
		"high should not hurt, as long as you have the memory for "
		"it.",
		0,
		"100000", "sessions" },
	{ "timeout_linger", tweak_timeout, &mgt_param.timeout_linger,
		"0", NULL,
		"How long time the workerthread lingers on an idle session "
		"before handing it over to the waiter.\n"
		"When sessions are reused, as much as half of all reuses "
		"happen within the first 100 msec of the previous request "
		"completing.\n"
		"Setting this too high results in worker threads not doing "
		"anything for their keep, setting it too low just means that "
		"more sessions take a detour around the waiter.",
		EXPERIMENTAL,
		"0.050", "seconds" },
	{ "waiter", tweak_waiter, NULL,
		NULL, NULL,
		"Select the waiter kernel interface.",
		WIZARD | MUST_RESTART,
		WAITER_DEFAULT, NULL },
	{ "ban_dups", tweak_bool, &mgt_param.ban_dups,
		NULL, NULL,
		"Elimited older identical bans when new bans are created."
		"  This test is CPU intensive and scales with the number and"
		" complexity of active (non-Gone) bans.  If identical bans"
		" are frequent, the amount of CPU needed to actually test "
		" the bans will be similarly reduced.",
		0,
		"on", "bool" },
	{ "syslog_cli_traffic", tweak_bool, &mgt_param.syslog_cli_traffic,
		NULL, NULL,
		"Log all CLI traffic to syslog(LOG_INFO).",
		0,
		"on", "bool" },
	{ "ban_lurker_age", tweak_timeout,
		&mgt_param.ban_lurker_age,
		"0", NULL,
		"The ban lurker does not process bans until they are this"
		" old.  Right when a ban is added, the most frequently hit"
		" objects will get tested against it as part of object"
		" lookup.  This parameter prevents the ban-lurker from"
		" kicking in, until the rush is over.",
		0,
		"60", "s" },
	{ "ban_lurker_sleep", tweak_timeout,
		&mgt_param.ban_lurker_sleep,
		"0", NULL,
		"The ban lurker thread sleeps between work batches, in order"
		" to not monopolize CPU power."
		"  When nothing is done, it sleeps a fraction of a second"
		" before looking for new work to do.\n"
		"A value of zero disables the ban lurker.",
		0,
		"0.01", "s" },
	{ "ban_lurker_batch", tweak_uint,
		&mgt_param.ban_lurker_batch,
		"1", NULL,
		"How many objects the ban lurker examines before taking a"
		" ban_lurker_sleep.  Use this to pace the ban lurker so it"
		" does not eat too much CPU.",
		0,
		"1000", "" },
	{ "http_range_support", tweak_bool, &mgt_param.http_range_support,
		NULL, NULL,
		"Enable support for HTTP Range headers.",
		0,
		"on", "bool" },
	{ "http_gzip_support", tweak_bool, &mgt_param.http_gzip_support,
		NULL, NULL,
		"Enable gzip support. When enabled Varnish request compressed "
		"objects from the backend and store them compressed. "
		"If a client does not support gzip encoding Varnish will "
		"uncompress compressed objects on demand. Varnish will also "
		"rewrite the Accept-Encoding header of clients indicating "
		"support for gzip to:\n"
		"  Accept-Encoding: gzip\n\n"
		"Clients that do not support gzip will have their "
		"Accept-Encoding header removed. For more information on how "
		"gzip is implemented please see the chapter on gzip in the "
		"Varnish reference.",
		0,
		"on", "bool" },
	{ "gzip_level", tweak_uint, &mgt_param.gzip_level,
		"0", "9",
		"Gzip compression level: 0=debug, 1=fast, 9=best",
		0,
		"6", ""},
	{ "gzip_memlevel", tweak_uint, &mgt_param.gzip_memlevel,
		"1", "9",
		"Gzip memory level 1=slow/least, 9=fast/most compression.\n"
		"Memory impact is 1=1k, 2=2k, ... 9=256k.",
		0,
		"8", ""},
	{ "gzip_buffer",
		tweak_bytes_u, &mgt_param.gzip_buffer,
	        "2048", NULL,
		"Size of malloc buffer used for gzip processing.\n"
		"These buffers are used for in-transit data,"
		" for instance gunzip'ed data being sent to a client."
		"Making this space to small results in more overhead,"
		" writes to sockets etc, making it too big is probably"
		" just a waste of memory.",
		EXPERIMENTAL,
		"32k", "bytes" },
	{ "shortlived", tweak_timeout,
		&mgt_param.shortlived,
		"0", NULL,
		"Objects created with (ttl+grace+keep) shorter than this"
		" are always put in transient storage.",
		0,
		"10.0", "s" },
	{ "critbit_cooloff", tweak_timeout,
		&mgt_param.critbit_cooloff,
		"60", "254",
		"How long time the critbit hasher keeps deleted objheads "
		"on the cooloff list.",
		WIZARD,
		"180.0", "s" },
	{ "sigsegv_handler", tweak_bool, &mgt_param.sigsegv_handler,
		NULL, NULL,
		"Install a signal handler which tries to dump debug "
		"information on segmentation faults.",
		MUST_RESTART,
		"off", "bool" },
	{ "vcl_dir", tweak_string, &mgt_vcl_dir,
		NULL, NULL,
		"Directory from which relative VCL filenames (vcl.load and "
		"include) are opened.",
		0,
#ifdef VARNISH_VCL_DIR
		VARNISH_VCL_DIR,
#else
		".",
#endif
		NULL },
	{ "vmod_dir", tweak_string, &mgt_vmod_dir,
		NULL, NULL,
		"Directory where VCL modules are to be found.",
		0,
#ifdef VARNISH_VMOD_DIR
		VARNISH_VMOD_DIR,
#else
		".",
#endif
		NULL },

	{ "vcc_err_unref", tweak_bool, &mgt_vcc_err_unref,
		NULL, NULL,
		"Unreferenced VCL objects result in error.",
		0,
		"on", "bool" },

	{ "vcc_allow_inline_c", tweak_bool, &mgt_vcc_allow_inline_c,
		NULL, NULL,
		"Allow inline C code in VCL.",
		0,
		"off", "bool" },

	{ "vcc_unsafe_path", tweak_bool, &mgt_vcc_unsafe_path,
		NULL, NULL,
		"Allow '/' in vmod & include paths.\n"
		"Allow 'import ... from ...'.",
		0,
		"on", "bool" },

	{ "pcre_match_limit", tweak_uint,
		&mgt_param.vre_limits.match,
		"1", NULL,
		"The limit for the  number of internal matching function"
		" calls in a pcre_exec() execution.",
		0,
		"10000", ""},

	{ "pcre_match_limit_recursion", tweak_uint,
		&mgt_param.vre_limits.match_recursion,
		"1", NULL,
		"The limit for the  number of internal matching function"
		" recursions in a pcre_exec() execution.",
		0,
		"10000", ""},

	{ "vsl_space", tweak_bytes,
		&mgt_param.vsl_space,
		"1M", NULL,
		"The amount of space to allocate for the VSL fifo buffer"
		" in the VSM memory segment."
		"  If you make this too small, varnish{ncsa|log} etc will"
		" not be able to keep up."
		"  Making it too large just costs memory resources.",
		MUST_RESTART,
		"80M", "bytes"},

	{ "vsm_space", tweak_bytes,
		&mgt_param.vsm_space,
		"1M", NULL,
		"The amount of space to allocate for stats counters"
		" in the VSM memory segment."
		"  If you make this too small, some counters will be"
		" invisible."
		"  Making it too large just costs memory resources.",
		MUST_RESTART,
		"1M", "bytes"},

	{ "busyobj_worker_cache", tweak_bool,
		&mgt_param.bo_cache,
		NULL, NULL,
		"Cache free busyobj per worker thread. "
		"Disable this if you have very high hitrates and want "
		"to save the memory of one busyobj per worker thread.",
		0,
		"off", "bool"},

	{ "pool_vbc", tweak_poolparam, &mgt_param.vbc_pool,
		NULL, NULL,
		"Parameters for backend connection memory pool.\n"
		MEMPOOL_TEXT,
		0,
		"10,100,10", ""},

	{ "pool_req", tweak_poolparam, &mgt_param.req_pool,
		NULL, NULL,
		"Parameters for per worker pool request memory pool.\n"
		MEMPOOL_TEXT,
		0,
		"10,100,10", ""},
	{ "pool_sess", tweak_poolparam, &mgt_param.sess_pool,
		NULL, NULL,
		"Parameters for per worker pool session memory pool.\n"
		MEMPOOL_TEXT,
		0,
		"10,100,10", ""},
	{ "pool_vbo", tweak_poolparam, &mgt_param.vbo_pool,
		NULL, NULL,
		"Parameters for backend object fetch memory pool.\n"
		MEMPOOL_TEXT,
		0,
		"10,100,10", ""},

	{ NULL, NULL, NULL }
};
