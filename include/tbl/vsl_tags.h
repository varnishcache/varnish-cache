/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 * Define the tags in the shared memory in a reusable format.
 * Whoever includes this get to define what the SLTM macro does.
 *
 * REMEMBER to update the documentation (especially the varnishlog(1) man
 * page) whenever this list changes.
 *
 * XXX: Please add new entries at the end to not break saved log-segments.
 *
 * Arguments:
 *	Tag-Name
 *	Flags
 *	Short Description (1 line, max ? chars)
 *	Long Description (in RST "definition list" format)
 */

/*lint -save -e525 -e539 */

#define NODEF_NOTICE \
    "NB: This log record is masked by default.\n\n"

/*
 * REL_20190915 remove after VSLng
 * kept for now for VSL binary compatibility
 */
#define NOSUP_NOTICE \
    "NOTE: This tag is currently not in use in the Varnish log.\n" \
    "It is mentioned here to document legacy versions of the log,\n" \
    "or reserved for possible use in future versions.\n\n"

SLTM(Debug, SLT_F_UNSAFE, "Debug messages",
	"Debug messages can normally be ignored, but are sometimes"
	" helpful during trouble-shooting.  Most debug messages must"
	" be explicitly enabled with parameters.\n\n"
	"Debug messages may be added, changed or removed without"
	" prior notice and shouldn't be considered stable.\n\n"
	NODEF_NOTICE
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
	"\t%s %d %s %s %s %f %d\n"
	"\t|  |  |  |  |  |  |\n"
	"\t|  |  |  |  |  |  +- File descriptor number\n"
	"\t|  |  |  |  |  +---- Session start time (unix epoch)\n"
	"\t|  |  |  |  +------- Local TCP port / 0 for UDS\n"
	"\t|  |  |  +---------- Local IPv4/6 address / 0.0.0.0 for UDS\n"
	"\t|  |  +------------- Socket name (from -a argument)\n"
	"\t|  +---------------- Remote TCP port / 0 for UDS\n"
	"\t+------------------- Remote IPv4/6 address / 0.0.0.0 for UDS\n"
	"\n"
)
/*
 * XXX generate the list of SC_* reasons:
 *
 * #include <stdio.h>
 * int main(void) {
 * #define SESS_CLOSE(e, sc, err, desc)			\
 *	printf("\t\"\\t* ``%s``: %s\\n\"\n", #e, desc);
 * #include "include/tbl/sess_close.h"
 *	return (0);
 * }
 */

SLTM(SessClose, 0, "Client connection closed",
	"SessClose is the last record for any client connection.\n\n"
	"The format is::\n\n"
	"\t%s %f\n"
	"\t|  |\n"
	"\t|  +- How long the session was open\n"
	"\t+---- Why the connection closed\n"
	"\nExplanation of reasons (first column):\n"
	"\t* ``REM_CLOSE``: Client Closed\n"
	"\t* ``REQ_CLOSE``: Client requested close\n"
	"\t* ``REQ_HTTP10``: Proto < HTTP/1.1\n"
	"\t* ``RX_BAD``: Received bad req/resp\n"
	"\t* ``RX_BODY``: Failure receiving body\n"
	"\t* ``RX_JUNK``: Received junk data\n"
	"\t* ``RX_OVERFLOW``: Received buffer overflow\n"
	"\t* ``RX_TIMEOUT``: Receive timeout\n"
	"\t* ``RX_CLOSE_IDLE``: timeout_idle reached\n"
	"\t* ``TX_PIPE``: Piped transaction\n"
	"\t* ``TX_ERROR``: Error transaction\n"
	"\t* ``TX_EOF``: EOF transmission\n"
	"\t* ``RESP_CLOSE``: Backend/VCL requested close\n"
	"\t* ``OVERLOAD``: Out of some resource\n"
	"\t* ``PIPE_OVERFLOW``: Session pipe overflow\n"
	"\t* ``RANGE_SHORT``: Insufficient data for range\n"
	"\t* ``REQ_HTTP20``: HTTP2 not accepted\n"
	"\t* ``VCL_FAILURE``: VCL failure\n"
	"\t* ``RAPID_RESET``: HTTP2 rapid reset\n"
	"\n"
)

/*---------------------------------------------------------------------*/

SLTM(BackendOpen, 0, "Backend connection opened",
	"Logged when a new backend connection is opened.\n\n"
	"The format is::\n\n"
	"\t%d %s %s %s %s %s %s\n"
	"\t|  |  |  |  |  |  |\n"
	"\t|  |  |  |  |  |  +- \"connect\" or \"reuse\"\n"
	"\t|  |  |  |  |  +---- Local port\n"
	"\t|  |  |  |  +------- Local address\n"
	"\t|  |  |  +---------- Remote port\n"
	"\t|  |  +------------- Remote address\n"
	"\t|  +---------------- Backend display name\n"
	"\t+------------------- Connection file descriptor\n"
	"\n"
)

SLTM(BackendClose, 0, "Backend connection closed",
	"Logged when a backend connection is closed.\n\n"
	"The format is::\n\n"
	"\t%d %s %s [ %s ]\n"
	"\t|  |  |    |\n"
	"\t|  |  |    +- Optional reason, see SessClose for explanation\n"
	"\t|  |  +------ \"close\" or \"recycle\"\n"
	"\t|  +--------- Backend display name\n"
	"\t+------------ Connection file descriptor\n"
	"\n"
)

SLTM(HttpGarbage, SLT_F_UNSAFE, "Unparseable HTTP request",
	"Logs the content of unparseable HTTP requests.\n\n"
)

SLTM(Proxy, 0, "PROXY protocol information",
	"PROXY protocol information.\n\n"
	"The format is::\n\n"
	"\t%d %s %d %s %d\n"
	"\t|  |  |  |  |\n"
	"\t|  |  |  |  +- server port\n"
	"\t|  |  |  +---- server ip\n"
	"\t|  |  +------- client port\n"
	"\t|  +---------- client ip\n"
	"\t+------------- PROXY protocol version\n"
	"\t\n"
	"\tAll fields are \"local\" for PROXY local connections (command 0x0)\n"
	"\n"
)

SLTM(ProxyGarbage, 0, "Unparseable PROXY request",
	"A PROXY protocol header was unparseable.\n\n"
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
	SLTM(Obj##tag, (resp ? 0 : SLT_F_UNUSED), "Object " sdesc, ldesc)
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
	" values for an object is set as well as whether the object is "
	" cacheable or not.\n\n"
	"The format is::\n\n"
	"\t%s %d %d %d %d [ %d %d %u %u ] %s\n"
	"\t|  |  |  |  |    |  |  |  |    |\n"
	"\t|  |  |  |  |    |  |  |  |    +- \"cacheable\" or \"uncacheable\"\n"
	"\t|  |  |  |  |    |  |  |  +------ Max-Age from Cache-Control header\n"
	"\t|  |  |  |  |    |  |  +--------- Expires header\n"
	"\t|  |  |  |  |    |  +------------ Date header\n"
	"\t|  |  |  |  |    +--------------- Age (incl Age: header value)\n"
	"\t|  |  |  |  +-------------------- Reference time for TTL\n"
	"\t|  |  |  +----------------------- Keep\n"
	"\t|  |  +-------------------------- Grace\n"
	"\t|  +----------------------------- TTL\n"
	"\t+-------------------------------- \"RFC\", \"VCL\" or \"HFP\"\n"
	"\n"
	"The four optional fields are only present in \"RFC\" headers.\n\n"
	"Examples::\n\n"
	"\tRFC 60 10 -1 1312966109 1312966109 1312966109 0 60 cacheable\n"
	"\tVCL 120 10 0 1312966111 uncacheable\n"
	"\tHFP 2 0 0 1312966113 uncacheable\n"
	"\n"
)

SLTM(Fetch_Body, 0, "Body fetched from backend",
	"Ready to fetch body from backend.\n\n"
	"The format is::\n\n"
	"\t%d %s %s\n"
	"\t|  |  |\n"
	"\t|  |  +---- 'stream' or '-'\n"
	"\t|  +------- Text description of body fetch mode\n"
	"\t+---------- Body fetch mode\n"
	"\n"
)

SLTM(VCL_acl, 0, "VCL ACL check results",
	"ACLs with the `+log` flag emits this record with the result.\n\n"
	"The format is::\n\n"
	"\t%s [%s [%s [fixed: %s]]]\n"
	"\t|   |   |          |\n"
	"\t|   |   |          +- Fix info (see below)\n"
	"\t|   |   +------------ Matching entry (only for MATCH)\n"
	"\t|   +---------------- Name of the ACL for MATCH or NO_MATCH\n"
	"\t+-------------------- MATCH, NO_MATCH or NO_FAM\n"
	"\n"
	"* Fix info: either contains network/mask for non-canonical entries "
	"(see acl +pedantic flag) or ``folded`` for entries "
	"which were the result of a fold operation (see acl +fold flag).\n"
	"* ``MATCH`` denotes an ACL match\n"
	"* ``NO_MATCH`` denotes that a checked ACL has not matched\n"
	"* ``NO_FAM`` denotes a missing address family and should not occur.\n"
	"\n"
)

SLTM(VCL_call, 0, "VCL method called",
	"Logs the VCL method name when a VCL method is called.\n\n"
)

SLTM(VCL_trace, 0, "VCL trace data",
	"Logs VCL execution trace data.\n\n"
	"The format is::\n\n"
	"\t%s %u %u.%u.%u\n"
	"\t|  |  |  |  |\n"
	"\t|  |  |  |  +- VCL program line position\n"
	"\t|  |  |  +---- VCL program line number\n"
	"\t|  |  +------- VCL program source index\n"
	"\t|  +---------- VCL trace point index\n"
	"\t+------------- VCL configname\n"
	"\n"
	NODEF_NOTICE
)

SLTM(VCL_return, 0, "VCL method return value",
	"Logs the VCL method terminating statement.\n\n"
)

SLTM(ReqStart, 0, "Client request start",
	"Start of request processing. Logs the client address, port number "
	" and listener endpoint name (from the -a command-line argument).\n\n"
	"The format is::\n\n"
	"\t%s %s %s\n"
	"\t|  |  |\n"
	"\t|  |  +-- Listener name (from -a)\n"
	"\t|  +----- Client Port number (0 for Unix domain sockets)\n"
	"\t+-------- Client IP4/6 address (0.0.0.0 for UDS)\n"
	"\n"
)

SLTM(Hit, 0, "Hit object in cache",
	"Object looked up in cache.\n\n"
	"The format is::\n\n"
	"\t%u %f %f %f [%u [%u]]\n"
	"\t|  |  |  |   |   |\n"
	"\t|  |  |  |   |   +- Content length\n"
	"\t|  |  |  |   +----- Fetched so far\n"
	"\t|  |  |  +--------- Keep period\n"
	"\t|  |  +------------ Grace period\n"
	"\t|  +--------------- Remaining TTL\n"
	"\t+------------------ VXID of the object\n"
	"\n"
)

SLTM(HitPass, 0, "Hit for pass object in cache.",
	"Hit-for-pass object looked up in cache.\n\n"
	"The format is::\n\n"
	"\t%u %f\n"
	"\t|  |\n"
	"\t|  +- Remaining TTL\n"
	"\t+---- VXID of the object\n"
	"\n"
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
	"EXP_Inspect\n"
	"\tLogged when the expiry thread inspects the next object scheduled"
	" to expire.\n\n"
	"EXP_Expired\n"
	"\tLogged when the expiry thread expires an object.\n\n"
	"EXP_Removed\n"
	"\tLogged when the expiry thread removes an object before expiry.\n\n"
	"VBF_Superseded\n"
	"\tLogged when an object supersedes another.\n\n"
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
	"\tEXP_Inspect p=%p e=%f f=0x%x\n"
	"\tEXP_Expired x=%u t=%f h=%u\n"
	"\tEXP_Removed x=%u t=%f h=%u\n"
	"\tVBF_Superseded x=%u n=%u\n"
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
	"\tn=%u         New object VXID\n"
	"\th=%u         Objcore hits\n"
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

SLTM(Hash, SLT_F_UNSAFE, "Value added to hash",
	"This value was added to the object lookup hash.\n\n"
	NODEF_NOTICE
)

/*
 * Probe window bits:
 *
 * the documentation below could get auto-generated like so:
 *
 * ( echo '#define PROBE_BITS_DOC \' ; \
 *   $CC -D 'BITMAP(n, c, t, b)="\t" #c ": " t "\n" \' \
 *       -E ./include/tbl/backend_poll.h | grep -E '^"' ; \
 *  echo '""' ) >./include/tbl/backend_poll_doc.h
 *
 * as this has a hackish feel to it, the documentation is included here as text
 * until we find a better solution or the above is accepted
 */

SLTM(Backend_health, 0, "Backend health check",
	"The result of a backend health probe.\n\n"
	"The format is::\n\n"
	"\t%s %s %s %s %u %u %u %f %f %s\n"
	"\t|  |  |  |  |  |  |  |  |  |\n"
	"\t|  |  |  |  |  |  |  |  |  +- Probe HTTP response / error information\n"
	"\t|  |  |  |  |  |  |  |  +---- Average response time\n"
	"\t|  |  |  |  |  |  |  +------- Response time\n"
	"\t|  |  |  |  |  |  +---------- Probe window size\n"
	"\t|  |  |  |  |  +------------- Probe threshold level\n"
	"\t|  |  |  |  +---------------- Number of good probes in window\n"
	"\t|  |  |  +------------------- Probe window bits\n"
	"\t|  |  +---------------------- \"healthy\" or \"sick\"\n"
	"\t|  +------------------------- \"Back\", \"Still\" or \"Went\"\n"
	"\t+---------------------------- Backend name\n"
	"\n"

	"Probe window bits are::\n\n"
	"\t'-': Could not connect\n"
	"\t'4': Good IPv4\n"
	"\t'6': Good IPv6\n"
	"\t'U': Good UNIX\n"
	"\t'x': Error Xmit\n"
	"\t'X': Good Xmit\n"
	"\t'r': Error Recv\n"
	"\t'R': Good Recv\n"
	"\t'H': Happy\n"
	"\n"
	"When the backend is just created, the window bits for health check\n"
	"slots that haven't run yet appear as '-' like failures to connect.\n"
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
	" gzip'ed objects but delivered gunzipped, will run into many of"
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
	"\t%s %d %s [%u]\n"
	"\t|  |  |   |\n"
	"\t|  |  |   +- Child task sub-level\n"
	"\t|  |  +----- Reason\n"
	"\t|  +-------- Child vxid\n"
	"\t+----------- Child type (\"sess\", \"req\" or \"bereq\")\n"
	"\n"
)

SLTM(Begin, 0, "Marks the start of a VXID",
	"The first record of a VXID transaction.\n\n"
	"The format is::\n\n"
	"\t%s %d %s [%u]\n"
	"\t|  |  |   |\n"
	"\t|  |  |   +- Task sub-level\n"
	"\t|  |  +----- Reason\n"
	"\t|  +-------- Parent vxid\n"
	"\t+----------- Type (\"sess\", \"req\" or \"bereq\")\n"
	"\n"
)

SLTM(End, 0, "Marks the end of a VXID",
	"The last record of a VXID transaction.\n\n"
)

SLTM(VSL, 0, "VSL API warnings and error message",
	"Warnings and error messages generated by the VSL API while"
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
	" was logged. See the TIMESTAMPS section below for information about"
	" the individual time stamps.\n\n"
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
	"Contains byte counts for the request handling.\n"
	"The body bytes count includes transmission overhead"
	" (ie: chunked encoding).\n"
	"ESI sub-requests show the body bytes this ESI fragment including"
	" any subfragments contributed to the top level request.\n"
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

SLTM(VfpAcct, 0, "Fetch filter accounting",
	"Contains name of VFP and statistics.\n\n"
	"The format is::\n\n"
	"\t%s %d %d\n"
	"\t|  |  |\n"
	"\t|  |  +- Total bytes produced\n"
	"\t|  +---- Number of calls made\n"
	"\t+------- Name of filter\n"
	"\n"
	NODEF_NOTICE
)

SLTM(Witness, 0, "Lock order witness records",
	"Diagnostic recording of locking order.\n"
)

SLTM(H2RxHdr, SLT_F_BINARY, "Received HTTP2 frame header",
	"Binary data"
)

SLTM(H2RxBody, SLT_F_BINARY, "Received HTTP2 frame body",
	"Binary data"
)

SLTM(H2TxHdr, SLT_F_BINARY, "Transmitted HTTP2 frame header",
	"Binary data"
)

SLTM(H2TxBody, SLT_F_BINARY, "Transmitted HTTP2 frame body",
	"Binary data"
)

SLTM(HitMiss, 0, "Hit for miss object in cache.",
	"Hit-for-miss object looked up in cache.\n\n"
	"The format is::\n\n"
	"\t%u %f\n"
	"\t|  |\n"
	"\t|  +- Remaining TTL\n"
	"\t+---- VXID of the object\n"
	"\n"
)

SLTM(Filters, 0, "Body filters",
	"List of filters applied to the body"
)

SLTM(SessError, 0, "Client connection accept failed",
	"Accepting a client connection has failed.\n\n"
	"The format is::\n\n"
	"\t%s %s %s %d %d %s\n"
	"\t|  |  |  |  |  |\n"
	"\t|  |  |  |  |  +- Detailed error message\n"
	"\t|  |  |  |  +---- Error Number (errno) from accept(2)\n"
	"\t|  |  |  +------- File descriptor number\n"
	"\t|  |  +---------- Local TCP port / 0 for UDS\n"
	"\t|  +------------- Local IPv4/6 address / 0.0.0.0 for UDS\n"
	"\t+---------------- Socket name (from -a argument)\n"
	"\n"
)

SLTM(VCL_use, 0, "VCL in use",
	"Records the name of the VCL being used.\n\n"
	"The format is::\n\n"
	"\t%s [ %s %s ]\n"
	"\t|    |  |\n"
	"\t|    |  +- Name of label used to find it\n"
	"\t|    +---- \"via\"\n"
	"\t+--------- Name of VCL put in use\n"
	"\n"
)

SLTM(Notice, 0, "Informational messages about request handling",
	"Informational log messages on events occurred during request"
	" handling.\n\n"
	"The format is::\n\n"
	"\t%s: %s\n"
	"\t|   |\n"
	"\t|   +- Short description of the notice message\n"
	"\t+----- Manual page containing the detailed description\n"
	"\n"
	"See the NOTICE MESSAGES section below or the individual VMOD manual"
	" pages for detailed information of notice messages.\n"
	"\n"
)

SLTM(VdpAcct, 0, "Deliver filter accounting",
	"Contains name of VDP and statistics.\n\n"
	"The format is::\n\n"
	"\t%s %d %d\n"
	"\t|  |  |\n"
	"\t|  |  +- Total bytes produced\n"
	"\t|  +---- Number of calls made\n"
	"\t+------- Name of filter\n"
	"\n"
	NODEF_NOTICE
)


#undef NOSUP_NOTICE
#undef NODEF_NOTICE
#undef SLTM

/*lint -restore */
