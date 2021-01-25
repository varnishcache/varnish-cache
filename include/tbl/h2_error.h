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
 * Types: conn=1|stream=2
 */

/*lint -save -e525 -e539 */

H2_ERROR(
	/* name */	NO_ERROR,
	/* val */	0,
	/* types */	3,
	/* descr */	"Graceful shutdown"
)

H2_ERROR(
	/* name */	PROTOCOL_ERROR,
	/* val */	1,
	/* types */	3,
	/* descr */	"Protocol error detected"
)

H2_ERROR(
	/* name */	INTERNAL_ERROR,
	/* val */	2,
	/* types */	3,
	/* descr */	"Implementation fault"
)

H2_ERROR(
	/* name */	FLOW_CONTROL_ERROR,
	/* val */	3,
	/* types */	3,
	/* descr */	"Flow-control limits exceeded"
)

H2_ERROR(
	/* name */	SETTINGS_TIMEOUT,
	/* val */	4,
	/* types */	1,
	/* descr */	"Settings not acknowledged"
)

H2_ERROR(
	/* name */	STREAM_CLOSED,
	/* val */	5,
	/* types */	2,
	/* descr */	"Frame received for closed stream"
)

H2_ERROR(
	/* name */	FRAME_SIZE_ERROR,
	/* val */	6,
	/* types */	3,
	/* descr */	"Frame size incorrect"
)

H2_ERROR(
	/* name */	REFUSED_STREAM,
	/* val */	7,
	/* types */	2,
	/* descr */	"Stream not processed"
)

H2_ERROR(
	/* name */	CANCEL,
	/* val */	8,
	/* types */	2,
	/* descr */	"Stream cancelled"
)

H2_ERROR(
	/* name */	COMPRESSION_ERROR,
	/* val */	9,
	/* types */	1,
	/* descr */	"Compression state not updated"
)

H2_ERROR(
	/* name */	CONNECT_ERROR,
	/* val */	10,
	/* types */	2,
	/* descr */	"TCP connection error for CONNECT method"
)

H2_ERROR(
	/* name */	ENHANCE_YOUR_CALM,
	/* val */	11,
	/* types */	3,
	/* descr */	"Processing capacity exceeded"
)

H2_ERROR(
	/* name */	INADEQUATE_SECURITY,
	/* val */	12,
	/* types */	1,
	/* descr */	"Negotiated TLS parameters not acceptable"
)

H2_ERROR(
	/* name */	HTTP_1_1_REQUIRED,
	/* val */	13,
	/* types */	1,
	/* descr */	"Use HTTP/1.1 for the request"
)

#undef H2_ERROR
/*lint -restore */
