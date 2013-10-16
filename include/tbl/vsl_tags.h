/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2013 Varnish Software AS
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
	"Debug messages can normally be ignored, but are sometimes"
	" helpful during trouble-shooting.  Most debug messages must"
	" be explicitly enabled with parameters.\n\n"
)
SLTM(Error, "Error messages",
	"Error messages are stuff you probably want to know.\n\n"
)
SLTM(CLI, "CLI communication",
	"CLI communication between master and child process.\n\n"
)

SLTM(ReqEnd, "Client request end",
	"Marks the end of client request.\n\n"
	"The format is::\n\n"
	"\t%f %f %f %f %f\n"
	"\t|  |  |  |  |\n"
	"\t|  |  |  |  +- Time to transmit response\n"
	"\t|  |  |  +---- Time to process request\n"
	"\t|  |  +------- Time to receive request\n"
	"\t|  +---------- Timestamp (since epoch) when the request ended\n"
	"\t+------------- Timestamp (since epoch) when the request started\n"
	"\n"
)

/*---------------------------------------------------------------------*/

SLTM(SessOpen, "Client connection opened",
	"The first record for a client connection, with the socket-endpoints"
	" of the connection.\n\n"
	"The format is::\n\n"
	"\t%s %d %s %s %s %d\n"
	"\t|  |  |  |  |  |\n"
	"\t|  |  |  |  |  +- File descriptor number\n"
	"\t|  |  |  |  +---- Local TCP port ('-' if !$log_local_addr)\n"
	"\t|  |  |  +------- Local IPv4/6 address ('-' if !$log_local_addr)\n"
	"\t|  |  +---------- Listen socket\n"
	"\t|  +------------- Client TCP socket\n"
	"\t+---------------- Client IPv4/6 address\n"
	"\n"
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
	"The format is::\n\n"
	"\t%s %f %u %u %u %u %u %u\n"
	"\t|  |  |  |  |  |  |  |\n"
	"\t|  |  |  |  |  |  |  +- Body bytes sent on session\n"
	"\t|  |  |  |  |  |  +---- Header bytes sent on session\n"
	"\t|  |  |  |  |  +------- Backend fetches by session\n"
	"\t|  |  |  |  +---------- Requests handled with pass\n"
	"\t|  |  |  +------------- If 'pipe' were used on session\n"
	"\t|  |  +---------------- How many requests on session\n"
	"\t|  +------------------- How long the session was open\n"
	"\t+---------------------- Why the connection closed\n"
	"\n"
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

#define SLTH(tag, ind, req, resp, sdesc, ldesc) \
	SLTM(Req##tag, (req ? "Client request " sdesc : "(unused)"), ldesc)
#include "tbl/vsl_tags_http.h"
#undef SLTH

#define SLTH(tag, ind, req, resp, sdesc, ldesc) \
	SLTM(Resp##tag, (resp ? "Client response " sdesc : "(unused)"), ldesc)
#include "tbl/vsl_tags_http.h"
#undef SLTH

#define SLTH(tag, ind, req, resp, sdesc, ldesc) \
	SLTM(Bereq##tag, (req ? "Backend request " sdesc : "(unused)"), ldesc)
#include "tbl/vsl_tags_http.h"
#undef SLTH

#define SLTH(tag, ind, req, resp, sdesc, ldesc) \
	SLTM(Beresp##tag, (resp ? "Backend response " sdesc : "(unused)"), \
	    ldesc)
#include "tbl/vsl_tags_http.h"
#undef SLTH

#define SLTH(tag, ind, req, resp, sdesc, ldesc) \
	SLTM(Obj##tag, (resp ? "Object  " sdesc : "(unused)"), ldesc)
#include "tbl/vsl_tags_http.h"
#undef SLTH

SLTM(BogoHeader, "Bogus HTTP received",
	"Contains the first 20 characters of received HTTP headers we could"
	" not make sense of.  Applies to both req.http and beres.http.\n\n"
)
SLTM(LostHeader, "Failed attempt to set HTTP header", "")

SLTM(TTL, "TTL set on object",
	"A TTL record is emitted whenever the ttl, grace or keep"
	" values for an object is set.\n\n"
	"The format is::\n\n"
	"\t%u %s %d %d %d %d %d [ %d %u %u ]\n"
	"\t|  |  |  |  |  |  |    |  |  |\n"
	"\t|  |  |  |  |  |  |    |  |  +- Max-Age from Cache-Control header\n"
	"\t|  |  |  |  |  |  |    |  +---- Expires header\n"
	"\t|  |  |  |  |  |  |    +------- Date header\n"
	"\t|  |  |  |  |  |  +------------ Age (incl Age: header value)\n"
	"\t|  |  |  |  |  +--------------- Reference time for TTL\n"
	"\t|  |  |  |  +------------------ Keep\n"
	"\t|  |  |  +--------------------- Grace\n"
	"\t|  |  +------------------------ TTL\n"
	"\t|  +--------------------------- \"RFC\" or \"VCL\"\n"
	"\t+------------------------------ object XID\n"
	"\n"
	"The last three fields are only present in \"RFC\" headers.\n\n"
	"Examples::\n\n"
	"\t1001 RFC 19 -1 -1 1312966109 4 0 0 23\n"
	"\t1001 VCL 10 -1 -1 1312966109 4\n"
	"\t1001 VCL 7 -1 -1 1312966111 6\n"
	"\t1001 VCL 7 120 -1 1312966111 6\n"
	"\t1001 VCL 7 120 3600 1312966111 6\n"
	"\t1001 VCL 12 120 3600 1312966113 8\n"
	"\n"
)
SLTM(Fetch_Body, "Body fetched from backend",
	"Finished fetching body from backend.\n\n"
	"The format is::\n\n"
	"\t%d(%s) cls %d\n"
	"\t|  |       |\n"
	"\t|  |       +- 1 if the backend connection was closed\n"
	"\t|  +--------- Text description of body status\n"
	"\t+------------ Body status\n"
	"\n"
)
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

SLTM(ESI_xmlerror, "ESI parser error or warning message",
	"An error or warning was generated during parsing of an ESI object."
	" The log record describes the problem encountered."
)

SLTM(Hash, "Value added to hash",
	"This value was added to the object lookup hash."
)

SLTM(Backend_health, "Backend health check",
	"The result of a backend health probe.\n\n"
	"The format is::\n\n"
	"\t%s %s %s %u %u %u %f %f %s\n"
	"\t|  |  |  |  |  |  |  |  |\n"
	"\t|  |  |  |  |  |  |  |  +- Probe HTTP response\n"
	"\t|  |  |  |  |  |  |  +---- Average response time\n"
	"\t|  |  |  |  |  |  +------- Response time\n"
	"\t|  |  |  |  |  +---------- Probe window size\n"
	"\t|  |  |  |  +------------- Probe threshold level\n"
	"\t|  |  |  +---------------- Number of good probes in window\n"
	"\t|  |  +------------------- Probe window bits\n"
	"\t|  +---------------------- Status message\n"
	"\t+------------------------- Backend name\n"
)

SLTM(VCL_Debug, "(unused)", "")
SLTM(VCL_Log, "Log statement from VCL",
	"User generated log messages insert from VCL through std.log()"
)
SLTM(VCL_Error, "", "")

SLTM(Gzip, "G(un)zip performed on object",
	"A Gzip record is emitted for each instance of gzip or gunzip"
	" work performed. Worst case, an ESI transaction stored in"
	" gzip'ed objects but delivered gunziped, will run into many of"
	" these.\n\n"
	"The format is::\n\n"
	"\t%c %c %c %d %d %d %d %d\n"
	"\t|  |  |  |  |  |  |  |\n"
	"\t|  |  |  |  |  |  |  +- Bit length of compressed data\n"
	"\t|  |  |  |  |  |  +---- Bit location of 'last' bit\n"
	"\t|  |  |  |  |  +------- Bit location of first deflate block\n"
	"\t|  |  |  |  +---------- Bytes output\n"
	"\t|  |  |  +------------- Bytes input\n"
	"\t|  |  +---------------- 'E': ESI, '-': Plain object\n"
	"\t|  +------------------- 'F': Fetch, 'D': Deliver\n"
	"\t+---------------------- 'G': Gzip, 'U': Gunzip, 'u': Gunzip-test\n"
	"\n"
	"Examples::\n\n"
	"\tU F E 182 159 80 80 1392\n"
	"\tG F E 159 173 80 1304 1314\n"
	"\n"
)

SLTM(Link, "Links to a child VXID",
	"Links this VXID to any child VXID it initiates.\n\n"
	"The format is::\n\n"
	"\t%s %d\n"
	"\t|  |\n"
	"\t|  +- Child vxid\n"
	"\t+---- Child type (\"req\", \"bereq\" or \"esireq\")\n"
	"\n"
)

SLTM(Begin, "Marks the start of a VXID",
	"The first record of a VXID transaction.\n\n"
	"The format is::\n\n"
	"\t%s %d\n"
	"\t|  |\n"
	"\t|  +- Parent vxid\n"
	"\t+---- Type (\"sess\", \"req\", \"bereq\" or \"esireq\")\n"
	"\n"
)

SLTM(End, "Marks the end of a VXID",
	"The last record of a VXID transaction.\n\n"
)

SLTM(VSL, "VSL API warnings and error message",
	"Warnings and error messages genererated by the VSL API while"
	" reading the shared memory log.\n\n"
)
