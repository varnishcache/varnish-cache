/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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
 * $Id$
 */

MAC_STAT(client_conn,		uint64_t, 'a', "Client connections accepted")
MAC_STAT(client_req,		uint64_t, 'a', "Client requests received")

MAC_STAT(cache_hit,		uint64_t, 'a', "Cache hits")
MAC_STAT(cache_hitpass,		uint64_t, 'a', "Cache hits for pass")
MAC_STAT(cache_miss,		uint64_t, 'a', "Cache misses")

MAC_STAT(backend_conn,		uint64_t, 'a', "Backend connections success")
MAC_STAT(backend_fail,		uint64_t, 'a', "Backend connections failures")
MAC_STAT(backend_reuse,		uint64_t, 'a', "Backend connections reuses")
MAC_STAT(backend_recycle,	uint64_t, 'a', "Backend connections recycles")
MAC_STAT(backend_unused,	uint64_t, 'a', "Backend connections unused")

MAC_STAT(n_srcaddr,		uint64_t, 'i', "N struct srcaddr")
MAC_STAT(n_srcaddr_act,		uint64_t, 'i', "N active struct srcaddr")
MAC_STAT(n_sess_mem,		uint64_t, 'i', "N struct sess_mem")
MAC_STAT(n_sess,		uint64_t, 'i', "N struct sess")
MAC_STAT(n_object,		uint64_t, 'i', "N struct object")
MAC_STAT(n_objecthead,		uint64_t, 'i', "N struct objecthead")
MAC_STAT(n_smf,			uint64_t, 'i', "N struct smf")
MAC_STAT(n_smf_frag,		uint64_t, 'i', "N small free smf")
MAC_STAT(n_smf_large,		uint64_t, 'i', "N large free smf")
MAC_STAT(n_vbe_conn,		uint64_t, 'i', "N struct vbe_conn")
MAC_STAT(n_bereq,		uint64_t, 'i', "N struct bereq")
MAC_STAT(n_wrk,			uint64_t, 'i', "N worker threads")
MAC_STAT(n_wrk_create,		uint64_t, 'a', "N worker threads created")
MAC_STAT(n_wrk_failed,		uint64_t, 'a', "N worker threads not created")
MAC_STAT(n_wrk_max,		uint64_t, 'a', "N worker threads limited")
MAC_STAT(n_wrk_queue,		uint64_t, 'a', "N queued work requests")
MAC_STAT(n_wrk_overflow,	uint64_t, 'a', "N overflowed work requests")
MAC_STAT(n_wrk_drop,		uint64_t, 'a', "N dropped work requests")
MAC_STAT(n_backend,		uint64_t, 'i', "N backends")

MAC_STAT(n_expired,		uint64_t, 'i', "N expired objects")
MAC_STAT(n_lru_nuked,		uint64_t, 'i', "N LRU nuked objects")
MAC_STAT(n_lru_saved,		uint64_t, 'i', "N LRU saved objects")
MAC_STAT(n_lru_moved,		uint64_t, 'i', "N LRU moved objects")
MAC_STAT(n_deathrow,		uint64_t, 'i', "N objects on deathrow")

MAC_STAT(losthdr,		uint64_t, 'a', "HTTP header overflows")

MAC_STAT(n_objsendfile,		uint64_t, 'a', "Objects sent with sendfile")
MAC_STAT(n_objwrite,		uint64_t, 'a', "Objects sent with write")
MAC_STAT(n_objoverflow,		uint64_t, 'a', "Objects overflowing workspace")

MAC_STAT(s_sess,		uint64_t, 'a', "Total Sessions")
MAC_STAT(s_req,			uint64_t, 'a', "Total Requests")
MAC_STAT(s_pipe,		uint64_t, 'a', "Total pipe")
MAC_STAT(s_pass,		uint64_t, 'a', "Total pass")
MAC_STAT(s_fetch,		uint64_t, 'a', "Total fetch")
MAC_STAT(s_hdrbytes,		uint64_t, 'a', "Total header bytes")
MAC_STAT(s_bodybytes,		uint64_t, 'a', "Total body bytes")

MAC_STAT(sess_closed,		uint64_t, 'a', "Session Closed")
MAC_STAT(sess_pipeline,		uint64_t, 'a', "Session Pipeline")
MAC_STAT(sess_readahead,	uint64_t, 'a', "Session Read Ahead")
MAC_STAT(sess_linger,		uint64_t, 'a', "Session Linger")
MAC_STAT(sess_herd,		uint64_t, 'a', "Session herd")

MAC_STAT(shm_records,		uint64_t, 'a', "SHM records")
MAC_STAT(shm_writes,		uint64_t, 'a', "SHM writes")
MAC_STAT(shm_flushes,		uint64_t, 'a', "SHM flushes due to overflow")
MAC_STAT(shm_cont,		uint64_t, 'a', "SHM MTX contention")

MAC_STAT(sm_nreq,		uint64_t, 'a', "allocator requests")
MAC_STAT(sm_nobj,		uint64_t, 'i', "outstanding allocations")
MAC_STAT(sm_balloc,		uint64_t, 'i', "bytes allocated")
MAC_STAT(sm_bfree,		uint64_t, 'i', "bytes free")

MAC_STAT(sma_nreq,		uint64_t, 'a', "SMA allocator requests")
MAC_STAT(sma_nobj,		uint64_t, 'i', "SMA outstanding allocations")
MAC_STAT(sma_nbytes,		uint64_t, 'i', "SMA outstanding bytes")
MAC_STAT(sma_balloc,		uint64_t, 'i', "SMA bytes allocated")
MAC_STAT(sma_bfree,		uint64_t, 'i', "SMA bytes free")

MAC_STAT(backend_req,		uint64_t, 'a', "Backend requests made")

MAC_STAT(n_vcl,			uint64_t, 'a', "N vcl total")
MAC_STAT(n_vcl_avail,		uint64_t, 'a', "N vcl available")
MAC_STAT(n_vcl_discard,		uint64_t, 'a', "N vcl discarded")
