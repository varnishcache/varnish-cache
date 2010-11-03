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
 * $Id$
 *
 * Define the tags in the shared memory in a reusable format.
 * Whoever includes this get to define what the SLTM macro does.
 *
 * REMEMBER to update the documentation (especially the varnishlog(1) man
 * page) whenever this list changes.
 *
 * XXX: Please add new entries a the end to not break saved log-segments.
 * XXX: we can resort them when we have a major release.
 */

SLTM(Debug)
SLTM(Error)
SLTM(CLI)
SLTM(StatSess)
SLTM(ReqEnd)
SLTM(SessionOpen)
SLTM(SessionClose)
SLTM(BackendOpen)
SLTM(BackendXID)
SLTM(BackendReuse)
SLTM(BackendClose)
SLTM(HttpGarbage)
SLTM(Backend)
SLTM(Length)

SLTM(FetchError)

SLTM(RxRequest)
SLTM(RxResponse)
SLTM(RxStatus)
SLTM(RxURL)
SLTM(RxProtocol)
SLTM(RxHeader)

SLTM(TxRequest)
SLTM(TxResponse)
SLTM(TxStatus)
SLTM(TxURL)
SLTM(TxProtocol)
SLTM(TxHeader)

SLTM(ObjRequest)
SLTM(ObjResponse)
SLTM(ObjStatus)
SLTM(ObjURL)
SLTM(ObjProtocol)
SLTM(ObjHeader)

SLTM(LostHeader)

SLTM(TTL)
SLTM(VCL_acl)
SLTM(VCL_call)
SLTM(VCL_trace)
SLTM(VCL_return)
SLTM(VCL_error)
SLTM(ReqStart)
SLTM(Hit)
SLTM(HitPass)
SLTM(ExpBan)
SLTM(ExpKill)
SLTM(WorkThread)

SLTM(ESI_xmlerror)

SLTM(Hash)

SLTM(Backend_health)
SLTM(VCL_Log)
SLTM(Fetch_Body)
