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
 */

/*lint -save -e525 -e539 */

SESS_CLOSE(REM_CLOSE,	"Client Closed")
SESS_CLOSE(REQ_CLOSE,	"Client requested close")
SESS_CLOSE(REQ_HTTP10,	"Proto < HTTP/1.1")
SESS_CLOSE(RX_BODY,	"Failure receiving req.body")
SESS_CLOSE(RX_JUNK,	"Received junk data")
SESS_CLOSE(RX_OVERFLOW,	"Received buffer overflow")
SESS_CLOSE(RX_TIMEOUT,	"Receive timeout")
SESS_CLOSE(TX_PIPE,	"Piped transaction")
SESS_CLOSE(TX_ERROR,	"Error transaction")
SESS_CLOSE(TX_EOF,	"EOF transmission")
SESS_CLOSE(RESP_CLOSE,	"Backend/VCL requested close")
SESS_CLOSE(OVERLOAD,	"Out of some resource")
SESS_CLOSE(SESS_PIPE_OVERFLOW,	"Session pipe overflow")

/*lint -restore */
