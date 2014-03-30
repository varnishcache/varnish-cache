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
 *
 * Definition of all shared memory statistics below.
 *
 * Fields (n, t, l, f, v, e, d):
 *    n - Name:		Field name, in C-source and stats programs
 *    t - Type:		C-type, uint64_t, unless marked in 'f'
 *    l - Local:	Local counter in worker thread.
 *    f - Format:	Semantics of the value in this field
 *				'a' - Accumulator (deprecated, use 'c')
 *				'b' - Bitmap
 *				'c' - Counter, never decreases.
 *				'g' - Gauge, goes up and down
 *				'i' - Integer (deprecated, use 'g')
 *    v - Verbosity:	Counter verbosity level (see vsc_levels.h)
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

/**********************************************************************/

#ifdef VSC_DO_MGT

VSC_F(uptime,			uint64_t, 0, 'c', info,
    "Management process uptime",
	"Uptime in seconds of the management process"
)
VSC_F(child_start,		uint64_t, 0, 'c', diag,
    "Child process started",
	"Number of times the child process has been started"
)
VSC_F(child_exit,		uint64_t, 0, 'c', diag,
    "Child process normal exit",
	"Number of times the child process has been cleanly stopped"
)
VSC_F(child_stop,		uint64_t, 0, 'c', diag,
    "Child process unexpected exit",
	"Number of times the child process has exited with an unexpected"
	" return code"
)
VSC_F(child_died,		uint64_t, 0, 'c', diag,
    "Child process died (signal)",
	"Number of times the child process has died due to signals"
)
VSC_F(child_dump,		uint64_t, 0, 'c', diag,
    "Child process core dumped",
	"Number of times the child process has produced core dumps"
)
VSC_F(child_panic,		uint64_t, 0, 'c', diag,
    "Child process panic",
	"Number of times the management process has caught a child panic"
)

#endif

/**********************************************************************/

#ifdef VSC_DO_LCK

VSC_F(creat,			uint64_t, 0, 'a', debug,
    "Created locks",
	""
)
VSC_F(destroy,			uint64_t, 0, 'a', debug,
    "Destroyed locks",
	""
)
VSC_F(locks,			uint64_t, 0, 'a', debug,
    "Lock Operations",
	""
)

#endif

/**********************************************************************
 * All Stevedores support these counters
 */

#if defined(VSC_DO_SMA) || defined (VSC_DO_SMF)
VSC_F(c_req,			uint64_t, 0, 'a', info,
    "Allocator requests",
	""
)
VSC_F(c_fail,			uint64_t, 0, 'a', info,
    "Allocator failures",
	""
)
VSC_F(c_bytes,			uint64_t, 0, 'a', info,
    "Bytes allocated",
	""
)
VSC_F(c_freed,			uint64_t, 0, 'a', info,
    "Bytes freed",
	""
)
VSC_F(g_alloc,			uint64_t, 0, 'i', info,
    "Allocations outstanding",
	""
)
VSC_F(g_bytes,			uint64_t, 0, 'i', info,
    "Bytes outstanding",
	""
)
VSC_F(g_space,			uint64_t, 0, 'i', info,
    "Bytes available",
	""
)
#endif


/**********************************************************************/

#ifdef VSC_DO_SMA
/* No SMA specific counters */
#endif

/**********************************************************************/

#ifdef VSC_DO_SMF
VSC_F(g_smf,			uint64_t, 0, 'i', info,
    "N struct smf",
	""
)
VSC_F(g_smf_frag,		uint64_t, 0, 'i', info,
    "N small free smf",
	""
)
VSC_F(g_smf_large,		uint64_t, 0, 'i', info,
    "N large free smf",
	""
)
#endif

/**********************************************************************/

#ifdef VSC_DO_VBE

VSC_F(vcls,			uint64_t, 0, 'i', debug,
    "VCL references",
	""
)
VSC_F(happy,			uint64_t, 0, 'b', info,
    "Happy health probes",
	""
)
VSC_F(bereq_hdrbytes,		uint64_t, 0, 'a', info,
    "Request header bytes",
	"Total backend request header bytes sent"
)
VSC_F(bereq_bodybytes,		uint64_t, 0, 'a', info,
    "Request body bytes",
	"Total backend request body bytes sent"
)
VSC_F(beresp_hdrbytes,		uint64_t, 0, 'a', info,
    "Response header bytes",
	"Total backend response header bytes received"
)
VSC_F(beresp_bodybytes,		uint64_t, 0, 'a', info,
    "Response body bytes",
	"Total backend response body bytes received"
)
VSC_F(pipe_hdrbytes,		uint64_t, 0, 'a', info,
    "Pipe request header bytes",
	"Total request bytes sent for piped sessions"
)
VSC_F(pipe_out,			uint64_t, 0, 'a', info,
    "Piped bytes to backend",
	"Total number of bytes forwarded to backend in"
	" pipe sessions"
)
VSC_F(pipe_in,			uint64_t, 0, 'a', info,
    "Piped bytes from backend",
	"Total number of bytes forwarded from backend in"
	" pipe sessions"
)

#endif

/**********************************************************************/
#ifdef VSC_DO_MEMPOOL

VSC_F(live,			uint64_t, 0, 'g', debug,
    "In use",
	""
)
VSC_F(pool,			uint64_t, 0, 'g', debug,
    "In Pool",
	""
)
VSC_F(sz_wanted,		uint64_t, 0, 'g', debug,
    "Size requested",
	""
)
VSC_F(sz_needed,		uint64_t, 0, 'g', debug,
    "Size allocated",
	""
)
VSC_F(allocs,			uint64_t, 0, 'c', debug,
    "Allocations",
	""
)
VSC_F(frees,			uint64_t, 0, 'c', debug,
    "Frees",
	""
)
VSC_F(recycle,			uint64_t, 0, 'c', debug,
    "Recycled from pool",
	""
)
VSC_F(timeout,			uint64_t, 0, 'c', debug,
    "Timed out from pool",
	""
)
VSC_F(toosmall,			uint64_t, 0, 'c', debug,
    "Too small to recycle",
	""
)
VSC_F(surplus,			uint64_t, 0, 'c', debug,
    "Too many for pool",
	""
)
VSC_F(randry,			uint64_t, 0, 'c', debug,
    "Pool ran dry",
	""
)

#endif
