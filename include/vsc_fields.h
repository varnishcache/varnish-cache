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
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION, "")
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * 3rd argument marks fields for inclusion in the per worker-thread
 * stats structure.
 *
 * XXX: We need a much more consistent naming of these fields, this has
 * XXX: turned into a major mess, causing trouble already for backends.
 * XXX:
 * XXX: Please converge on:
 * XXX:		c_* counter	(total bytes ever allocated from sma, "")
 * XXX:		g_* gauge	(presently allocated bytes from sma, "")
 */

/**********************************************************************/

#ifdef VSC_DO_MAIN

VSC_F(client_conn,		uint64_t, 1, 'a', "Client connections accepted", "")
VSC_F(client_drop,		uint64_t, 0, 'a',
					"Connection dropped, no sess/wrk", "")
VSC_F(client_req,		uint64_t, 1, 'a', "Client requests received", "")

VSC_F(cache_hit,		uint64_t, 1, 'a', "Cache hits", "")
VSC_F(cache_hitpass,	uint64_t, 1, 'a', "Cache hits for pass", "")
VSC_F(cache_miss,		uint64_t, 1, 'a', "Cache misses", "")

VSC_F(backend_conn,	uint64_t, 0, 'a', "Backend conn. success", "")
VSC_F(backend_unhealthy,	uint64_t, 0, 'a', "Backend conn. not attempted", "")
VSC_F(backend_busy,	uint64_t, 0, 'a', "Backend conn. too many", "")
VSC_F(backend_fail,	uint64_t, 0, 'a', "Backend conn. failures", "")
VSC_F(backend_reuse,	uint64_t, 0, 'a', "Backend conn. reuses", "")
VSC_F(backend_toolate,	uint64_t, 0, 'a', "Backend conn. was closed", "")
VSC_F(backend_recycle,	uint64_t, 0, 'a', "Backend conn. recycles", "")
VSC_F(backend_retry,	uint64_t, 0, 'a', "Backend conn. retry", "")

VSC_F(fetch_head,		uint64_t, 1, 'a', "Fetch head", "")
VSC_F(fetch_length,		uint64_t, 1, 'a', "Fetch with Length", "")
VSC_F(fetch_chunked,		uint64_t, 1, 'a', "Fetch chunked", "")
VSC_F(fetch_eof,		uint64_t, 1, 'a', "Fetch EOF", "")
VSC_F(fetch_bad,		uint64_t, 1, 'a', "Fetch had bad headers", "")
VSC_F(fetch_close,		uint64_t, 1, 'a', "Fetch wanted close", "")
VSC_F(fetch_oldhttp,		uint64_t, 1, 'a', "Fetch pre HTTP/1.1 closed", "")
VSC_F(fetch_zero,		uint64_t, 1, 'a', "Fetch zero len", "")
VSC_F(fetch_failed,		uint64_t, 1, 'a', "Fetch failed", "")
VSC_F(fetch_1xx,		uint64_t, 1, 'a', "Fetch no body (1xx)", "")
VSC_F(fetch_204,		uint64_t, 1, 'a', "Fetch no body (204)", "")
VSC_F(fetch_304,		uint64_t, 1, 'a', "Fetch no body (304)", "")

/*---------------------------------------------------------------------
 * Session Memory
 *    see: cache_session.c
 */

VSC_F(n_sess_mem,		uint64_t, 0, 'i', "N struct sess_mem", "")
VSC_F(n_sess,			uint64_t, 0, 'i', "N struct sess", "")
VSC_F(n_object,			uint64_t, 1, 'i', "N struct object", "")
VSC_F(n_vampireobject,		uint64_t, 1, 'i', "N unresurrected objects", "")
VSC_F(n_objectcore,		uint64_t, 1, 'i', "N struct objectcore", "")
VSC_F(n_objecthead,		uint64_t, 1, 'i', "N struct objecthead", "")
VSC_F(n_waitinglist,		uint64_t, 1, 'i', "N struct waitinglist", "")

VSC_F(n_vbc,		uint64_t, 0, 'i', "N struct vbc", "")
VSC_F(n_wrk,		uint64_t, 0, 'i', "N worker threads", "")
VSC_F(n_wrk_create,	uint64_t, 0, 'a', "N worker threads created", "")
VSC_F(n_wrk_failed,	uint64_t, 0, 'a',
					"N worker threads not created", "")
VSC_F(n_wrk_max,		uint64_t, 0, 'a', "N worker threads limited", "")
VSC_F(n_wrk_lqueue,		uint64_t, 0, 'a', "work request queue length", "")
VSC_F(n_wrk_queued,		uint64_t, 0, 'a', "N queued work requests", "")
VSC_F(n_wrk_drop,		uint64_t, 0, 'a', "N dropped work requests", "")
VSC_F(n_backend,		uint64_t, 0, 'i', "N backends", "")

VSC_F(n_expired,		uint64_t, 0, 'i', "N expired objects", "")
VSC_F(n_lru_nuked,		uint64_t, 0, 'i', "N LRU nuked objects", "")
VSC_F(n_lru_moved,		uint64_t, 0, 'i', "N LRU moved objects", "")

VSC_F(losthdr,		uint64_t, 0, 'a', "HTTP header overflows", "")

VSC_F(n_objsendfile,	uint64_t, 0, 'a', "Objects sent with sendfile", "")
VSC_F(n_objwrite,		uint64_t, 0, 'a', "Objects sent with write", "")
VSC_F(n_objoverflow,	uint64_t, 1, 'a',
					"Objects overflowing workspace", "")

VSC_F(s_sess,		uint64_t, 1, 'a', "Total Sessions", "")
VSC_F(s_req,		uint64_t, 1, 'a', "Total Requests", "")
VSC_F(s_pipe,		uint64_t, 1, 'a', "Total pipe", "")
VSC_F(s_pass,		uint64_t, 1, 'a', "Total pass", "")
VSC_F(s_fetch,		uint64_t, 1, 'a', "Total fetch", "")
VSC_F(s_hdrbytes,		uint64_t, 1, 'a', "Total header bytes", "")
VSC_F(s_bodybytes,		uint64_t, 1, 'a', "Total body bytes", "")

VSC_F(sess_closed,		uint64_t, 1, 'a', "Session Closed", "")
VSC_F(sess_pipeline,	uint64_t, 1, 'a', "Session Pipeline", "")
VSC_F(sess_readahead,	uint64_t, 1, 'a', "Session Read Ahead", "")
VSC_F(sess_linger,		uint64_t, 1, 'a', "Session Linger", "")
VSC_F(sess_herd,		uint64_t, 1, 'a', "Session herd", "")

VSC_F(shm_records,		uint64_t, 0, 'a', "SHM records", "")
VSC_F(shm_writes,		uint64_t, 0, 'a', "SHM writes", "")
VSC_F(shm_flushes,		uint64_t, 0, 'a', "SHM flushes due to overflow", "")
VSC_F(shm_cont,		uint64_t, 0, 'a', "SHM MTX contention", "")
VSC_F(shm_cycles,		uint64_t, 0, 'a', "SHM cycles through buffer", "")

VSC_F(sms_nreq,		uint64_t, 0, 'a', "SMS allocator requests", "")
VSC_F(sms_nobj,		uint64_t, 0, 'i', "SMS outstanding allocations", "")
VSC_F(sms_nbytes,		uint64_t, 0, 'i', "SMS outstanding bytes", "")
VSC_F(sms_balloc,		uint64_t, 0, 'i', "SMS bytes allocated", "")
VSC_F(sms_bfree,		uint64_t, 0, 'i', "SMS bytes freed", "")

VSC_F(backend_req,		uint64_t, 0, 'a', "Backend requests made", "")

VSC_F(n_vcl,		uint64_t, 0, 'a', "N vcl total", "")
VSC_F(n_vcl_avail,		uint64_t, 0, 'a', "N vcl available", "")
VSC_F(n_vcl_discard,	uint64_t, 0, 'a', "N vcl discarded", "")

VSC_F(n_ban,		uint64_t, 0, 'i', "N total active bans", "")
VSC_F(n_ban_add,		uint64_t, 0, 'a', "N new bans added", "")
VSC_F(n_ban_retire,	uint64_t, 0, 'a', "N old bans deleted", "")
VSC_F(n_ban_obj_test,	uint64_t, 0, 'a', "N objects tested", "")
VSC_F(n_ban_re_test,	uint64_t, 0, 'a', "N regexps tested against", "")
VSC_F(n_ban_dups,	uint64_t, 0, 'a', "N duplicate bans removed", "")

VSC_F(hcb_nolock,		uint64_t, 0, 'a', "HCB Lookups without lock", "")
VSC_F(hcb_lock,		uint64_t, 0, 'a', "HCB Lookups with lock", "")
VSC_F(hcb_insert,		uint64_t, 0, 'a', "HCB Inserts", "")

VSC_F(esi_errors,		uint64_t, 0, 'a', "ESI parse errors (unlock)", "")
VSC_F(esi_warnings,		uint64_t, 0, 'a', "ESI parse warnings (unlock)", "")
VSC_F(accept_fail,		uint64_t, 0, 'a', "Accept failures", "")
VSC_F(client_drop_late,	uint64_t, 0, 'a', "Connection dropped late", "")
VSC_F(uptime,		uint64_t, 0, 'a', "Client uptime", "")

VSC_F(dir_dns_lookups,	uint64_t, 0, 'a', "DNS director lookups", "")
VSC_F(dir_dns_failed,	uint64_t, 0, 'a', "DNS director failed lookups", "")
VSC_F(dir_dns_hit,		uint64_t, 0, 'a', "DNS director cached lookups hit", "")
VSC_F(dir_dns_cache_full,	uint64_t, 0, 'a', "DNS director full dnscache", "")

VSC_F(vmods,		uint64_t, 0, 'i', "Loaded VMODs", "")

VSC_F(n_gzip,			uint64_t, 0, 'a', "Gzip operations", "")
VSC_F(n_gunzip,			uint64_t, 0, 'a', "Gunzip operations", "")

#endif

/**********************************************************************/

#ifdef VSC_DO_LCK

VSC_F(creat,		uint64_t, 0, 'a', "Created locks", "")
VSC_F(destroy,		uint64_t, 0, 'a', "Destroyed locks", "")
VSC_F(locks,		uint64_t, 0, 'a', "Lock Operations", "")
VSC_F(colls,		uint64_t, 0, 'a', "Collisions", "")

#endif

/**********************************************************************
 * All Stevedores support these counters
 */

#if defined(VSC_DO_SMA) || defined (VSC_DO_SMF)
VSC_F(c_req,		uint64_t, 0, 'a', "Allocator requests", "")
VSC_F(c_fail,		uint64_t, 0, 'a', "Allocator failures", "")
VSC_F(c_bytes,		uint64_t, 0, 'a', "Bytes allocated", "")
VSC_F(c_freed,		uint64_t, 0, 'a', "Bytes freed", "")
VSC_F(g_alloc,		uint64_t, 0, 'i', "Allocations outstanding", "")
VSC_F(g_bytes,		uint64_t, 0, 'i', "Bytes outstanding", "")
VSC_F(g_space,		uint64_t, 0, 'i', "Bytes available", "")
#endif


/**********************************************************************/

#ifdef VSC_DO_SMA
/* No SMA specific counters */
#endif

/**********************************************************************/

#ifdef VSC_DO_SMF
VSC_F(g_smf,			uint64_t, 0, 'i', "N struct smf", "")
VSC_F(g_smf_frag,		uint64_t, 0, 'i', "N small free smf", "")
VSC_F(g_smf_large,		uint64_t, 0, 'i', "N large free smf", "")
#endif

/**********************************************************************/

#ifdef VSC_DO_VBE

VSC_F(vcls,			uint64_t, 0, 'i', "VCL references", "")
VSC_F(happy,		uint64_t, 0, 'b', "Happy health probes", "")

#endif

