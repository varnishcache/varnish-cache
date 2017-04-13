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
 * RFC7540 section 11.2
 */

/*lint -save -e525 -e539 */

#ifdef H2_FRAME
/*	   lower,               upper,         type, flags
 *	rx_stream-zero
 *	rx_stream-nonzero
 *	rx_stream-idle
 *	tx_flow-control			// rfc7540,l,1265,1270
 *	tx_continuation
 *	tx_final-flags
 *	overhead
 */
  H2_FRAME(data,		DATA,		0x0, 0x09,
	H2CE_PROTOCOL_ERROR,		// rfc7540,l,1758,1761
	0,
	H2CE_PROTOCOL_ERROR,
	1,
	H2_F_DATA,
	0x01,				// rfc7540,l,1750,1753
	0
  )
  H2_FRAME(headers,		HEADERS,	0x1, 0x2d,
	H2CE_PROTOCOL_ERROR,		// rfc7540,l,1876,1879
	0,
	0,				// rfc7540,l,938,940
	0,
	H2_F_CONTINUATION,
	0x04,				// rfc7540,l,1855,1857
	0
  )
  H2_FRAME(priority,		PRIORITY,	0x2, 0x00,
	H2CE_PROTOCOL_ERROR,		// rfc7540,l,1933,1936
	0,
	0,				// rfc7540,l,938,940
	0,
	0,
	0,
	1
  )
  H2_FRAME(rst_stream,		RST_STREAM,	0x3, 0x00,
	H2CE_PROTOCOL_ERROR,		// rfc7540,l,1993,1996
	0,
	H2CE_PROTOCOL_ERROR,
	0,
	0,
	0,
	1
  )
  H2_FRAME(settings,		SETTINGS,	0x4, 0x01,
	0,
	H2CE_PROTOCOL_ERROR,		// rfc7540,l,2052,2056
	H2CE_PROTOCOL_ERROR,
	0,
	0,
	0,
	1
  )
  H2_FRAME(push_promise,	PUSH_PROMISE,	0x5, 0x0c,
	H2CE_PROTOCOL_ERROR,		// rfc7540,l,2262,2263
	0,
	H2CE_PROTOCOL_ERROR,
	0,
	H2_F_CONTINUATION,
	0x04,				// rfc7540,l,2249,2251
	2
  )
  H2_FRAME(ping,		PING,		0x6, 0x01,
	0,
	H2CE_PROTOCOL_ERROR,		// rfc7540,l,2359,2362
	H2CE_PROTOCOL_ERROR,
	0,
	0,
	0,
	1
  )
  H2_FRAME(goaway,		GOAWAY,		0x7, 0x00,
	0,
	H2CE_PROTOCOL_ERROR,		// rfc7540,l,2432,2435
	H2CE_PROTOCOL_ERROR,
	0,
	0,
	0,
	1
  )
  H2_FRAME(window_update,	WINDOW_UPDATE,	0x8, 0x00,
	0,
	0,
	H2CE_PROTOCOL_ERROR,
	0,
	0,
	0,
	1
  )
  H2_FRAME(continuation,	CONTINUATION,	0x9, 0x04,
	H2CE_PROTOCOL_ERROR,		// rfc7540,l,2764,2767
	0,
	H2CE_PROTOCOL_ERROR,
	0,
	H2_F_CONTINUATION,
	0x04,				// rfc7540,l,2753,2754
	0
  )
  #undef H2_FRAME
#endif

#ifdef H2_FRAME_FLAGS
/*		 lower,			upper,				flag */
  H2_FRAME_FLAGS(none,			NONE,				0x00)
  H2_FRAME_FLAGS(data_end_stream,	DATA_END_STREAM,		0x01)
  H2_FRAME_FLAGS(data_padded,		DATA_PADDED,			0x08)
  H2_FRAME_FLAGS(headers_end_stream,	HEADERS_END_STREAM,		0x01)
  H2_FRAME_FLAGS(headers_end_headers,	HEADERS_END_HEADERS,		0x04)
  H2_FRAME_FLAGS(headers_padded,	HEADERS_PADDED,			0x08)
  H2_FRAME_FLAGS(headers_priority,	HEADERS_PRIORITY,		0x20)
  H2_FRAME_FLAGS(settings_ack,		SETTINGS_ACK,			0x01)
  H2_FRAME_FLAGS(push_promise_end_headers,PUSH_PROMISE_END_HEADERS,	0x04)
  H2_FRAME_FLAGS(push_promise_padded,	PUSH_PROMISE_PADDED,		0x08)
  H2_FRAME_FLAGS(ping_ack,		PING_ACK,			0x01)
  H2_FRAME_FLAGS(continuation_end_headers,CONTINUATION_END_HEADERS,	0x04)
  #undef H2_FRAME_FLAGS
#endif

/*lint -restore */
