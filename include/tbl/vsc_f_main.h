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
 * Definition of all shared memory statistics below.
 *
 * Fields (n, t, l, f, e, d):
 *    n - Name:		Field name, in C-source and stats programs
 *    t - Type:		C-type, uint64_t, unless marked in 'f'
 *    l - Local:	Local counter in worker thread.
 *    f - Format:	Semantics of the value in this field
 *				'a' - Accumulator (deprecated, use 'c')
 *				'b' - Bitmap
 *				'c' - Counter, never decreases.
 *				'g' - Gauge, goes up and down
 *				'i' - Integer (deprecated, use 'g')
 *    e - Explantion:	Short explanation of field (for screen use)
 *    d - Description:	Long explanation of field (for doc use)
 *
 * Please describe Gauge variables as "Number of..." to indicate that
 * this is a snapshot, and Counter variables as "Count of" to indicate
 * accumulative count.
 *
 * -----------------------
 * NB: Cleanup in progress
 * -----------------------
 *
 * Insufficient attention has caused this to become a swamp of conflicting
 * conventions, shorthands and general mumbo-jumbo.  I'm trying to clean
 * it up as I go over the code in other business.
 *
 * Please see the sessmem section for how it should look.
 *
 */

/*---------------------------------------------------------------------
 * Sessions
 *    see: cache_acceptor.c and cache_pool.c
 */

VSC_F(sess_conn,		uint64_t, 1, 'c',
    "Sessions accepted",
	"Count of sessions succesfully accepted"
)
VSC_F(sess_drop,		uint64_t, 1, 'c',
    "Sessions dropped",
	"Count of sessions silently dropped due to lack of session memory."
	"  See parameter 'max_sess'."
)

VSC_F(sess_fail,		uint64_t, 1, 'c',
    "Session accept failures",
	"Count of failures to accept TCP connection."
	"  Either the client changed its mind, or the kernel ran out of"
	" some resource like filedescriptors."
)

/*---------------------------------------------------------------------*/

VSC_F(client_req,		uint64_t, 1, 'a',
    "Client requests received",
	""
)

VSC_F(cache_hit,		uint64_t, 1, 'a',
    "Cache hits",
	"Count of cache hits. "
	"  A cache hit indicates that an object has been delivered to a"
	"  client without fetching it from a backend server."
)

VSC_F(cache_hitpass,		uint64_t, 1, 'a',
    "Cache hits for pass",
	"Count of hits for pass"
	"  A cache hit for pass indicates that Varnish is going to"
	"  pass the request to the backend and this decision has been "
	"  cached in it self. This counts how many times the cached "
	"  decision is being used."
)
VSC_F(cache_miss,		uint64_t, 1, 'a',
    "Cache misses",
	"Count of misses"
	"  A cache miss indicates the object was fetched from the"
	"  backend before delivering it to the backend."
)

VSC_F(backend_conn,		uint64_t, 0, 'a',
    "Backend conn. success",
	""
)

VSC_F(backend_unhealthy,	uint64_t, 0, 'a',
    "Backend conn. not attempted",
	""
)
VSC_F(backend_busy,		uint64_t, 0, 'a',
    "Backend conn. too many",
	""
)
VSC_F(backend_fail,		uint64_t, 0, 'a',
    "Backend conn. failures",
	""
)
VSC_F(backend_reuse,		uint64_t, 0, 'a',
    "Backend conn. reuses",
	"Count of backend connection reuses"
	"  This counter is increased whenever we reuse a recycled connection."
)
VSC_F(backend_toolate,		uint64_t, 0, 'a',
    "Backend conn. was closed",
	""
)
VSC_F(backend_recycle,		uint64_t, 0, 'a',
    "Backend conn. recycles",
	"Count of backend connection recycles"
	"  This counter is increased whenever we have a keep-alive"
	"  connection that is put back into the pool of connections."
	"  It has not yet been used, but it might be, unless the backend"
	"  closes it."
)
VSC_F(backend_retry,		uint64_t, 0, 'a',
    "Backend conn. retry",
	""
)

VSC_F(fetch_head,		uint64_t, 1, 'a',
    "Fetch head",
	""
)
VSC_F(fetch_length,		uint64_t, 1, 'a',
    "Fetch with Length",
	""
)
VSC_F(fetch_chunked,		uint64_t, 1, 'a',
    "Fetch chunked",
	""
)
VSC_F(fetch_eof,		uint64_t, 1, 'a',
    "Fetch EOF",
	""
)
VSC_F(fetch_bad,		uint64_t, 1, 'a',
    "Fetch had bad headers",
	""
)
VSC_F(fetch_close,		uint64_t, 1, 'a',
    "Fetch wanted close",
	""
)
VSC_F(fetch_oldhttp,		uint64_t, 1, 'a',
    "Fetch pre HTTP/1.1 closed",
	""
)
VSC_F(fetch_zero,		uint64_t, 1, 'a',
    "Fetch zero len",
	""
)
VSC_F(fetch_failed,		uint64_t, 1, 'a',
    "Fetch failed",
	""
)
VSC_F(fetch_1xx,		uint64_t, 1, 'a',
    "Fetch no body (1xx)",
	""
)
VSC_F(fetch_204,		uint64_t, 1, 'a',
    "Fetch no body (204)",
	""
)
VSC_F(fetch_304,		uint64_t, 1, 'a',
    "Fetch no body (304)",
	""
)
VSC_F(fetch_not_validated,	uint64_t, 1, 'c',
    "Non-validating responses",
        "Count of backend responses to conditional requests with status != 304"
)

/*---------------------------------------------------------------------
 * Pools, threads, and sessions
 *    see: cache_pool.c
 *
 */

VSC_F(pools,			uint64_t, 0, 'g',
    "Number of thread pools",
	"Number of thread pools.  See also param wthread_pools."
	"  NB: Presently pools cannot be removed once created."
)

VSC_F(threads,			uint64_t, 0, 'g',
    "Total number of threads",
	"Number of threads in all pools."
	"  See also params thread_pools, thread_pool_min & thread_pool_max."
)

VSC_F(threads_limited,		uint64_t, 0, 'c',
    "Threads hit max",
	"Number of times more threads were needed, but limit was reached"
	" in a thread pool."
	"  See also param thread_pool_max."
)

VSC_F(threads_created,		uint64_t, 0, 'c',
    "Threads created",
	"Total number of threads created in all pools."
)

VSC_F(threads_destroyed,	uint64_t, 0, 'c',
    "Threads destoryed",
	"Total number of threads destroyed in all pools."
)

VSC_F(threads_failed,		uint64_t, 0, 'c',
    "Thread creation failed",
	"Number of times creating a thread failed."
	"  See VSL::Debug for diagnostics."
	"  See also param thread_fail_delay."
)

VSC_F(thread_queue_len,		uint64_t, 0, 'g',
    "Length of session queue",
	"Length of session queue waiting for threads."
	"  NB: Only updates once per second."
	"  See also param queue_max."
)

VSC_F(sess_queued,		uint64_t, 0, 'c',
    "Sessions queued for thread",
	"Number of times session was queued waiting for a thread."
	"  See also param queue_max."
)

VSC_F(sess_dropped,		uint64_t, 0, 'c',
    "Sessions dropped for thread",
	"Number of times session was dropped because the queue were too"
	" long already."
	"  See also param queue_max."
)

/*---------------------------------------------------------------------*/

VSC_F(n_object,			uint64_t, 1, 'i',
    "N struct object",
	""
)
VSC_F(n_vampireobject,		uint64_t, 1, 'i',
    "N unresurrected objects",
	""
)
VSC_F(n_objectcore,		uint64_t, 1, 'i',
    "N struct objectcore",
	""
)
VSC_F(n_objecthead,		uint64_t, 1, 'i',
    "N struct objecthead",
	""
)
VSC_F(n_waitinglist,		uint64_t, 1, 'i',
    "N struct waitinglist",
	""
)

VSC_F(n_backend,		uint64_t, 0, 'i',
    "N backends",
	""
)

VSC_F(n_expired,		uint64_t, 0, 'i',
    "N expired objects",
	""
)
VSC_F(n_lru_nuked,		uint64_t, 0, 'i',
    "N LRU nuked objects",
	""
)
VSC_F(n_lru_moved,		uint64_t, 0, 'i',
    "N LRU moved objects",
	""
)

VSC_F(losthdr,			uint64_t, 0, 'a',
    "HTTP header overflows",
	""
)

VSC_F(n_objsendfile,		uint64_t, 0, 'a',
    "Objects sent with sendfile",
	"The number of objects sent with the sendfile system call. If enabled "
	"sendfile will be used on object larger than a certain size."
)
VSC_F(n_objwrite,		uint64_t, 0, 'a',
    "Objects sent with write",
	"The number of objects sent with regular write calls."
	"Writes are used when the objects are too small for sendfile "
	"or if the sendfile call has been disabled"
)
VSC_F(n_objoverflow,		uint64_t, 1, 'a',
    "Objects overflowing workspace",
	""
)

VSC_F(s_sess,			uint64_t, 1, 'a',
    "Total Sessions",
	""
)
VSC_F(s_req,			uint64_t, 1, 'a',
    "Total Requests",
	""
)
VSC_F(s_pipe,			uint64_t, 1, 'a',
    "Total pipe",
	""
)
VSC_F(s_pass,			uint64_t, 1, 'a',
    "Total pass",
	""
)
VSC_F(s_fetch,			uint64_t, 1, 'a',
    "Total fetch",
	""
)
VSC_F(s_hdrbytes,		uint64_t, 1, 'a',
    "Total header bytes",
	""
)
VSC_F(s_bodybytes,		uint64_t, 1, 'a',
    "Total body bytes",
	""
)

VSC_F(sess_closed,		uint64_t, 1, 'a',
    "Session Closed",
	""
)
VSC_F(sess_pipeline,		uint64_t, 1, 'a',
    "Session Pipeline",
	""
)
VSC_F(sess_readahead,		uint64_t, 1, 'a',
    "Session Read Ahead",
	""
)
VSC_F(sess_herd,		uint64_t, 1, 'a',
    "Session herd",
	""
)

VSC_F(shm_records,		uint64_t, 0, 'a',
    "SHM records",
	""
)
VSC_F(shm_writes,		uint64_t, 0, 'a',
    "SHM writes",
	""
)
VSC_F(shm_flushes,		uint64_t, 0, 'a',
    "SHM flushes due to overflow",
	""
)
VSC_F(shm_cont,			uint64_t, 0, 'a',
    "SHM MTX contention",
	""
)
VSC_F(shm_cycles,		uint64_t, 0, 'a',
    "SHM cycles through buffer",
	""
)

VSC_F(sms_nreq,			uint64_t, 0, 'a',
    "SMS allocator requests",
	""
)
VSC_F(sms_nobj,			uint64_t, 0, 'i',
    "SMS outstanding allocations",
	""
)
VSC_F(sms_nbytes,		uint64_t, 0, 'i',
    "SMS outstanding bytes",
	""
)
VSC_F(sms_balloc,		uint64_t, 0, 'i',
    "SMS bytes allocated",
	""
)
VSC_F(sms_bfree,		uint64_t, 0, 'i',
    "SMS bytes freed",
	""
)

VSC_F(backend_req,		uint64_t, 0, 'a',
    "Backend requests made",
	""
)

VSC_F(n_vcl,			uint64_t, 0, 'a',
    "N vcl total",
	""
)
VSC_F(n_vcl_avail,		uint64_t, 0, 'a',
    "N vcl available",
	""
)
VSC_F(n_vcl_discard,		uint64_t, 0, 'a',
    "N vcl discarded",
	""
)

/**********************************************************************/

VSC_F(bans,			uint64_t, 0, 'g',
   "Count of bans",
	"Number of all bans in system, including bans superseded"
	" by newer bans and bans already checked by the ban-lurker."
)
VSC_F(bans_gone,		uint64_t, 0, 'g',
    "Number of bans marked 'gone'",
	"Number of bans which are no longer active, either because they"
	" got checked by the ban-lurker or superseded by newer identical bans."
)
VSC_F(bans_req,			uint64_t, 0, 'g',
    "Number of bans using req.*",
	"Number of bans which use req.* variables.  These bans can not"
	" be washed by the ban-lurker."
)
VSC_F(bans_added,		uint64_t, 0, 'c',
    "Bans added",
	"Counter of bans added to ban list."
)
VSC_F(bans_deleted,		uint64_t, 0, 'c',
    "Bans deleted",
	"Counter of bans deleted from ban list."
)

VSC_F(bans_tested,		uint64_t, 0, 'c',
    "Bans tested against objects",
	"Count of how many bans and objects have been tested against"
	" each other."
)
VSC_F(bans_tests_tested,	uint64_t, 0, 'c',
    "Ban tests tested against objects",
	"Count of how many tests and objects have been tested against"
	" each other.  'ban req.url == foo && req.http.host == bar'"
	" counts as one in 'bans_tested' and as two in 'bans_tests_tested'"
)
VSC_F(bans_dups,		uint64_t, 0, 'c',
    "Bans superseded by other bans",
	"Count of bans replaced by later identical bans."
)

/**********************************************************************/

VSC_F(hcb_nolock,		uint64_t, 0, 'a',
    "HCB Lookups without lock",
	""
)
VSC_F(hcb_lock,			uint64_t, 0, 'a',
    "HCB Lookups with lock",
	""
)
VSC_F(hcb_insert,		uint64_t, 0, 'a',
    "HCB Inserts",
	""
)

VSC_F(esi_errors,		uint64_t, 0, 'a',
    "ESI parse errors (unlock)",
	""
)
VSC_F(esi_warnings,		uint64_t, 0, 'a',
    "ESI parse warnings (unlock)",
	""
)
VSC_F(client_drop_late,		uint64_t, 0, 'a',
    "Connection dropped late",
	""
)
VSC_F(uptime,			uint64_t, 0, 'a',
    "Client uptime",
	""
)

VSC_F(dir_dns_lookups,		uint64_t, 0, 'a',
    "DNS director lookups",
	""
)
VSC_F(dir_dns_failed,		uint64_t, 0, 'a',
    "DNS director failed lookups",
	""
)
VSC_F(dir_dns_hit,		uint64_t, 0, 'a',
    "DNS director cached lookups hit",
	""
)
VSC_F(dir_dns_cache_full,	uint64_t, 0, 'a',
    "DNS director full dnscache",
	""
)

VSC_F(vmods,			uint64_t, 0, 'i',
    "Loaded VMODs",
	""
)

VSC_F(n_gzip,			uint64_t, 0, 'a',
    "Gzip operations",
	""
)
VSC_F(n_gunzip,			uint64_t, 0, 'a',
    "Gunzip operations",
	""
)

/**********************************************************************/

VSC_F(vsm_free,			uint64_t, 0, 'g',
    "Free VSM space",
	"Number of bytes free in the shared memory used to communicate"
	" with tools like varnishstat, varnishlog etc."
)

VSC_F(vsm_used,			uint64_t, 0, 'g',
    "Used VSM space",
	"Number of bytes used in the shared memory used to communicate"
	" with tools like varnishstat, varnishlog etc."
)

VSC_F(vsm_cooling,		uint64_t, 0, 'g',
    "Cooling VSM space",
	"Number of bytes which will soon (max 1 minute) be freed"
	" in the shared memory used to communicate"
	" with tools like varnishstat, varnishlog etc."
)

VSC_F(vsm_overflow,		uint64_t, 0, 'g',
    "Overflow VSM space",
	"Number of bytes which does not fit"
	" in the shared memory used to communicate"
	" with tools like varnishstat, varnishlog etc."
)

VSC_F(vsm_overflowed,		uint64_t, 0, 'c',
    "Overflowed VSM space",
	"Total number of bytes which did not fit"
	" in the shared memory used to communicate"
	" with tools like varnishstat, varnishlog etc."
)
