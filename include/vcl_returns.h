/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006 Linpro AS
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
 * Initial implementation by Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * $Id$
 *
 * NB:  This file is machine generated, DO NOT EDIT!
 *
 * Edit vcc_gen_fixed_token.tcl instead
 */

#ifdef VCL_RET_MAC
#ifdef VCL_RET_MAC_E
VCL_RET_MAC_E(error, ERROR, (1 << 0), 0)
#endif
VCL_RET_MAC(lookup, LOOKUP, (1 << 1), 1)
VCL_RET_MAC(pipe, PIPE, (1 << 2), 2)
VCL_RET_MAC(pass, PASS, (1 << 3), 3)
VCL_RET_MAC(insert_pass, INSERT_PASS, (1 << 4), 4)
VCL_RET_MAC(fetch, FETCH, (1 << 5), 5)
VCL_RET_MAC(insert, INSERT, (1 << 6), 6)
VCL_RET_MAC(deliver, DELIVER, (1 << 7), 7)
VCL_RET_MAC(discard, DISCARD, (1 << 8), 8)
#else
#define VCL_RET_ERROR  (1 << 0)
#define VCL_RET_LOOKUP  (1 << 1)
#define VCL_RET_PIPE  (1 << 2)
#define VCL_RET_PASS  (1 << 3)
#define VCL_RET_INSERT_PASS  (1 << 4)
#define VCL_RET_FETCH  (1 << 5)
#define VCL_RET_INSERT  (1 << 6)
#define VCL_RET_DELIVER  (1 << 7)
#define VCL_RET_DISCARD  (1 << 8)
#define VCL_RET_MAX 9
#endif

#ifdef VCL_MET_MAC
VCL_MET_MAC(recv,RECV,(VCL_RET_ERROR|VCL_RET_PASS|VCL_RET_PIPE|VCL_RET_LOOKUP))
VCL_MET_MAC(miss,MISS,(VCL_RET_ERROR|VCL_RET_PASS|VCL_RET_PIPE|VCL_RET_FETCH))
VCL_MET_MAC(hit,HIT,(VCL_RET_ERROR|VCL_RET_PASS|VCL_RET_PIPE|VCL_RET_DELIVER))
VCL_MET_MAC(fetch,FETCH,(VCL_RET_ERROR|VCL_RET_PASS|VCL_RET_PIPE|VCL_RET_INSERT|VCL_RET_INSERT_PASS))
VCL_MET_MAC(timeout,TIMEOUT,(VCL_RET_FETCH|VCL_RET_DISCARD))
#endif
