/*-
 * Copyright (c) 2016 Varnish Software AS
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
 * RFC7540 section 11.4
 *
 * Fields: Upper, value, conn=1|stream=2, description
 */

/*lint -save -e525 -e539 */

H2_ERROR(NO_ERROR,	       0,3, "Graceful shutdown")
H2_ERROR(PROTOCOL_ERROR,       1,3, "Protocol error detected")
H2_ERROR(INTERNAL_ERROR,       2,3, "Implementation fault")
H2_ERROR(FLOW_CONTROL_ERROR,   3,3, "Flow-control limits exceeded")
H2_ERROR(SETTINGS_TIMEOUT,     4,1, "Settings not acknowledged")
H2_ERROR(STREAM_CLOSED,	       5,2, "Frame received for closed stream")
H2_ERROR(FRAME_SIZE_ERROR,     6,3, "Frame size incorrect")
H2_ERROR(REFUSED_STREAM,       7,2, "Stream not processed")
H2_ERROR(CANCEL,	       8,2, "Stream cancelled")
H2_ERROR(COMPRESSION_ERROR,    9,1, "Compression state not updated")
H2_ERROR(CONNECT_ERROR,	      10,2, "TCP connection error for CONNECT method")
H2_ERROR(ENHANCE_YOUR_CALM,   11,3, "Processing capacity exceeded")
H2_ERROR(INADEQUATE_SECURITY, 12,1, "Negotiated TLS parameters not acceptable")
H2_ERROR(HTTP_1_1_REQUIRED,   13,1, "Use HTTP/1.1 for the request")
#undef H2_ERROR

/*lint -restore */
