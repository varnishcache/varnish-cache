/*-
 * Copyright (c) 2012 Varnish Software AS
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
 * Define the VSL tags for HTTP protocol messages
 *
 * NB: The order of this table is not random, DO NOT RESORT.
 *
 * Specifically FIRST, UNSET and LOST entries must be last, in that order.
 *
 * See bin/varnishd/cache/cache_http.c::http_VSLH() for the other side.
 *
 * Arguments:
 *	Tag-Name
 *	struct http header index
 *	1 if this header is used in requests
 *	1 if this header is used in responses
 *	short description postfix
 *	long description (in RST "definition list" format)
 *
 */

SLTH(Method,	HTTP_HDR_METHOD,	1, 0, "method",
	"The HTTP request method used.\n\n"
)
SLTH(URL,	HTTP_HDR_URL,		1, 0, "URL",
	"The HTTP request URL.\n\n"
)
SLTH(Protocol,	HTTP_HDR_PROTO,		1, 1, "protocol",
	"The HTTP protocol version information.\n\n"
)
SLTH(Status,	HTTP_HDR_STATUS,	0, 1, "status",
	"The HTTP status code received.\n\n"
)
SLTH(Response,	HTTP_HDR_RESPONSE,	0, 1, "response",
	"The HTTP response string received.\n\n"
)
SLTH(Header,	HTTP_HDR_FIRST,		1, 1, "header",
	"HTTP header contents.\n\n"
	"The format is::\n\n"
	"\t%s: %s\n"
	"\t|   |\n"
	"\t|   +- Header value\n"
	"\t+----- Header name\n"
	"\n"
)
SLTH(Unset,	HTTP_HDR_UNSET,		0, 0, "unset header",
	"HTTP header contents.\n\n"
	"The format is::\n\n"
	"\t%s: %s\n"
	"\t|   |\n"
	"\t|   +- Header value\n"
	"\t+----- Header name\n"
	"\n"
)
SLTH(Lost,	HTTP_HDR_LOST,		0, 0, "lost header",
	""
)
