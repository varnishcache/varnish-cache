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
 */

/*lint -save -e525 -e539 */

// stream_close_t	  sc_* stat	is_err	Description
SESS_CLOSE(REM_CLOSE,	  rem_close,	0,	"Client Closed")
SESS_CLOSE(REQ_CLOSE,	  req_close,	0,	"Client requested close")
SESS_CLOSE(REQ_HTTP10,	  req_http10,	1,	"Proto < HTTP/1.1")
SESS_CLOSE(RX_BAD,	  rx_bad,	1,	"Received bad req/resp")
SESS_CLOSE(RX_BODY,	  rx_body,	1,	"Failure receiving body")
SESS_CLOSE(RX_JUNK,	  rx_junk,	1,	"Received junk data")
SESS_CLOSE(RX_OVERFLOW,   rx_overflow,	1,	"Received buffer overflow")
SESS_CLOSE(RX_TIMEOUT,	  rx_timeout,	1,	"Receive timeout")
SESS_CLOSE(RX_CLOSE_IDLE, rx_close_idle,0,	"timeout_idle reached")
SESS_CLOSE(TX_PIPE,	  tx_pipe,	0,	"Piped transaction")
SESS_CLOSE(TX_ERROR,	  tx_error,	1,	"Error transaction")
SESS_CLOSE(TX_EOF,	  tx_eof,	0,	"EOF transmission")
SESS_CLOSE(RESP_CLOSE,	  resp_close,	0,	"Backend/VCL requested close")
SESS_CLOSE(OVERLOAD,	  overload,	1,	"Out of some resource")
SESS_CLOSE(PIPE_OVERFLOW, pipe_overflow,1,	"Session pipe overflow")
SESS_CLOSE(RANGE_SHORT,   range_short,	1,	"Insufficient data for range")
SESS_CLOSE(REQ_HTTP20,	  req_http20,	1,	"HTTP2 not accepted")
SESS_CLOSE(VCL_FAILURE,	  vcl_failure,	1,	"VCL failure")
SESS_CLOSE(TRAFFIC_REFUSE,traffic_refuse,0,	"Not accepting new traffic")
#undef SESS_CLOSE

/*lint -restore */
