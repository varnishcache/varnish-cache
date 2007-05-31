/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006 Linpro AS
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

MAC_STAT(client_conn,		uint64_t, "u", "Client connections accepted")
MAC_STAT(client_req,		uint64_t, "u", "Client requests received")

MAC_STAT(cache_hit,		uint64_t, "u", "Cache hits")
MAC_STAT(cache_hitpass,		uint64_t, "u", "Cache hits for pass")
MAC_STAT(cache_miss,		uint64_t, "u", "Cache misses")

MAC_STAT(backend_conn,		uint64_t, "u", "Backend connections success")
MAC_STAT(backend_fail,		uint64_t, "u", "Backend connections failures")
MAC_STAT(backend_reuse,		uint64_t, "u", "Backend connections reuses")
MAC_STAT(backend_recycle,	uint64_t, "u", "Backend connections recycles")
MAC_STAT(backend_unused,	uint64_t, "u", "Backend connections unused")

MAC_STAT(n_srcaddr,		uint64_t, "u", "N struct srcaddr")
MAC_STAT(n_srcaddr_act,		uint64_t, "u", "N active struct srcaddr")
MAC_STAT(n_sess_mem,		uint64_t, "u", "N struct sess_mem")
MAC_STAT(n_sess,		uint64_t, "u", "N struct sess")
MAC_STAT(n_object,		uint64_t, "u", "N struct object")
MAC_STAT(n_objecthead,		uint64_t, "u", "N struct objecthead")
MAC_STAT(n_smf,			uint64_t, "u", "N struct smf")
MAC_STAT(n_smf_frag,		uint64_t, "u", "N small free smf")
MAC_STAT(n_smf_large,		uint64_t, "u", "N large free smf")
MAC_STAT(n_vbe_conn,		uint64_t, "u", "N struct vbe_conn")
MAC_STAT(n_wrk,			uint64_t, "u", "N worker threads")
MAC_STAT(n_wrk_create,		uint64_t, "u", "N worker threads created")
MAC_STAT(n_wrk_failed,		uint64_t, "u", "N worker threads not created")
MAC_STAT(n_wrk_max,		uint64_t, "u", "N worker threads limited")
MAC_STAT(n_wrk_queue,		uint64_t, "u", "N queued work requests")
MAC_STAT(n_wrk_overflow,	uint64_t, "u", "N overflowed work requests")
MAC_STAT(n_wrk_drop,		uint64_t, "u", "N dropped work requests")

MAC_STAT(n_expired,		uint64_t, "u", "N expired objects")
MAC_STAT(n_deathrow,		uint64_t, "u", "N objects on deathrow")

MAC_STAT(losthdr,		uint64_t, "u", "HTTP header overflows")

MAC_STAT(n_objsendfile,		uint64_t, "u", "Objects sent with sendfile")
MAC_STAT(n_objwrite,		uint64_t, "u", "Objects sent with write")

MAC_STAT(s_sess,		uint64_t, "u", "Total Sessions")
MAC_STAT(s_req,			uint64_t, "u", "Total Requests")
MAC_STAT(s_pipe,		uint64_t, "u", "Total pipe")
MAC_STAT(s_pass,		uint64_t, "u", "Total pass")
MAC_STAT(s_fetch,		uint64_t, "u", "Total fetch")
MAC_STAT(s_hdrbytes,		uint64_t, "u", "Total header bytes")
MAC_STAT(s_bodybytes,		uint64_t, "u", "Total body bytes")

MAC_STAT(sess_closed,		uint64_t, "u", "Session Closed")
MAC_STAT(sess_pipeline,		uint64_t, "u", "Session Pipeline")
MAC_STAT(sess_readahead,	uint64_t, "u", "Session Read Ahead")
MAC_STAT(sess_herd,		uint64_t, "u", "Session herd")

MAC_STAT(shm_records,		uint64_t, "u", "SHM records")
MAC_STAT(shm_writes,		uint64_t, "u", "SHM writes")
MAC_STAT(shm_cont,		uint64_t, "u", "SHM MTX contention")
