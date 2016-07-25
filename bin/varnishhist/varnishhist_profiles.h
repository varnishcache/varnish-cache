/*-
 * Copyright 2016 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Author: Nils Goroll <nils.goroll@uplex.de>
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
 * Profile definitions for varnishhist
 */

// client
HIS_PROF(
    "responsetime",	// name
    HIS_CLIENT,		// HIS_CLIENT | HIS_BACKEND
    SLT_Timestamp,	// tag
    "Process:",		// prefix
    3,			// field
    -6,			// hist_low
    3,			// hist_high
    "graph the total time from start of request processing"
    " (first byte received) until ready to deliver the"
    " client response"
    )
HIS_PROF(
    "size",		// name
    HIS_CLIENT,		// HIS_CLIENT | HIS_BACKEND
    SLT_ReqAcct,	// tag
    HIS_NO_PREFIX,	// prefix
    5,			// field
    1,			// hist_low
    8,			// hist_high
    "graph the size of responses"
    )
// backend
HIS_PROF(
    "Bereqtime",	// name
    HIS_BACKEND,	// HIS_CLIENT | HIS_BACKEND
    SLT_Timestamp,	// tag
    "Bereq:",		// prefix
    3,			// field
    -6,			// hist_low
    3,			// hist_high
    "graph the time from beginning of backend processing"
    " until a backend request is sent completely"
    )
HIS_PROF(
    "Beresptime",	// name
    HIS_BACKEND,	// HIS_CLIENT | HIS_BACKEND
    SLT_Timestamp,	// tag
    "Beresp:",		// prefix
    3,			// field
    -6,			// hist_low
    3,			// hist_high
    "graph the time from beginning of backend processing"
    " until the response headers are being received completely"
    )
HIS_PROF(
    "BerespBodytime",	// name
    HIS_BACKEND,	// HIS_CLIENT | HIS_BACKEND
    SLT_Timestamp,	// tag
    "BerespBody:",	// prefix
    3,			// field
    -6,			// hist_low
    3,			// hist_high
    "graph the time from beginning of backend processing"
    " until the response body has been received"
    )
HIS_PROF(
    "Besize",		// name
    HIS_BACKEND,	// HIS_CLIENT | HIS_BACKEND
    SLT_BereqAcct,	// tag
    HIS_NO_PREFIX,	// prefix
    5,			// field
    1,			// hist_low
    8,			// hist_high
    "graph the backend response body size"
    )
