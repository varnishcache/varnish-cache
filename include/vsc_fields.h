/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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
 * $Id: vsc_fields.h 4857 2010-05-25 10:47:20Z phk $
 *
 * 3rd argument marks fields for inclusion in the per worker-thread
 * stats structure.
 */

/**********************************************************************/
#ifndef VSC_F_MAIN
#define VSC_F_MAIN(a, b, c, d, e)
#define __VSC_F_MAIN
#endif

VSC_F_MAIN(client_conn,		uint64_t, 0, 'a', "Client connections accepted")
VSC_F_MAIN(client_drop,		uint64_t, 0, 'a',
					"Connection dropped, no sess/wrk")
VSC_F_MAIN(client_req,		uint64_t, 1, 'a', "Client requests received")

VSC_F_MAIN(cache_hit,		uint64_t, 1, 'a', "Cache hits")
VSC_F_MAIN(cache_hitpass,	uint64_t, 1, 'a', "Cache hits for pass")
VSC_F_MAIN(cache_miss,		uint64_t, 1, 'a', "Cache misses")

VSC_F_MAIN(backend_conn,	uint64_t, 0, 'a', "Backend conn. success")
VSC_F_MAIN(backend_unhealthy,	uint64_t, 0, 'a', "Backend conn. not attempted")
VSC_F_MAIN(backend_busy,	uint64_t, 0, 'a', "Backend conn. too many")
VSC_F_MAIN(backend_fail,	uint64_t, 0, 'a', "Backend conn. failures")
VSC_F_MAIN(backend_reuse,	uint64_t, 0, 'a', "Backend conn. reuses")
VSC_F_MAIN(backend_toolate,	uint64_t, 0, 'a', "Backend conn. was closed")
VSC_F_MAIN(backend_recycle,	uint64_t, 0, 'a', "Backend conn. recycles")
VSC_F_MAIN(backend_unused,	uint64_t, 0, 'a', "Backend conn. unused")

VSC_F_MAIN(fetch_head,		uint64_t, 1, 'a', "Fetch head")
VSC_F_MAIN(fetch_length,	uint64_t, 1, 'a', "Fetch with Length")
VSC_F_MAIN(fetch_chunked,	uint64_t, 1, 'a', "Fetch chunked")
VSC_F_MAIN(fetch_eof,		uint64_t, 1, 'a', "Fetch EOF")
VSC_F_MAIN(fetch_bad,		uint64_t, 1, 'a', "Fetch had bad headers")
VSC_F_MAIN(fetch_close,		uint64_t, 1, 'a', "Fetch wanted close")
VSC_F_MAIN(fetch_oldhttp,	uint64_t, 1, 'a', "Fetch pre HTTP/1.1 closed")
VSC_F_MAIN(fetch_zero,		uint64_t, 1, 'a', "Fetch zero len")
VSC_F_MAIN(fetch_failed,	uint64_t, 1, 'a', "Fetch failed")


VSC_F_MAIN(n_sess_mem,		uint64_t, 0, 'i', "N struct sess_mem")
VSC_F_MAIN(n_sess,		uint64_t, 0, 'i', "N struct sess")
VSC_F_MAIN(n_object,		uint64_t, 1, 'i', "N struct object")
VSC_F_MAIN(n_vampireobject,	uint64_t, 1, 'i', "N unresurrected objects")
VSC_F_MAIN(n_objectcore,	uint64_t, 1, 'i', "N struct objectcore")
VSC_F_MAIN(n_objecthead,	uint64_t, 1, 'i', "N struct objecthead")
VSC_F_MAIN(n_smf,		uint64_t, 0, 'i', "N struct smf")
VSC_F_MAIN(n_smf_frag,		uint64_t, 0, 'i', "N small free smf")
VSC_F_MAIN(n_smf_large,		uint64_t, 0, 'i', "N large free smf")
VSC_F_MAIN(n_vbc,		uint64_t, 0, 'i', "N struct vbc")
VSC_F_MAIN(n_wrk,		uint64_t, 0, 'i', "N worker threads")
VSC_F_MAIN(n_wrk_create,	uint64_t, 0, 'a', "N worker threads created")
VSC_F_MAIN(n_wrk_failed,	uint64_t, 0, 'a',
					"N worker threads not created")
VSC_F_MAIN(n_wrk_max,		uint64_t, 0, 'a', "N worker threads limited")
VSC_F_MAIN(n_wrk_queue,		uint64_t, 0, 'a', "N queued work requests")
VSC_F_MAIN(n_wrk_overflow,	uint64_t, 0, 'a', "N overflowed work requests")
VSC_F_MAIN(n_wrk_drop,		uint64_t, 0, 'a', "N dropped work requests")
VSC_F_MAIN(n_backend,		uint64_t, 0, 'i', "N backends")

VSC_F_MAIN(n_expired,		uint64_t, 0, 'i', "N expired objects")
VSC_F_MAIN(n_lru_nuked,		uint64_t, 0, 'i', "N LRU nuked objects")
VSC_F_MAIN(n_lru_saved,		uint64_t, 0, 'i', "N LRU saved objects")
VSC_F_MAIN(n_lru_moved,		uint64_t, 0, 'i', "N LRU moved objects")
VSC_F_MAIN(n_deathrow,		uint64_t, 0, 'i', "N objects on deathrow")

VSC_F_MAIN(losthdr,		uint64_t, 0, 'a', "HTTP header overflows")

VSC_F_MAIN(n_objsendfile,	uint64_t, 0, 'a', "Objects sent with sendfile")
VSC_F_MAIN(n_objwrite,		uint64_t, 0, 'a', "Objects sent with write")
VSC_F_MAIN(n_objoverflow,	uint64_t, 1, 'a',
					"Objects overflowing workspace")

VSC_F_MAIN(s_sess,		uint64_t, 1, 'a', "Total Sessions")
VSC_F_MAIN(s_req,		uint64_t, 1, 'a', "Total Requests")
VSC_F_MAIN(s_pipe,		uint64_t, 1, 'a', "Total pipe")
VSC_F_MAIN(s_pass,		uint64_t, 1, 'a', "Total pass")
VSC_F_MAIN(s_fetch,		uint64_t, 1, 'a', "Total fetch")
VSC_F_MAIN(s_hdrbytes,		uint64_t, 1, 'a', "Total header bytes")
VSC_F_MAIN(s_bodybytes,		uint64_t, 1, 'a', "Total body bytes")

VSC_F_MAIN(sess_closed,		uint64_t, 1, 'a', "Session Closed")
VSC_F_MAIN(sess_pipeline,	uint64_t, 1, 'a', "Session Pipeline")
VSC_F_MAIN(sess_readahead,	uint64_t, 1, 'a', "Session Read Ahead")
VSC_F_MAIN(sess_linger,		uint64_t, 1, 'a', "Session Linger")
VSC_F_MAIN(sess_herd,		uint64_t, 1, 'a', "Session herd")

VSC_F_MAIN(shm_records,		uint64_t, 0, 'a', "SHM records")
VSC_F_MAIN(shm_writes,		uint64_t, 0, 'a', "SHM writes")
VSC_F_MAIN(shm_flushes,		uint64_t, 0, 'a', "SHM flushes due to overflow")
VSC_F_MAIN(shm_cont,		uint64_t, 0, 'a', "SHM MTX contention")
VSC_F_MAIN(shm_cycles,		uint64_t, 0, 'a', "SHM cycles through buffer")

VSC_F_MAIN(sm_nreq,		uint64_t, 0, 'a', "allocator requests")
VSC_F_MAIN(sm_nobj,		uint64_t, 0, 'i', "outstanding allocations")
VSC_F_MAIN(sm_balloc,		uint64_t, 0, 'i', "bytes allocated")
VSC_F_MAIN(sm_bfree,		uint64_t, 0, 'i', "bytes free")

VSC_F_MAIN(sms_nreq,		uint64_t, 0, 'a', "SMS allocator requests")
VSC_F_MAIN(sms_nobj,		uint64_t, 0, 'i', "SMS outstanding allocations")
VSC_F_MAIN(sms_nbytes,		uint64_t, 0, 'i', "SMS outstanding bytes")
VSC_F_MAIN(sms_balloc,		uint64_t, 0, 'i', "SMS bytes allocated")
VSC_F_MAIN(sms_bfree,		uint64_t, 0, 'i', "SMS bytes freed")

VSC_F_MAIN(backend_req,		uint64_t, 0, 'a', "Backend requests made")

VSC_F_MAIN(n_vcl,		uint64_t, 0, 'a', "N vcl total")
VSC_F_MAIN(n_vcl_avail,		uint64_t, 0, 'a', "N vcl available")
VSC_F_MAIN(n_vcl_discard,	uint64_t, 0, 'a', "N vcl discarded")

VSC_F_MAIN(n_purge,		uint64_t, 0, 'i', "N total active purges")
VSC_F_MAIN(n_purge_add,		uint64_t, 0, 'a', "N new purges added")
VSC_F_MAIN(n_purge_retire,	uint64_t, 0, 'a', "N old purges deleted")
VSC_F_MAIN(n_purge_obj_test,	uint64_t, 0, 'a', "N objects tested")
VSC_F_MAIN(n_purge_re_test,	uint64_t, 0, 'a', "N regexps tested against")
VSC_F_MAIN(n_purge_dups,	uint64_t, 0, 'a', "N duplicate purges removed")

VSC_F_MAIN(hcb_nolock,		uint64_t, 0, 'a', "HCB Lookups without lock")
VSC_F_MAIN(hcb_lock,		uint64_t, 0, 'a', "HCB Lookups with lock")
VSC_F_MAIN(hcb_insert,		uint64_t, 0, 'a', "HCB Inserts")

VSC_F_MAIN(esi_parse,		uint64_t, 0, 'a', "Objects ESI parsed (unlock)")
VSC_F_MAIN(esi_errors,		uint64_t, 0, 'a', "ESI parse errors (unlock)")
VSC_F_MAIN(accept_fail,		uint64_t, 0, 'a', "Accept failures")
VSC_F_MAIN(client_drop_late,	uint64_t, 0, 'a', "Connection dropped late")
VSC_F_MAIN(uptime,		uint64_t, 0, 'a', "Client uptime")

VSC_F_MAIN(dir_dns_lookups,	uint64_t, 0, 'a', "DNS director lookups")
VSC_F_MAIN(dir_dns_failed,	uint64_t, 0, 'a', "DNS director failed lookups")
VSC_F_MAIN(dir_dns_hit,		uint64_t, 0, 'a', "DNS director cached lookups hit")
VSC_F_MAIN(dir_dns_cache_full,	uint64_t, 0, 'a', "DNS director full dnscache")


VSC_F_MAIN(critbit_cooler,	uint64_t, 0, 'i', "Objhdr's on cool list")

#ifdef __VSC_F_MAIN
#undef VSC_F_MAIN
#undef __VSC_F_MAIN
#endif

/**********************************************************************/

#ifndef VSC_F_SMA
#define VSC_F_SMA(a, b, c, d, e)
#define __VSC_F_SMA
#endif

VSC_F_SMA(sma_nreq,		uint64_t, 0, 'a', "Allocator requests")
VSC_F_SMA(sma_nobj,		uint64_t, 0, 'i', "Outstanding allocations")
VSC_F_SMA(sma_nbytes,		uint64_t, 0, 'i', "Outstanding bytes")
VSC_F_SMA(sma_balloc,		uint64_t, 0, 'i', "Bytes allocated")
VSC_F_SMA(sma_bfree,		uint64_t, 0, 'i', "Bytes free")

#ifdef __VSC_F_SMA
#undef VSC_F_SMA
#undef __VSC_F_SMA
#endif

/**********************************************************************/

#ifndef VSC_F_VBE
#define VSC_F_VBE(a, b, c, d, e)
#define __VSC_F_VBE
#endif

VSC_F_VBE(vcls,			uint64_t, 0, 'i', "VCL references")

#ifdef __VSC_F_VBE
#undef VSC_F_VBE
#undef __VSC_F_VBE
#endif
