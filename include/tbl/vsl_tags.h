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
 *	Short Description (1 line, max ? chars)
 *	Long Description (in RST "definition list" format)
 */

SLTM(Debug, "Debug messages",
	"Debug messages can normally be ignored, but are sometimes\n"
	"helpful during trouble-shooting.  Most debug messages must\n"
	"be explicitly enabled with parameters."
)
SLTM(Error, "Error messages",
	"Error messages are stuff you probably want to know."
)
SLTM(CLI, "CLI communication",
	"CLI communication between master and child process."
)

SLTM(ReqEnd, "Client request end",
	"Marks the end of client request.\n\n"
	"xid\n    Transaction id.\n\n"
	"Trxd\n     Timestamp when the request started.\n\n"
	"Tidle\n    Timestamp when the request ended.\n\n"
	"dTrx\n    Time to receive request\n\n"
	"dTproc\n    Time to process request\n\n"
	"dTtx\n    Time to transmit response\n\n"
)

/*---------------------------------------------------------------------*/

SLTM(SessOpen, "Client connection opened",
	"The first record for a client connection, with the\n"
	"socket-endpoints of the connection.\n\n"
	"caddr\n    Client IPv4/6 address\n\n"
	"cport\n    Client TCP port\n\n"
	"lsock\n    Listen socket\n\n"
	"laddr\n    Local IPv4/6 address ('-' if !$log_local_addr)\n\n"
	"lport\n    Local TCP port ('-' if !$log_local_addr)\n\n"
	"fd\n    File descriptor number"
)

/*
 * XXX: compilers are _so_ picky, and won't let us do an #include
 * XXX: in the middle of a macro invocation :-(
 * XXX: If we could, these three lines would have described the
 * XXX: 'reason' field below.
#define SESS_CLOSE(nm, desc) "    " #nm "\n\t" desc "\n\n"
#include <tbl/sess_close.h>
#undef SESS_CLOSE
*/

SLTM(SessClose, "Client connection closed",
	"SessionClose is the last record for any client connection.\n\n"
	"reason\n    Why the connection closed.\n\n"
	"duration\n    How long the session were open.\n\n"
	"Nreq\n    How many requests on session.\n\n"
	"Npipe\n    If 'pipe' were used on session.\n\n"
	"Npass\n    Requests handled with pass.\n\n"
	"Nfetch\n    Backend fetches by session.\n\n"
	"Bhdr\n    Header bytes sent on session.\n\n"
	"Bbody\n    Body bytes sent on session.\n\n"
)

/*---------------------------------------------------------------------*/

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

SLTM(BogoHeader, "Bogus HTTP received",
	"Contains the first 20 characters of received HTTP headers we could"
	" not make sense of.  Applies to both req.http and beres.http."
)
SLTM(LostHeader, "Failed attempt to set HTTP header", "")

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

SLTM(Link, "Links to a child VXID",
	"Links this VXID to any child VXID it initiates\n"
	"The first field gives the type of the child:\n"
	"    req     Request\n"
	"    bereq   Backend request\n"
	"    esireq  ESI subrequest\n"
	"The second field gives the VXID of the child.\n"
)

SLTM(Begin, "Marks the start of a VXID",
    "The first record of a VXID transaction.\n"
    "The first field gives the type of the transaction:\n"
    "    sess	Session\n"
    "    req	Request\n"
    "    bereq	Backend request\n"
    "    esireq	ESI subrequest\n"
    "The second field gives the VXID of the parent that initiated this"
    " transaction. For Session transactions this field is blank.\n"
)

SLTM(End, "Marks the end of a VXID",
    "The last record of a VXID transaction.\n"
)

SLTM(VSL, "Internally generated VSL API warnings and error message",
    "Warnings and error messages genererated by the VSL API while reading the"
    " shared memory log"
)
