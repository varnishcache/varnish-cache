/*-
 * Copyright 2020 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Author: Nils Goroll <nils.goroll@uplex.de>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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
 * definition of privileges to use for the different Varnish Jail (VJ)
 * levels
 */
#define E VJS_MASK(VJS_EFFECTIVE)	// as-is
#define I VJS_MASK(VJS_INHERITABLE)	// as-is
#define P VJS_MASK(VJS_PERMITTED)	// joined with effective
#define L VJS_MASK(VJS_LIMIT)		// joined with of all the above

/* ------------------------------------------------------------
 * MASTER
 * - only EFFECTIVE & INHERITABLE are per JAIL state
 * - other priv sets are shared across all MASTER_* JAIL states
 *
 * MASTER implicit rules (vjs_master_rules())
 * - INHERITABLE and PERMITTED from SUBPROC* joined into PERMITTED
 * - implicit rules from above
 */
PRIV(MASTER_LOW,	E	, "file_write")	// XXX vcl_boot
PRIV(MASTER_LOW,	E	, "file_read")	// XXX library open
PRIV(MASTER_LOW,	E	, "net_access")

PRIV(MASTER_SYSTEM,	E|I	, PRIV_PROC_EXEC)
PRIV(MASTER_SYSTEM,	E|I	, PRIV_PROC_FORK)
PRIV(MASTER_SYSTEM,	E|I	, "file_read")
PRIV(MASTER_SYSTEM,	E|I	, "file_write")

PRIV(MASTER_FILE,	E	, "file_read")
PRIV(MASTER_FILE,	E	, "file_write")

PRIV(MASTER_STORAGE,	E	, "file_read")
PRIV(MASTER_STORAGE,	E	, "file_write")

PRIV(MASTER_PRIVPORT,	E	, "file_write")	// bind(AF_UNIX)
PRIV(MASTER_PRIVPORT,	E	, "net_access")
PRIV(MASTER_PRIVPORT,	E	, PRIV_NET_PRIVADDR)

PRIV(MASTER_KILL,	E	, PRIV_PROC_OWNER)

/* ------------------------------------------------------------
 * SUBPROC
 */
PRIV(SUBPROC_VCC,	E	, PRIV_PROC_SETID)	// waived after setuid
PRIV(SUBPROC_VCC,	E	, "file_read")
PRIV(SUBPROC_VCC,	E	, "file_write")

PRIV(SUBPROC_CC,	E	, PRIV_PROC_SETID)	// waived after setuid
PRIV(SUBPROC_CC,	E|I	, PRIV_PROC_EXEC)
PRIV(SUBPROC_CC,	E|I	, PRIV_PROC_FORK)
PRIV(SUBPROC_CC,	E|I	, "file_read")
PRIV(SUBPROC_CC,	E|I	, "file_write")

PRIV(SUBPROC_VCLLOAD,	E	, PRIV_PROC_SETID)	// waived after setuid
PRIV(SUBPROC_VCLLOAD,	E	, "file_read")

PRIV(SUBPROC_WORKER,	E	, PRIV_PROC_SETID)	// waived after setuid
PRIV(SUBPROC_WORKER,	E	, "net_access")
PRIV(SUBPROC_WORKER,	E	, "file_read")
PRIV(SUBPROC_WORKER,	E	, "file_write")
PRIV(SUBPROC_WORKER,	P	, PRIV_PROC_INFO)	// vmod_unix

#undef E
#undef I
#undef P
#undef L
#undef PRIV
