/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Varnish Software AS
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
 * Define the tags in the shared memory in a reusable format.
 * Whoever includes this get to define what the SLTM macro does.
 *
 * REMEMBER to update the documentation (especially the varnishlog(1) man
 * page) whenever this list changes.
 *
 * XXX: Please add new entries a the end to not break saved log-segments.
 * XXX: we can resort them when we have a major release.
 *
 * Arguments:
 *	Tag-Name
 *	Short Description (1 line, max ?? chars)
 *	Long Description (Multi line)
 */

SLTM(Debug, "", "")
SLTM(Error, "", "")
SLTM(CLI, "CLI communication", "CLI communication between master and child process.")
SLTM(StatSess, "Session statistics", "")
SLTM(ReqEnd, "Client request end", "Client request end. The first number is the XID. \n"
"The second is the time when processing of the request started.\n"
"The third is the time the request completed.\n"
"The forth is is the time elapsed between the request actually being accepted and\n"
"the start of the request processing.\n" 
"The fifth number is the time elapsed from the start of the request processing \n"
"until we start delivering the object to the client.\n" 
"The sixth and last number is the time from we start delivering the object\n" 
"until the request completes. ")
SLTM(SessionOpen, "Client connection opened", "")
SLTM(SessionClose, "Client connection closed", "SessionClose tells you why HTTP\n"
"client-connections are closed. These can be:\n"
"timeout - No keep-alive was received within sess_timeout\n"
"Connection: close - The client specifed that keepalive should be disabled by sending a 'Connection: close' header.\n"
"no request - No initial request was received within sess_timeout.\n"
"EOF - ???\n"
"remote closed - ???\n"
"error - Processing reached vcl_error even if the status code indicates success\n"
"blast - ???")
SLTM(BackendOpen, "Backend connection opened", "")
SLTM(BackendXID, "The unique ID of the backend transaction", "")
SLTM(BackendReuse, "Backend connection reused", "")
SLTM(BackendClose, "Backend connection closed", "")
SLTM(HttpGarbage, "", "")
SLTM(Backend, "Backend selected", "")
SLTM(Length, "Size of object body", "")

SLTM(FetchError, "Error while fetching object", "")

#define SLTH(aa, bb)	SLTM(Req##aa, "", "")
#include "tbl/vsl_tags_http.h"
#undef SLTH

#define SLTH(aa, bb)	SLTM(Resp##aa, "", "")
#include "tbl/vsl_tags_http.h"
#undef SLTH

#define SLTH(aa, bb)	SLTM(Bereq##aa, "", "")
#include "tbl/vsl_tags_http.h"
#undef SLTH

#define SLTH(aa, bb)	SLTM(Beresp##aa, "", "")
#include "tbl/vsl_tags_http.h"
#undef SLTH

#define SLTH(aa, bb)	SLTM(Obj##aa, "", "")
#include "tbl/vsl_tags_http.h"
#undef SLTH

SLTM(LostHeader, "", "")

SLTM(TTL, "TTL set on object", "")
SLTM(Fetch_Body, "Body fetched from backend", "")
SLTM(VCL_acl, "", "")
SLTM(VCL_call, "VCL method called", "")
SLTM(VCL_trace, "VCL trace data", "")
SLTM(VCL_return, "VCL method return value", "")
SLTM(ReqStart, "Client request start", "")
SLTM(Hit, "Hit object in cache", "")
SLTM(HitPass, "Hit for pass object in cache", "")
SLTM(ExpBan, "Object evicted due to ban", "")
SLTM(ExpKill, "Object expired", "")
SLTM(WorkThread, "", "")

SLTM(ESI_xmlerror, "Error while parsing ESI tags", "")

SLTM(Hash, "Value added to hash", "")

SLTM(Backend_health, "Backend health check", "")

SLTM(VCL_Debug, "Unused", "")
SLTM(VCL_Log, "Log statement from VCL", "")
SLTM(VCL_Error, "", "")

SLTM(Gzip, "G(un)zip performed on object", "")
