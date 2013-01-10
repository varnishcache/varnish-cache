/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2013 Varnish Software AS
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

#include <limits.h>
#include <stdio.h>

#include "mgt/mgt.h"
#include "common/params.h"

#include "mgt/mgt_param.h"
#include "waiter/waiter.h"

/*
 * Remember to update varnishd.1 whenever you add / remove a parameter or
 * change its default value.
 * XXX: we should generate the relevant section of varnishd.1 from here.
 */

const struct parspec mgt_parspec[] = {
	{ "user", tweak_user, NULL, 0, 0,
		"The unprivileged user to run as.",
		MUST_RESTART,
		"" },
	{ "group", tweak_group, NULL, 0, 0,
		"The unprivileged group to run as.",
		MUST_RESTART,
		"" },
	{ "default_ttl", tweak_timeout_double, &mgt_param.default_ttl,
		0, UINT_MAX,
		"The TTL assigned to objects if neither the backend nor "
		"the VCL code assigns one.\n"
		"Objects already cached will not be affected by changes "
		"made until they are fetched from the backend again.\n"
		"To force an immediate effect at the expense of a total "
		"flush of the cache use \"ban obj.http.date ~ .\"",
		0,
		"120", "seconds" },
	{ "workspace_client",
		tweak_bytes_u, &mgt_param.workspace_client, 3072, UINT_MAX,
		"Bytes of HTTP protocol workspace for clients HTTP req/resp."
		"  If larger than 4k, use a multiple of 4k for VM efficiency.",
		DELAYED_EFFECT,
		"64k", "bytes" },
	{ "workspace_backend",
		tweak_bytes_u, &mgt_param.workspace_backend, 1024, UINT_MAX,
		"Bytes of HTTP protocol workspace for backend HTTP req/resp."
		"  If larger than 4k, use a multiple of 4k for VM efficiency.",
		DELAYED_EFFECT,
		"64k", "bytes" },
	{ "workspace_thread",
		tweak_bytes_u, &mgt_param.workspace_thread, 256, 8192,
		"Bytes of auxillary workspace per thread.\n"
		"This workspace is used for certain temporary data structures"
		" during the operation of a worker thread.\n"
		"One use is for the io-vectors for writing requests and"
		" responses to sockets, having too little space will"
		" result in more writev(2) system calls, having too much"
		" just wastes the space.\n",
		DELAYED_EFFECT,
		"2048", "bytes" },
	{ "http_req_hdr_len",
		tweak_bytes_u, &mgt_param.http_req_hdr_len,
		40, UINT_MAX,
		"Maximum length of any HTTP client request header we will "
		"allow.  The limit is inclusive its continuation lines.\n",
		0,
		"8k", "bytes" },
	{ "http_req_size",
		tweak_bytes_u, &mgt_param.http_req_size,
		256, UINT_MAX,
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
		40, UINT_MAX,
		"Maximum length of any HTTP backend response header we will "
		"allow.  The limit is inclusive its continuation lines.\n",
		0,
		"8k", "bytes" },
	{ "http_resp_size",
		tweak_bytes_u, &mgt_param.http_resp_size,
		256, UINT_MAX,
		"Maximum number of bytes of HTTP backend resonse we will deal "
		"with.  This is a limit on all bytes up to the double blank "
		"line which ends the HTTP request.\n"
		"The memory for the request is allocated from the worker "
		"workspace (param: thread_pool_workspace) and this parameter "
		"limits how much of that the request is allowed to take up.",
		0,
		"32k", "bytes" },
	{ "http_max_hdr", tweak_uint, &mgt_param.http_max_hdr, 32, 65535,
		"Maximum number of HTTP headers we will deal with in "
		"client request or backend reponses.  "
		"Note that the first line occupies five header fields.\n"
		"This parameter does not influence storage consumption, "
		"objects allocate exact space for the headers they store.\n",
		0,
		"64", "header lines" },
	{ "vsl_buffer",
		tweak_bytes_u, &mgt_param.vsl_buffer, 1024, UINT_MAX,
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
		tweak_bytes_u, &mgt_param.shm_reclen, 16, 65535,
		"Maximum number of bytes in SHM log record.\n"
		"Maximum is 65535 bytes.",
		0,
		"255", "bytes" },
	{ "default_grace", tweak_timeout_double, &mgt_param.default_grace,
		0, UINT_MAX,
		"Default grace period.  We will deliver an object "
		"this long after it has expired, provided another thread "
		"is attempting to get a new copy.\n"
		"Objects already cached will not be affected by changes "
		"made until they are fetched from the backend again.\n",
		DELAYED_EFFECT,
		"10", "seconds" },
	{ "default_keep", tweak_timeout_double, &mgt_param.default_keep,
		0, UINT_MAX,
		"Default keep period.  We will keep a useless object "
		"around this long, making it available for conditional "
		"backend fetches.  "
		"That means that the object will be removed from the "
		"cache at the end of ttl+grace+keep.",
		DELAYED_EFFECT,
		"0", "seconds" },
	{ "timeout_idle", tweak_timeout_double, &mgt_param.timeout_idle,
		0, UINT_MAX,
		"Idle timeout for client connections.\n"
		"A connection is considered idle, until we receive"
		" a non-white-space character on it.",
		0,
		"5", "seconds" },
	{ "timeout_req", tweak_timeout_double, &mgt_param.timeout_req,
		0, UINT_MAX,
		"Max time to receive clients request header, measured"
		" from first non-white-space character to double CRNL.",
		0,
		"2", "seconds" },
	{ "expiry_sleep", tweak_timeout_double, &mgt_param.expiry_sleep, 0, 60,
		"How long the expiry thread sleeps when there is nothing "
		"for it to do.\n",
		0,
		"1", "seconds" },
	{ "pipe_timeout", tweak_timeout, &mgt_param.pipe_timeout, 0, 0,
		"Idle timeout for PIPE sessions. "
		"If nothing have been received in either direction for "
		"this many seconds, the session is closed.\n",
		0,
		"60", "seconds" },
	{ "send_timeout", tweak_timeout, &mgt_param.send_timeout, 0, 0,
		"Send timeout for client connections. "
		"If the HTTP response hasn't been transmitted in this many\n"
                "seconds the session is closed. \n"
		"See setsockopt(2) under SO_SNDTIMEO for more information.",
		DELAYED_EFFECT,
		"600", "seconds" },
	{ "idle_send_timeout", tweak_timeout, &mgt_param.idle_send_timeout,
		0, 0,
		"Time to wait with no data sent. "
		"If no data has been transmitted in this many\n"
                "seconds the session is closed. \n"
		"See setsockopt(2) under SO_SNDTIMEO for more information.",
		DELAYED_EFFECT,
		"60", "seconds" },
	{ "auto_restart", tweak_bool, &mgt_param.auto_restart, 0, 0,
		"Restart child process automatically if it dies.\n",
		0,
		"on", "bool" },
	{ "nuke_limit",
		tweak_uint, &mgt_param.nuke_limit, 0, UINT_MAX,
		"Maximum number of objects we attempt to nuke in order"
		"to make space for a object body.",
		EXPERIMENTAL,
		"50", "allocations" },
	{ "fetch_chunksize",
		tweak_bytes,
		    &mgt_param.fetch_chunksize, 4 * 1024, UINT_MAX,
		"The default chunksize used by fetcher. "
		"This should be bigger than the majority of objects with "
		"short TTLs.\n"
		"Internal limits in the storage_file module makes increases "
		"above 128kb a dubious idea.",
		EXPERIMENTAL,
		"128k", "bytes" },
	{ "fetch_maxchunksize",
		tweak_bytes,
		    &mgt_param.fetch_maxchunksize, 64 * 1024, UINT_MAX,
		"The maximum chunksize we attempt to allocate from storage. "
		"Making this too large may cause delays and storage "
		"fragmentation.\n",
		EXPERIMENTAL,
		"256m", "bytes" },
	{ "accept_filter", tweak_bool, &mgt_param.accept_filter, 0, 0,
		"Enable kernel accept-filters, if supported by the kernel.",
		MUST_RESTART,
		"on", "bool" },
	{ "listen_address", tweak_listen_address, NULL, 0, 0,
		"Whitespace separated list of network endpoints where "
		"Varnish will accept requests.\n"
		"Possible formats: host, host:port, :port",
		MUST_RESTART,
		":80" },
	{ "listen_depth", tweak_uint, &mgt_param.listen_depth, 0, UINT_MAX,
		"Listen queue depth.",
		MUST_RESTART,
		"1024", "connections" },
	{ "cli_buffer",
		tweak_bytes_u, &mgt_param.cli_buffer, 4096, UINT_MAX,
		"Size of buffer for CLI command input."
		"\nYou may need to increase this if you have big VCL files "
		"and use the vcl.inline CLI command.\n"
		"NB: Must be specified with -p to have effect.\n",
		0,
		"8k", "bytes" },
	{ "cli_limit",
		tweak_bytes_u, &mgt_param.cli_limit, 128, 99999999,
		"Maximum size of CLI response.  If the response exceeds"
		" this limit, the reponse code will be 201 instead of"
		" 200 and the last line will indicate the truncation.",
		0,
		"48k", "bytes" },
	{ "cli_timeout", tweak_timeout, &mgt_param.cli_timeout, 0, 0,
		"Timeout for the childs replies to CLI requests from "
		"the mgt_param.",
		0,
		"10", "seconds" },
	{ "ping_interval", tweak_uint, &mgt_param.ping_interval, 0, UINT_MAX,
		"Interval between pings from parent to child.\n"
		"Zero will disable pinging entirely, which makes "
		"it possible to attach a debugger to the child.",
		MUST_RESTART,
		"3", "seconds" },
	{ "lru_interval", tweak_timeout, &mgt_param.lru_timeout, 0, 0,
		"Grace period before object moves on LRU list.\n"
		"Objects are only moved to the front of the LRU "
		"list if they have not been moved there already inside "
		"this timeout period.  This reduces the amount of lock "
		"operations necessary for LRU list access.",
		EXPERIMENTAL,
		"2", "seconds" },
	{ "cc_command", tweak_string, &mgt_cc_cmd, 0, 0,
		"Command used for compiling the C source code to a "
		"dlopen(3) loadable object.  Any occurrence of %s in "
		"the string will be replaced with the source file name, "
		"and %o will be replaced with the output file name.",
		MUST_RELOAD,
		VCC_CC , NULL },
	{ "max_restarts", tweak_uint, &mgt_param.max_restarts, 0, UINT_MAX,
		"Upper limit on how many times a request can restart."
		"\nBe aware that restarts are likely to cause a hit against "
		"the backend, so don't increase thoughtlessly.\n",
		0,
		"4", "restarts" },
	{ "esi_syntax",
		tweak_uint, &mgt_param.esi_syntax, 0, UINT_MAX,
		"Bitmap controlling ESI parsing code:\n"
		"  0x00000001 - Don't check if it looks like XML\n"
		"  0x00000002 - Ignore non-esi elements\n"
		"  0x00000004 - Emit parsing debug records\n"
		"  0x00000008 - Force-split parser input (debugging)\n"
		"\n"
		"Use 0x notation and do the bitor in your head :-)\n",
		0,
		"0", "bitmap" },
	{ "max_esi_depth",
		tweak_uint, &mgt_param.max_esi_depth, 0, UINT_MAX,
		"Maximum depth of esi:include processing.\n",
		0,
		"5", "levels" },
	{ "connect_timeout", tweak_timeout_double,
		&mgt_param.connect_timeout,0, UINT_MAX,
		"Default connection timeout for backend connections. "
		"We only try to connect to the backend for this many "
		"seconds before giving up. "
		"VCL can override this default value for each backend and "
		"backend request.",
		0,
		"0.7", "s" },
	{ "first_byte_timeout", tweak_timeout_double,
		&mgt_param.first_byte_timeout,0, UINT_MAX,
		"Default timeout for receiving first byte from backend. "
		"We only wait for this many seconds for the first "
		"byte before giving up. A value of 0 means it will never time "
		"out. "
		"VCL can override this default value for each backend and "
		"backend request. This parameter does not apply to pipe.",
		0,
		"60", "s" },
	{ "between_bytes_timeout", tweak_timeout_double,
		&mgt_param.between_bytes_timeout,0, UINT_MAX,
		"Default timeout between bytes when receiving data from "
		"backend. "
		"We only wait for this many seconds between bytes "
		"before giving up. A value of 0 means it will never time out. "
		"VCL can override this default value for each backend request "
		"and backend request. This parameter does not apply to pipe.",
		0,
		"60", "s" },
	{ "acceptor_sleep_max", tweak_timeout_double,
		&mgt_param.acceptor_sleep_max, 0,  10,
		"If we run out of resources, such as file descriptors or "
		"worker threads, the acceptor will sleep between accepts.\n"
		"This parameter limits how long it can sleep between "
		"attempts to accept new connections.",
		EXPERIMENTAL,
		"0.050", "s" },
	{ "acceptor_sleep_incr", tweak_timeout_double,
		&mgt_param.acceptor_sleep_incr, 0,  1,
		"If we run out of resources, such as file descriptors or "
		"worker threads, the acceptor will sleep between accepts.\n"
		"This parameter control how much longer we sleep, each time "
		"we fail to accept a new connection.",
		EXPERIMENTAL,
		"0.001", "s" },
	{ "acceptor_sleep_decay", tweak_generic_double,
		&mgt_param.acceptor_sleep_decay, 0,  1,
		"If we run out of resources, such as file descriptors or "
		"worker threads, the acceptor will sleep between accepts.\n"
		"This parameter (multiplicatively) reduce the sleep duration "
		"for each succesfull accept. (ie: 0.9 = reduce by 10%)",
		EXPERIMENTAL,
		"0.900", "" },
	{ "clock_skew", tweak_uint, &mgt_param.clock_skew, 0, UINT_MAX,
		"How much clockskew we are willing to accept between the "
		"backend and our own clock.",
		0,
		"10", "s" },
	{ "prefer_ipv6", tweak_bool, &mgt_param.prefer_ipv6, 0, 0,
		"Prefer IPv6 address when connecting to backends which "
		"have both IPv4 and IPv6 addresses.",
		0,
		"off", "bool" },
	{ "session_max", tweak_uint,
		&mgt_param.max_sess, 1000, UINT_MAX,
		"Maximum number of sessions we will allocate from one pool "
		"before just dropping connections.\n"
		"This is mostly an anti-DoS measure, and setting it plenty "
		"high should not hurt, as long as you have the memory for "
		"it.\n",
		0,
		"100000", "sessions" },
	{ "timeout_linger", tweak_timeout_double, &mgt_param.timeout_linger,
		0, UINT_MAX,
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
	{ "log_local_address", tweak_bool, &mgt_param.log_local_addr, 0, 0,
		"Log the local address on the TCP connection in the "
		"SessionOpen VSL record.\n"
		"Disabling this saves a getsockname(2) system call "
		"per TCP connection.\n",
		0,
		"on", "bool" },
	{ "waiter", tweak_waiter, NULL, 0, 0,
		"Select the waiter kernel interface.\n",
		WIZARD | MUST_RESTART,
		WAITER_DEFAULT, NULL },
	{ "ban_dups", tweak_bool, &mgt_param.ban_dups, 0, 0,
		"Detect and eliminate duplicate bans.\n",
		0,
		"on", "bool" },
	{ "syslog_cli_traffic", tweak_bool, &mgt_param.syslog_cli_traffic, 0, 0,
		"Log all CLI traffic to syslog(LOG_INFO).\n",
		0,
		"on", "bool" },
	{ "ban_lurker_sleep", tweak_timeout_double,
		&mgt_param.ban_lurker_sleep, 0, UINT_MAX,
		"How long time does the ban lurker thread sleeps between "
		"successful attempts to push the last item up the ban "
		" list.  It always sleeps a second when nothing can be done.\n"
		"A value of zero disables the ban lurker.",
		0,
		"0.01", "s" },
	{ "saintmode_threshold", tweak_uint,
		&mgt_param.saintmode_threshold, 0, UINT_MAX,
		"The maximum number of objects held off by saint mode before "
		"no further will be made to the backend until one times out.  "
		"A value of 0 disables saintmode.",
		EXPERIMENTAL,
		"10", "objects" },
	{ "http_range_support", tweak_bool, &mgt_param.http_range_support, 0, 0,
		"Enable support for HTTP Range headers.\n",
		0,
		"on", "bool" },
	{ "http_gzip_support", tweak_bool, &mgt_param.http_gzip_support, 0, 0,
		"Enable gzip support. When enabled Varnish will compress "
		"uncompressed objects before they are stored in the cache. "
		"If a client does not support gzip encoding Varnish will "
		"uncompress compressed objects on demand. Varnish will also "
		"rewrite the Accept-Encoding header of clients indicating "
		"support for gzip to:\n"
		"  Accept-Encoding: gzip\n\n"
		"Clients that do not support gzip will have their "
		"Accept-Encoding header removed. For more information on how "
		"gzip is implemented please see the chapter on gzip in the "
		"Varnish reference.",
		EXPERIMENTAL,
		"on", "bool" },
	{ "gzip_level", tweak_uint, &mgt_param.gzip_level, 0, 9,
		"Gzip compression level: 0=debug, 1=fast, 9=best",
		0,
		"6", ""},
	{ "gzip_memlevel", tweak_uint, &mgt_param.gzip_memlevel, 1, 9,
		"Gzip memory level 1=slow/least, 9=fast/most compression.\n"
		"Memory impact is 1=1k, 2=2k, ... 9=256k.",
		0,
		"8", ""},
	{ "gzip_buffer",
		tweak_bytes_u, &mgt_param.gzip_buffer,
	        2048, UINT_MAX,
		"Size of malloc buffer used for gzip processing.\n"
		"These buffers are used for in-transit data,"
		" for instance gunzip'ed data being sent to a client."
		"Making this space to small results in more overhead,"
		" writes to sockets etc, making it too big is probably"
		" just a waste of memory.",
		EXPERIMENTAL,
		"32k", "bytes" },
	{ "shortlived", tweak_timeout_double,
		&mgt_param.shortlived, 0, UINT_MAX,
		"Objects created with TTL shorter than this are always "
		"put in transient storage.\n",
		0,
		"10.0", "s" },
	{ "critbit_cooloff", tweak_timeout_double,
		&mgt_param.critbit_cooloff, 60, 254,
		"How long time the critbit hasher keeps deleted objheads "
		"on the cooloff list.\n",
		WIZARD,
		"180.0", "s" },
	{ "vcl_dir", tweak_string, &mgt_vcl_dir, 0, 0,
		"Directory from which relative VCL filenames (vcl.load and "
		"include) are opened.",
		0,
#ifdef VARNISH_VCL_DIR
		VARNISH_VCL_DIR,
#else
		".",
#endif
		NULL },
	{ "vmod_dir", tweak_string, &mgt_vmod_dir, 0, 0,
		"Directory where VCL modules are to be found.",
		0,
#ifdef VARNISH_VMOD_DIR
		VARNISH_VMOD_DIR,
#else
		".",
#endif
		NULL },

	{ "vcc_err_unref", tweak_bool, &mgt_vcc_err_unref, 0, 0,
		"Unreferenced VCL objects result in error.\n",
		0,
		"on", "bool" },

	{ "vcc_allow_inline_c", tweak_bool, &mgt_vcc_allow_inline_c, 0, 0,
		"Allow inline C code in VCL.\n",
		0,
		"on", "bool" },

	{ "vcc_unsafe_path", tweak_bool, &mgt_vcc_unsafe_path, 0, 0,
		"Allow '/' in vmod & include paths.\n"
		"Allow 'import ... from ...'.\n",
		0,
		"on", "bool" },

	{ "pcre_match_limit", tweak_uint,
		&mgt_param.vre_limits.match,
		1, UINT_MAX,
		"The limit for the  number of internal matching function"
		" calls in a pcre_exec() execution.",
		0,
		"10000", ""},

	{ "pcre_match_limit_recursion", tweak_uint,
		&mgt_param.vre_limits.match_recursion,
		1, UINT_MAX,
		"The limit for the  number of internal matching function"
		" recursions in a pcre_exec() execution.",
		0,
		"10000", ""},

	{ "vsl_space", tweak_bytes,
		&mgt_param.vsl_space, 1024*1024, 0,
		"The amount of space to allocate for the VSL fifo buffer"
		" in the VSM memory segment."
		"  If you make this too small, varnish{ncsa|log} etc will"
		" not be able to keep up."
		"  Making it too large just costs memory resources.",
		MUST_RESTART,
		"80M", "bytes"},

	{ "vsm_space", tweak_bytes,
		&mgt_param.vsm_space, 1024*1024, 0,
		"The amount of space to allocate for stats counters"
		" in the VSM memory segment."
		"  If you make this too small, some counters will be"
		" invisible."
		"  Making it too large just costs memory resources.",
		MUST_RESTART,
		"1M", "bytes"},

	{ "busyobj_worker_cache", tweak_bool,
		&mgt_param.bo_cache, 0, 0,
		"Cache free busyobj per worker thread."
		"Disable this if you have very high hitrates and want"
		"to save the memory of one busyobj per worker thread.",
		0,
		"off", "bool"},

	{ "pool_vbc", tweak_poolparam, &mgt_param.vbc_pool, 0, 10000,
		"Parameters for backend connection memory pool.\n"
		MEMPOOL_TEXT,
		0,
		"10,100,10", ""},

	{ "pool_req", tweak_poolparam, &mgt_param.req_pool, 0, 10000,
		"Parameters for per worker pool request memory pool.\n"
		MEMPOOL_TEXT,
		0,
		"10,100,10", ""},
	{ "pool_sess", tweak_poolparam, &mgt_param.sess_pool, 0, 10000,
		"Parameters for per worker pool session memory pool.\n"
		MEMPOOL_TEXT,
		0,
		"10,100,10", ""},
	{ "pool_vbo", tweak_poolparam, &mgt_param.vbo_pool, 0, 10000,
		"Parameters for backend object fetch memory pool.\n"
		MEMPOOL_TEXT,
		0,
		"10,100,10", ""},

	{ "obj_readonly", tweak_bool, &mgt_param.obj_readonly, 0, 0,
		"If set, we do not update obj.hits and obj.lastuse to"
		" avoid dirtying VM pages associated with cached objects.",
		0,
		"false", "bool"},

	{ NULL, NULL, NULL }
};
