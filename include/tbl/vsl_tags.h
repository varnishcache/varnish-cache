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
 *	Flags
 *	Short Description (1 line, max ? chars)
 *	Long Description (in RST "definition list" format)
 */

#define NODEF_NOTICE \
    "NB: This log record is masked by default.\n\n"

SLTM(Debug, SLT_F_BINARY, "Debug messages",
	"Debug messages can normally be ignored, but are sometimes"
	" helpful during trouble-shooting.  Most debug messages must"
	" be explicitly enabled with parameters.\n\n"
)

SLTM(Error, 0, "Error messages",
	"Error messages are stuff you probably want to know.\n\n"
)

SLTM(CLI, 0, "CLI communication",
	"CLI communication between varnishd master and child process.\n\n"
)

/*---------------------------------------------------------------------*/

SLTM(SessOpen, 0, "Client connection opened",
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

SLTM(SessClose, 0, "Client connection closed",
	"SessionClose is the last record for any client connection.\n\n"
	"The format is::\n\n"
	"\t%s %f\n"
	"\t|  |\n"
	"\t|  +- How long the session was open\n"
	"\t+---- Why the connection closed\n"
	"\n"
)

/*---------------------------------------------------------------------*/

SLTM(BackendOpen, 0, "Backend connection opened",
	"Logged when a new backend connection is opened.\n\n"
	"The format is::\n\n"
	"\t%d %s %s %s\n"
	"\t|  |  |  |\n"
	"\t|  |  |  +- Remote port\n"
	"\t|  |  +---- Remote address\n"
	"\t|  +------- Backend display name\n"
	"\t+---------- Connection file descriptor\n"
	"\n"
)

SLTM(BackendReuse, 0, "Backend connection put up for reuse",
	"Logged when a backend connection is put up for reuse by a later"
	" connection.\n\n"
	"The format is::\n\n"
	"\t%d %s\n"
	"\t|  |\n"
	"\t|  +- Backend display name\n"
	"\t+---- Connection file descriptor\n"
	"\n"
)

SLTM(BackendClose, 0, "Backend connection closed",
	"Logged when a backend connection is closed.\n\n"
	"The format is::\n\n"
	"\t%d %s [ %s ]\n"
	"\t|  |    |\n"
	"\t|  |    +- Optional reason\n"
	"\t|  +------ Backend display name\n"
	"\t+--------- Connection file descriptor\n"
	"\n"
)

SLTM(HttpGarbage, SLT_F_BINARY, "Unparseable HTTP request",
	"Logs the content of unparseable HTTP requests.\n\n"
)

SLTM(Backend, 0, "Backend selected",
	"Logged when a connection is selected for handling a backend"
	" request.\n\n"
	"The format is::\n\n"
	"\t%d %s %s\n"
	"\t|  |  |\n"
	"\t|  |  +- Backend display name\n"
	"\t|  +---- VCL name\n"
	"\t+------- Connection file descriptor\n"
	"\n"
)

SLTM(Length, 0, "Size of object body",
	"Logs the size of a fetch object body.\n\n"
)

SLTM(FetchError, 0, "Error while fetching object",
	"Logs the error message of a failed fetch operation.\n\n"
)

#define SLTH(tag, ind, req, resp, sdesc, ldesc) \
	SLTM(Req##tag, (req ? 0 : SLT_F_UNUSED), "Client request " sdesc, ldesc)
#include "tbl/vsl_tags_http.h"
#undef SLTH

#define SLTH(tag, ind, req, resp, sdesc, ldesc) \
	SLTM(Resp##tag, (resp ? 0 : SLT_F_UNUSED), "Client response " sdesc, \
	    ldesc)
#include "tbl/vsl_tags_http.h"
#undef SLTH

#define SLTH(tag, ind, req, resp, sdesc, ldesc) \
	SLTM(Bereq##tag, (req ? 0 : SLT_F_UNUSED), "Backend request " sdesc, \
	    ldesc)
#include "tbl/vsl_tags_http.h"
#undef SLTH

#define SLTH(tag, ind, req, resp, sdesc, ldesc) \
	SLTM(Beresp##tag, (resp ? 0 : SLT_F_UNUSED), "Backend response " \
	    sdesc, ldesc)
#include "tbl/vsl_tags_http.h"
#undef SLTH

#define SLTH(tag, ind, req, resp, sdesc, ldesc) \
	SLTM(Obj##tag, (resp ? 0 : SLT_F_UNUSED), "Object  " sdesc, ldesc)
#include "tbl/vsl_tags_http.h"
#undef SLTH

SLTM(BogoHeader, 0, "Bogus HTTP received",
	"Contains the first 20 characters of received HTTP headers we could"
	" not make sense of.  Applies to both req.http and beresp.http.\n\n"
)
SLTM(LostHeader, 0, "Failed attempt to set HTTP header",
	"Logs the header name of a failed HTTP header operation due to"
	" resource exhaustion or configured limits.\n\n"
)

SLTM(TTL, 0, "TTL set on object",
	"A TTL record is emitted whenever the ttl, grace or keep"
	" values for an object is set.\n\n"
	"The format is::\n\n"
	"\t%s %d %d %d %d %d [ %d %u %u ]\n"
	"\t|  |  |  |  |  |    |  |  |\n"
	"\t|  |  |  |  |  |    |  |  +- Max-Age from Cache-Control header\n"
	"\t|  |  |  |  |  |    |  +---- Expires header\n"
	"\t|  |  |  |  |  |    +------- Date header\n"
	"\t|  |  |  |  |  +------------ Age (incl Age: header value)\n"
	"\t|  |  |  |  +--------------- Reference time for TTL\n"
	"\t|  |  |  +------------------ Keep\n"
	"\t|  |  +--------------------- Grace\n"
	"\t|  +------------------------ TTL\n"
	"\t+--------------------------- \"RFC\" or \"VCL\"\n"
	"\n"
	"The last four fields are only present in \"RFC\" headers.\n\n"
	"Examples::\n\n"
	"\tRFC 19 -1 -1 1312966109 4 0 0 23\n"
	"\tVCL 10 -1 -1 1312966109 4\n"
	"\tVCL 7 -1 -1 1312966111 6\n"
	"\tVCL 7 120 -1 1312966111 6\n"
	"\tVCL 7 120 3600 1312966111 6\n"
	"\tVCL 12 120 3600 1312966113 8\n"
	"\n"
)

SLTM(Fetch_Body, 0, "Body fetched from backend",
	"Ready to fetch body from backend.\n\n"
	"The format is::\n\n"
	"\t%d (%s) %s\n"
	"\t|   |    |\n"
	"\t|   |    +---- 'stream' or '-'\n"
	"\t|   +--------- Text description of body fetch mode\n"
	"\t+------------- Body fetch mode\n"
	"\n"
)

SLTM(VCL_acl, 0, "VSL ACL check results",
	"Logs VCL ACL evaluation results.\n\n"
)

SLTM(VCL_call, 0, "VCL method called",
	"Logs the VCL method name when a VCL method is called.\n\n"
)

SLTM(VCL_trace, 0, "VCL trace data",
	"Logs VCL execution trace data.\n\n"
	"The format is::\n\n"
	"\t%u %u.%u\n"
	"\t|  |  |\n"
	"\t|  |  +- VCL program line position\n"
	"\t|  +---- VCL program line number\n"
	"\t+------- VCL trace point index\n"
	"\n"
	NODEF_NOTICE
)

SLTM(VCL_return, 0, "VCL method return value",
	"Logs the VCL method terminating statement.\n\n"
)

SLTM(ReqStart, 0, "Client request start",
	"Start of request processing. Logs the client IP address and port"
	" number.\n\n"
	"The format is::\n\n"
	"\t%s %s\n"
	"\t|  |\n"
	"\t|  +- Port number\n"
	"\t+---- IP address\n"
	"\n"
)

SLTM(Hit, 0, "Hit object in cache",
	"Object looked up in cache. Shows the VXID of the object.\n\n"
)

SLTM(HitPass, 0, "Hit for pass object in cache.",
	"Hit-for-pass object looked up in cache. Shows the VXID of the"
	" hit-for-pass object.\n\n"
)

SLTM(ExpBan, 0, "Object evicted due to ban",
	"Logs the VXID when an object is banned.\n\n"
)

SLTM(ExpKill, 0, "Object expiry event",
	"Logs events related to object expiry. The events are:\n\n"
	"EXP_Rearm\n"
	"\tLogged when the expiry time of an object changes.\n\n"
	"EXP_Inbox\n"
	"\tLogged when the expiry thread picks an object from the inbox for"
	" processing.\n\n"
	"EXP_Kill\n"
	"\tLogged when the expiry thread kills an object from the inbox.\n\n"
	"EXP_When\n"
	"\tLogged when the expiry thread moves an object on the binheap.\n\n"
	"EXP_Expired\n"
	"\tLogged when the expiry thread expires an object.\n\n"
	"LRU_Cand\n"
	"\tLogged when an object is evaluated for LRU force expiry.\n\n"
	"LRU\n"
	"\tLogged when an object is force expired due to LRU.\n\n"
	"LRU_Fail\n"
	"\tLogged when no suitable candidate object is found for LRU force"
	" expiry.\n\n"
	"The format is::\n\n"
	"\tEXP_Rearm p=%p E=%f e=%f f=0x%x\n"
	"\tEXP_Inbox p=%p e=%f f=0x%x\n"
	"\tEXP_Kill p=%p e=%f f=0x%x\n"
	"\tEXP_When p=%p e=%f f=0x%x\n"
	"\tEXP_Expired x=%u t=%f\n"
	"\tLRU_Cand p=%p f=0x%x r=%d\n"
	"\tLRU x=%u\n"
	"\tLRU_Fail\n"
	"\t\n"
	"\tLegend:\n"
	"\tp=%p         Objcore pointer\n"
	"\tt=%f         Remaining TTL (s)\n"
	"\te=%f         Expiry time (unix epoch)\n"
	"\tE=%f         Old expiry time (unix epoch)\n"
	"\tf=0x%x       Objcore flags\n"
	"\tr=%d         Objcore refcount\n"
	"\tx=%u         Object VXID\n"
	"\n"
)

SLTM(WorkThread, 0, "Logs thread start/stop events",
	"Logs worker thread creation and termination events.\n\n"
	"The format is::\n\n"
	"\t%p %s\n"
	"\t|  |\n"
	"\t|  +- [start|end]\n"
	"\t+---- Worker struct pointer\n"
	"\n"
	NODEF_NOTICE
)

SLTM(ESI_xmlerror, 0, "ESI parser error or warning message",
	"An error or warning was generated during parsing of an ESI object."
	" The log record describes the problem encountered."
)

SLTM(Hash, 0, "Value added to hash",
	"This value was added to the object lookup hash.\n\n"
	NODEF_NOTICE
)

SLTM(Backend_health, 0, "Backend health check",
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
	"\n"
)

SLTM(VCL_Log, 0, "Log statement from VCL",
	"User generated log messages insert from VCL through std.log()"
)

SLTM(VCL_Error, 0, "VCL execution error message",
	"Logs error messages generated during VCL execution.\n\n"
)

SLTM(Gzip, 0, "G(un)zip performed on object",
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

SLTM(Link, 0, "Links to a child VXID",
	"Links this VXID to any child VXID it initiates.\n\n"
	"The format is::\n\n"
	"\t%s %d %s\n"
	"\t|  |  |\n"
	"\t|  |  +- Reason\n"
	"\t|  +---- Child vxid\n"
	"\t+------- Child type (\"req\" or \"bereq\")\n"
	"\n"
)

SLTM(Begin, 0, "Marks the start of a VXID",
	"The first record of a VXID transaction.\n\n"
	"The format is::\n\n"
	"\t%s %d %s\n"
	"\t|  |  |\n"
	"\t|  |  +- Reason\n"
	"\t|  +---- Parent vxid\n"
	"\t+------- Type (\"sess\", \"req\" or \"bereq\")\n"
	"\n"
)

SLTM(End, 0, "Marks the end of a VXID",
	"The last record of a VXID transaction.\n\n"
)

SLTM(VSL, 0, "VSL API warnings and error message",
	"Warnings and error messages genererated by the VSL API while"
	" reading the shared memory log.\n\n"
)

SLTM(Storage, 0, "Where object is stored",
	"Type and name of the storage backend the object is stored in.\n\n"
	"The format is::\n\n"
	"\t%s %s\n"
	"\t|  |\n"
	"\t|  +- Name of storage backend\n"
	"\t+---- Type (\"malloc\", \"file\", \"persistent\" etc.)\n"
	"\n"
)

SLTM(Timestamp, 0, "Timing information",
	"Contains timing information for the Varnish worker threads.\n\n"
	"Time stamps are issued by Varnish on certain events,"
	" and show the absolute time of the event, the time spent since the"
	" start of the work unit, and the time spent since the last timestamp"
	" was logged. See vsl(7) for information about the individual"
	" timestamps.\n\n"
	"The format is::\n\n"
	"\t%s: %f %f %f\n"
	"\t|   |  |  |\n"
	"\t|   |  |  +- Time since last timestamp\n"
	"\t|   |  +---- Time since start of work unit\n"
	"\t|   +------- Absolute time of event\n"
	"\t+----------- Event label\n"
	"\n"
)

SLTM(ReqAcct, 0, "Request handling byte counts",
	"Contains byte counts for the request handling. This record is not"
	" logged for ESI sub-requests, but the sub-requests' response"
	" body count is added to the main request.\n\n"
	"The format is::\n\n"
	"\t%d %d %d %d %d %d\n"
	"\t|  |  |  |  |  |\n"
	"\t|  |  |  |  |  +- Total bytes transmitted\n"
	"\t|  |  |  |  +---- Body bytes transmitted\n"
	"\t|  |  |  +------- Header bytes transmitted\n"
	"\t|  |  +---------- Total bytes received\n"
	"\t|  +------------- Body bytes received\n"
	"\t+---------------- Header bytes received\n"
	"\n"
)

SLTM(ESI_BodyBytes, 0, "ESI body fragment byte counter",
	"Contains the body byte count for this ESI body fragment."
	" This number does not include any transfer encoding overhead.\n\n"
	"The format is::\n\n"
	"\t%d\n"
	"\t|\n"
	"\t+- Body bytes\n"
	"\n"
)

SLTM(PipeAcct, 0, "Pipe byte counts",
	"Contains byte counters for pipe sessions.\n\n"
	"The format is::\n\n"
	"\t%d %d %d %d\n"
	"\t|  |  |  |\n"
	"\t|  |  |  +------- Piped bytes to client\n"
	"\t|  |  +---------- Piped bytes from client\n"
	"\t|  +------------- Backend request headers\n"
	"\t+---------------- Client request headers\n"
	"\n"
)

SLTM(BereqAcct, 0, "Backend request accounting",
	"Contains byte counters from backend request processing.\n\n"
	"The format is::\n\n"
	"\t%d %d %d %d %d %d\n"
	"\t|  |  |  |  |  |\n"
	"\t|  |  |  |  |  +- Total bytes received\n"
	"\t|  |  |  |  +---- Body bytes received\n"
	"\t|  |  |  +------- Header bytes received\n"
	"\t|  |  +---------- Total bytes transmitted\n"
	"\t|  +------------- Body bytes transmitted\n"
	"\t+---------------- Header bytes transmitted\n"
	"\n"
)

#undef NODEF_NOTICE
