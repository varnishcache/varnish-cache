/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 * Definition of the main shared memory statistics below.
 *
 * See include/tbl/vsc_fields.h for the table definition.
 */

/*lint -save -e525 -e539 */

/*--------------------------------------------------------------------
 * Globals, not related to traffic
 */

VSC_FF(uptime,			uint64_t, 0, 'c', 'd', info,
    "Child process uptime",
	"How long the child process has been running."
)

/*---------------------------------------------------------------------
 * Sessions
 */

VSC_FF(sess_conn,		uint64_t, 1, 'c', 'i', info,
    "Sessions accepted",
	"Count of sessions successfully accepted"
)

VSC_FF(sess_drop,		uint64_t, 1, 'c', 'i', info,
    "Sessions dropped",
	"Count of sessions silently dropped due to lack of worker thread."
)

VSC_FF(sess_fail,		uint64_t, 1, 'c', 'i', info,
    "Session accept failures",
	"Count of failures to accept TCP connection."
	" Either the client changed its mind, or the kernel ran out of"
	" some resource like file descriptors."
)

/*---------------------------------------------------------------------*/

VSC_FF(client_req_400,		uint64_t, 1, 'c', 'i', info,
    "Client requests received, subject to 400 errors",
	"400 means we couldn't make sense of the request, it was"
	" malformed in some drastic way."
)

VSC_FF(client_req_417,		uint64_t, 1, 'c', 'i', info,
    "Client requests received, subject to 417 errors",
	"417 means that something went wrong with an Expect: header."
)

VSC_FF(client_req,		uint64_t, 1, 'c', 'i', info,
    "Good client requests received",
	"The count of parseable client requests seen."
)

/*---------------------------------------------------------------------*/

VSC_FF(cache_hit,		uint64_t, 1, 'c', 'i', info,
    "Cache hits",
	"Count of cache hits. "
	" A cache hit indicates that an object has been delivered to a"
	" client without fetching it from a backend server."
)

VSC_FF(cache_hitpass,		uint64_t, 1, 'c', 'i', info,
    "Cache hits for pass.",
	"Count of hits for pass."
	" A cache hit for pass indicates that Varnish is going to"
	" pass the request to the backend and this decision has been"
	" cached in it self. This counts how many times the cached"
	" decision is being used."
)

VSC_FF(cache_hitmiss,		uint64_t, 1, 'c', 'i', info,
    "Cache hits for miss.",
	"Count of hits for miss."
	" A cache hit for miss indicates that Varnish is going to"
	" proceed as for a cache miss without request coalescing, and"
	" this decision has been cached. This counts how many times the"
	" cached decision is being used."
)

VSC_FF(cache_miss,		uint64_t, 1, 'c', 'i', info,
    "Cache misses",
	"Count of misses."
	" A cache miss indicates the object was fetched from the"
	" backend before delivering it to the client."
)

/*---------------------------------------------------------------------*/

VSC_FF(backend_conn,		uint64_t, 0, 'c', 'i', info,
    "Backend conn. success",
	"How many backend connections have successfully been"
	" established."
)

VSC_FF(backend_unhealthy,	uint64_t, 0, 'c', 'i', info,
    "Backend conn. not attempted",
	""
)

VSC_FF(backend_busy,		uint64_t, 0, 'c', 'i', info,
    "Backend conn. too many",
	""
)

VSC_FF(backend_fail,		uint64_t, 0, 'c', 'i', info,
    "Backend conn. failures",
	""
)

VSC_FF(backend_reuse,		uint64_t, 0, 'c', 'i', info,
    "Backend conn. reuses",
	"Count of backend connection reuses."
	" This counter is increased whenever we reuse a recycled connection."
)

VSC_FF(backend_recycle,		uint64_t, 0, 'c', 'i', info,
    "Backend conn. recycles",
	"Count of backend connection recycles."
	" This counter is increased whenever we have a keep-alive"
	" connection that is put back into the pool of connections."
	" It has not yet been used, but it might be, unless the backend"
	" closes it."
)

VSC_FF(backend_retry,		uint64_t, 0, 'c', 'i', info,
    "Backend conn. retry",
	""
)

/*---------------------------------------------------------------------
 * Backend fetch statistics
 */

VSC_FF(fetch_head,		uint64_t, 1, 'c', 'i', info,
    "Fetch no body (HEAD)",
	"beresp with no body because the request is HEAD."
)

VSC_FF(fetch_length,		uint64_t, 1, 'c', 'i', info,
    "Fetch with Length",
	"beresp.body with Content-Length."
)

VSC_FF(fetch_chunked,		uint64_t, 1, 'c', 'i', info,
    "Fetch chunked",
	"beresp.body with Chunked."
)

VSC_FF(fetch_eof,		uint64_t, 1, 'c', 'i', info,
    "Fetch EOF",
	"beresp.body with EOF."
)

VSC_FF(fetch_bad,		uint64_t, 1, 'c', 'i', info,
    "Fetch bad T-E",
	"beresp.body length/fetch could not be determined."
)

VSC_FF(fetch_none,		uint64_t, 1, 'c', 'i', info,
    "Fetch no body",
	"beresp.body empty"
)

VSC_FF(fetch_1xx,		uint64_t, 1, 'c', 'i', info,
    "Fetch no body (1xx)",
	"beresp with no body because of 1XX response."
)

VSC_FF(fetch_204,		uint64_t, 1, 'c', 'i', info,
    "Fetch no body (204)",
	"beresp with no body because of 204 response."
)

VSC_FF(fetch_304,		uint64_t, 1, 'c', 'i', info,
    "Fetch no body (304)",
	"beresp with no body because of 304 response."
)

VSC_FF(fetch_failed,		uint64_t, 1, 'c', 'i', info,
    "Fetch failed (all causes)",
	"beresp fetch failed."
)

VSC_FF(fetch_no_thread,		uint64_t, 1, 'c', 'i', info,
    "Fetch failed (no thread)",
	"beresp fetch failed, no thread available."
)

/*---------------------------------------------------------------------
 * Pools, threads, and sessions
 *    see: cache_pool.c
 *
 */

VSC_FF(pools,			uint64_t, 0, 'g', 'i', info,
    "Number of thread pools",
	"Number of thread pools. See also parameter thread_pools."
	" NB: Presently pools cannot be removed once created."
)

VSC_FF(threads,			uint64_t, 0, 'g', 'i', info,
    "Total number of threads",
	"Number of threads in all pools."
	" See also parameters thread_pools, thread_pool_min and"
	" thread_pool_max."
)

VSC_FF(threads_limited,		uint64_t, 0, 'c', 'i', info,
    "Threads hit max",
	"Number of times more threads were needed, but limit was reached"
	" in a thread pool. See also parameter thread_pool_max."
)

VSC_FF(threads_created,		uint64_t, 0, 'c', 'i', info,
    "Threads created",
	"Total number of threads created in all pools."
)

VSC_FF(threads_destroyed,	uint64_t, 0, 'c', 'i', info,
    "Threads destroyed",
	"Total number of threads destroyed in all pools."
)

VSC_FF(threads_failed,		uint64_t, 0, 'c', 'i', info,
    "Thread creation failed",
	"Number of times creating a thread failed."
	" See VSL::Debug for diagnostics."
	" See also parameter thread_fail_delay."
)

VSC_FF(thread_queue_len,		uint64_t, 0, 'g', 'i', info,
    "Length of session queue",
	"Length of session queue waiting for threads."
	" NB: Only updates once per second."
	" See also parameter thread_queue_limit."
)

VSC_FF(busy_sleep,		uint64_t, 1, 'c', 'i', info,
    "Number of requests sent to sleep on busy objhdr",
	"Number of requests sent to sleep without a worker thread because"
	" they found a busy object."
)

VSC_FF(busy_wakeup,		uint64_t, 1, 'c', 'i', info,
    "Number of requests woken after sleep on busy objhdr",
	"Number of requests taken off the busy object sleep list and"
	" rescheduled."
)

VSC_FF(busy_killed,		uint64_t, 1, 'c', 'i', info,
    "Number of requests killed after sleep on busy objhdr",
	"Number of requests killed from the busy object sleep list"
	" due to lack of resources."
)

VSC_FF(sess_queued,		uint64_t, 0, 'c', 'i', info,
    "Sessions queued for thread",
	"Number of times session was queued waiting for a thread."
	" See also parameter thread_queue_limit."
)

VSC_FF(sess_dropped,		uint64_t, 0, 'c', 'i', info,
    "Sessions dropped for thread",
	"Number of times session was dropped because the queue were too"
	" long already. See also parameter thread_queue_limit."
)

/*---------------------------------------------------------------------*/

VSC_FF(n_object,			uint64_t, 1, 'g', 'i', info,
    "object structs made",
	"Approximate number of HTTP objects (headers + body, if present)"
	" in the cache."
)

VSC_FF(n_vampireobject,		uint64_t, 1, 'g', 'i', diag,
    "unresurrected objects",
	"Number of unresurrected objects"
)

VSC_FF(n_objectcore,		uint64_t, 1, 'g', 'i', info,
    "objectcore structs made",
	"Approximate number of object metadata elements in the cache."
	" Each object needs an objectcore, extra objectcores are for"
	" hit-for-miss, hit-for-pass and busy objects."
)

VSC_FF(n_objecthead,		uint64_t, 1, 'g', 'i', info,
    "objecthead structs made",
	"Approximate number of different hash entries in the cache."
)

VSC_FF(n_backend,		uint64_t, 0, 'g', 'i', info,
    "Number of backends",
	"Number of backends known to us."
)

VSC_FF(n_expired,		uint64_t, 0, 'g', 'i', info,
    "Number of expired objects",
	"Number of objects that expired from cache"
	" because of old age."
)

VSC_FF(n_lru_nuked,		uint64_t, 0, 'g', 'i', info,
    "Number of LRU nuked objects",
	"How many objects have been forcefully evicted"
	" from storage to make room for a new object."
)

VSC_FF(n_lru_moved,		uint64_t, 0, 'g', 'i', diag,
    "Number of LRU moved objects",
	"Number of move operations done on the LRU list."
)

VSC_FF(losthdr,			uint64_t, 0, 'c', 'i', info,
    "HTTP header overflows",
	""
)

VSC_FF(s_sess,			uint64_t, 1, 'c', 'i', info,
    "Total sessions seen",
	""
)

VSC_FF(s_req,			uint64_t, 1, 'c', 'i', info,
    "Total requests seen",
	""
)

VSC_FF(s_pipe,			uint64_t, 1, 'c', 'i', info,
    "Total pipe sessions seen",
	""
)

VSC_FF(s_pass,			uint64_t, 1, 'c', 'i', info,
    "Total pass-ed requests seen",
	""
)

VSC_FF(s_fetch,			uint64_t, 1, 'c', 'i', info,
    "Total backend fetches initiated",
	""
)

VSC_FF(s_synth,			uint64_t, 1, 'c', 'i', info,
    "Total synthethic responses made",
	""
)

VSC_FF(s_req_hdrbytes,		uint64_t, 1, 'c', 'B', info,
    "Request header bytes",
	"Total request header bytes received"
)

VSC_FF(s_req_bodybytes,		uint64_t, 1, 'c', 'B', info,
    "Request body bytes",
	"Total request body bytes received"
)

VSC_FF(s_resp_hdrbytes,		uint64_t, 1, 'c', 'B', info,
    "Response header bytes",
	"Total response header bytes transmitted"
)

VSC_FF(s_resp_bodybytes,		uint64_t, 1, 'c', 'B', info,
    "Response body bytes",
	"Total response body bytes transmitted"
)

VSC_FF(s_pipe_hdrbytes,		uint64_t, 0, 'c', 'B', info,
    "Pipe request header bytes",
	"Total request bytes received for piped sessions"
)

VSC_FF(s_pipe_in,		uint64_t, 0, 'c', 'B', info,
    "Piped bytes from client",
	"Total number of bytes forwarded from clients in"
	" pipe sessions"
)

VSC_FF(s_pipe_out,		uint64_t, 0, 'c', 'B', info,
    "Piped bytes to client",
	"Total number of bytes forwarded to clients in"
	" pipe sessions"
)

VSC_FF(sess_closed,		uint64_t, 1, 'c', 'i', info,
    "Session Closed",
	""
)

VSC_FF(sess_closed_err,		uint64_t, 0, 'c', 'i', info,
    "Session Closed with error",
	"Total number of sessions closed with errors."
	" See sc_* diag counters for detailed breakdown"
)

VSC_FF(sess_readahead,		uint64_t, 1, 'c', 'i', info,
    "Session Read Ahead",
	""
)

VSC_FF(sess_herd,		uint64_t, 1, 'c', 'i', diag,
    "Session herd",
	"Number of times the timeout_linger triggered"
)

#define SESS_CLOSE_ERR0 "OK  "
#define SESS_CLOSE_ERR1 "Err "
#define SESS_CLOSE_ERROR0 ""
#define SESS_CLOSE_ERROR1 "Error "
#define SESS_CLOSE(r, f, e, s)					\
VSC_FF(sc_ ## f, uint64_t, 0, 'c', 'i', diag,			\
    "Session " SESS_CLOSE_ERR ## e #r,				\
	"Number of session closes with "			\
	SESS_CLOSE_ERROR ## e #r " (" s ")"			\
)
#include "tbl/sess_close.h"
#undef SESS_CLOSE_ERROR1
#undef SESS_CLOSE_ERROR0
#undef SESS_CLOSE_ERR1
#undef SESS_CLOSE_ERR0
#undef SESS_CLOSE

/*--------------------------------------------------------------------*/

VSC_FF(shm_records,		uint64_t, 0, 'c', 'i', diag,
    "SHM records",
	""
)

VSC_FF(shm_writes,		uint64_t, 0, 'c', 'i', diag,
    "SHM writes",
	""
)

VSC_FF(shm_flushes,		uint64_t, 0, 'c', 'i', diag,
    "SHM flushes due to overflow",
	""
)

VSC_FF(shm_cont,			uint64_t, 0, 'c', 'i', diag,
    "SHM MTX contention",
	""
)

VSC_FF(shm_cycles,		uint64_t, 0, 'c', 'i', diag,
    "SHM cycles through buffer",
	""
)

/*--------------------------------------------------------------------*/

VSC_FF(backend_req,		uint64_t, 0, 'c', 'i', info,
    "Backend requests made",
	""
)

/*--------------------------------------------------------------------*/

VSC_FF(n_vcl,			uint64_t, 0, 'g', 'i', info,
    "Number of loaded VCLs in total",
	""
)

VSC_FF(n_vcl_avail,		uint64_t, 0, 'g', 'i', diag,
    "Number of VCLs available",
	""
)

VSC_FF(n_vcl_discard,		uint64_t, 0, 'g', 'i', diag,
    "Number of discarded VCLs",
	""
)

VSC_FF(vcl_fail,		uint64_t, 1, 'c', 'i', info,
    "VCL failures",
	"Count of failures which prevented VCL from completing."
)

/*--------------------------------------------------------------------*/

VSC_FF(bans,			uint64_t, 0, 'g', 'i', info,
   "Count of bans",
	"Number of all bans in system, including bans superseded"
	" by newer bans and bans already checked by the ban-lurker."
)

VSC_FF(bans_completed,		uint64_t, 0, 'g', 'i', diag,
    "Number of bans marked 'completed'",
	"Number of bans which are no longer active, either because they"
	" got checked by the ban-lurker or superseded by newer identical bans."
)

VSC_FF(bans_obj,			uint64_t, 0, 'g', 'i', diag,
    "Number of bans using obj.*",
	"Number of bans which use obj.* variables.  These bans can possibly"
	" be washed by the ban-lurker."
)

VSC_FF(bans_req,			uint64_t, 0, 'g', 'i', diag,
    "Number of bans using req.*",
	"Number of bans which use req.* variables.  These bans can not"
	" be washed by the ban-lurker."
)

VSC_FF(bans_added,		uint64_t, 0, 'c', 'i', diag,
    "Bans added",
	"Counter of bans added to ban list."
)

VSC_FF(bans_deleted,		uint64_t, 0, 'c', 'i', diag,
    "Bans deleted",
	"Counter of bans deleted from ban list."
)

VSC_FF(bans_tested,		uint64_t, 0, 'c', 'i', diag,
    "Bans tested against objects (lookup)",
	"Count of how many bans and objects have been tested against"
	" each other during hash lookup."
)

VSC_FF(bans_obj_killed,		uint64_t, 0, 'c', 'i', diag,
    "Objects killed by bans (lookup)",
	"Number of objects killed by bans during object lookup."
)

VSC_FF(bans_lurker_tested,	uint64_t, 0, 'c', 'i', diag,
    "Bans tested against objects (lurker)",
	"Count of how many bans and objects have been tested against"
	" each other by the ban-lurker."
)

VSC_FF(bans_tests_tested,	uint64_t, 0, 'c', 'i', diag,
    "Ban tests tested against objects (lookup)",
	"Count of how many tests and objects have been tested against"
	" each other during lookup."
	" 'ban req.url == foo && req.http.host == bar'"
	" counts as one in 'bans_tested' and as two in 'bans_tests_tested'"
)

VSC_FF(bans_lurker_tests_tested,	uint64_t, 0, 'c', 'i', diag,
    "Ban tests tested against objects (lurker)",
	"Count of how many tests and objects have been tested against"
	" each other by the ban-lurker."
	" 'ban req.url == foo && req.http.host == bar'"
	" counts as one in 'bans_tested' and as two in 'bans_tests_tested'"
)

VSC_FF(bans_lurker_obj_killed,	uint64_t, 0, 'c', 'i', diag,
    "Objects killed by bans (lurker)",
	"Number of objects killed by the ban-lurker."
)

VSC_FF(bans_lurker_obj_killed_cutoff,	uint64_t, 0, 'c', 'i', diag,
    "Objects killed by bans for cutoff (lurker)",
	"Number of objects killed by the ban-lurker to keep the number of"
	" bans below ban_cutoff."
)

VSC_FF(bans_dups,		uint64_t, 0, 'c', 'i', diag,
    "Bans superseded by other bans",
	"Count of bans replaced by later identical bans."
)

VSC_FF(bans_lurker_contention,	uint64_t, 0, 'c', 'i', diag,
    "Lurker gave way for lookup",
	"Number of times the ban-lurker had to wait for lookups."
)

VSC_FF(bans_persisted_bytes,	uint64_t, 0, 'g', 'B', diag,
    "Bytes used by the persisted ban lists",
	"Number of bytes used by the persisted ban lists."
)

VSC_FF(bans_persisted_fragmentation, uint64_t, 0, 'g', 'B', diag,
    "Extra bytes in persisted ban lists due to fragmentation",
	"Number of extra bytes accumulated through dropped and"
	" completed bans in the persistent ban lists."
)

/*--------------------------------------------------------------------*/

VSC_FF(n_purges,			uint64_t, 0, 'g', 'i', info,
    "Number of purge operations executed",
	""
)

VSC_FF(n_obj_purged,		uint64_t, 0, 'g', 'i', info,
    "Number of purged objects",
	""
)

/*--------------------------------------------------------------------*/

VSC_FF(exp_mailed,		uint64_t, 0, 'c', 'i', diag,
    "Number of objects mailed to expiry thread",
	"Number of objects mailed to expiry thread for handling."
)

VSC_FF(exp_received,		uint64_t, 0, 'c', 'i', diag,
    "Number of objects received by expiry thread",
	"Number of objects received by expiry thread for handling."
)

/*--------------------------------------------------------------------*/

VSC_FF(hcb_nolock,		uint64_t, 1, 'c', 'i', debug,
    "HCB Lookups without lock",
	""
)

VSC_FF(hcb_lock,			uint64_t, 0, 'c', 'i', debug,
    "HCB Lookups with lock",
	""
)

VSC_FF(hcb_insert,		uint64_t, 0, 'c', 'i', debug,
    "HCB Inserts",
	""
)

/*--------------------------------------------------------------------*/

VSC_FF(esi_errors,		uint64_t, 0, 'c', 'i', diag,
    "ESI parse errors (unlock)",
	""
)

VSC_FF(esi_warnings,		uint64_t, 0, 'c', 'i', diag,
    "ESI parse warnings (unlock)",
	""
)

/*--------------------------------------------------------------------*/

VSC_FF(vmods,			uint64_t, 0, 'g', 'i', info,
    "Loaded VMODs",
	""
)

/*--------------------------------------------------------------------*/

VSC_FF(n_gzip,			uint64_t, 0, 'c', 'i', info,
    "Gzip operations",
	""
)

VSC_FF(n_gunzip,			uint64_t, 0, 'c', 'i', info,
    "Gunzip operations",
	""
)

VSC_FF(n_test_gunzip,			uint64_t, 0, 'c', 'i', info,
    "Test gunzip operations",
	"Those operations occur when Varnish receives a compressed"
	" object from a backend. They are done to verify the gzip"
	" stream while it's inserted in storage."
)

/*--------------------------------------------------------------------*/

VSC_FF(vsm_free,			uint64_t, 0, 'g', 'B', diag,
    "Free VSM space",
	"Number of bytes free in the shared memory used to communicate"
	" with tools like varnishstat, varnishlog etc."
)

VSC_FF(vsm_used,			uint64_t, 0, 'g', 'B', diag,
    "Used VSM space",
	"Number of bytes used in the shared memory used to communicate"
	" with tools like varnishstat, varnishlog etc."
)

VSC_FF(vsm_cooling,		uint64_t, 0, 'g', 'B', debug,
    "Cooling VSM space",
	"Number of bytes which will soon (max 1 minute) be freed"
	" in the shared memory used to communicate"
	" with tools like varnishstat, varnishlog etc."
)

VSC_FF(vsm_overflow,		uint64_t, 0, 'g', 'B', diag,
    "Overflow VSM space",
	"Number of bytes which does not fit"
	" in the shared memory used to communicate"
	" with tools like varnishstat, varnishlog etc."
	" If this counter is not zero, consider"
	" increasing the runtime variable vsm_space."
)

VSC_FF(vsm_overflowed,		uint64_t, 0, 'c', 'B', diag,
    "Overflowed VSM space",
	"Total number of bytes which did not fit"
	" in the shared memory used to communicate"
	" with tools like varnishstat, varnishlog etc."
	" If this counter is not zero, consider"
	" increasing the runtime variable vsm_space."
)

#undef VSC_FF

/*lint -restore */
