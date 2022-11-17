/*-
 * Copyright (c) 2012 Varnish Software AS
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
 * Fields in the feature parameter
 *
 */

/*lint -save -e525 -e539 */

FEATURE_BIT(HTTP2,		http2,
    "Enable HTTP/2 protocol support."
)

FEATURE_BIT(SHORT_PANIC,		short_panic,
    "Short panic message."
)

FEATURE_BIT(NO_COREDUMP,		no_coredump,
    "No coredumps.  Must be set before child process starts."
)

FEATURE_BIT(HTTPS_SCHEME,		https_scheme,
    "Extract host from full URI in the HTTP/1 request line, if the scheme is https."
)

FEATURE_BIT(HTTP_DATE_POSTEL,	http_date_postel,
    "Tolerate non compliant timestamp headers "
    "like `Date`, `Last-Modified`, `Expires` etc."
)

FEATURE_BIT(ESI_IGNORE_HTTPS,		esi_ignore_https,
    "Convert `<esi:include src\"https://...` to `http://...`"
)

FEATURE_BIT(ESI_DISABLE_XML_CHECK,	esi_disable_xml_check,
    "Allow ESI processing on non-XML ESI bodies"
)

FEATURE_BIT(ESI_IGNORE_OTHER_ELEMENTS,	esi_ignore_other_elements,
    "Ignore XML syntax errors in ESI bodies."
)

FEATURE_BIT(ESI_REMOVE_BOM,		esi_remove_bom,
    "Ignore UTF-8 BOM in ESI bodies."
)

FEATURE_BIT(ESI_INCLUDE_ONERROR,	esi_include_onerror,
    "Parse the onerror attribute of <esi:include> tags."
)

FEATURE_BIT(WAIT_SILO,			wait_silo,
    "Wait for persistent silos to completely load before serving requests."
)

FEATURE_BIT(VALIDATE_HEADERS,		validate_headers,
    "Validate all header set operations to conform to RFC7230."
)

FEATURE_BIT(BUSY_STATS_RATE,	busy_stats_rate,
    "Make busy workers comply with thread_stats_rate."
)

FEATURE_BIT(TRACE,			trace,
    "Enable VCL tracing by default (enable (be)req.trace). "
    "Required for tracing vcl_init / vcl_fini"
)

FEATURE_BIT(VCL_REQ_RESET,			vcl_req_reset,
    "Stop processing client VCL once the client is gone. "
    "When this happens MAIN.req_reset is incremented."
)

FEATURE_BIT(VALIDATE_CLIENT_RESPONSES,	validate_client_responses,
    "Check client HTTP responses for invalid characters."
    " All HTTP responses will be checked for illegal characters set by the"
    " VCL program before sending. Failures will cause a VCL_Error state"
    " to be logged, and `vcl_synth` to be called."
)

FEATURE_BIT(VALIDATE_BACKEND_REQUESTS,	validate_backend_requests,
    "Check backend HTTP requests for invalid characters."
    " All backend HTTP requests will be checked for illegal characters set"
    " by the VCL program before sending. Failures will cause a VCL_Error"
    " state to be logged, and `vcl_backend_error` to be called."
)

#undef FEATURE_BIT

/*lint -restore */
