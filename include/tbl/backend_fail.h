/*-
 * Copyright (c) 2019 Varnish Software
 * All rights reserved.
 *
 * Author: Steven Wojcik <swojcik@varnish-software.com>
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

/* upper, lower, doc */
BACKEND_FAIL(UNSPEC, unspec,			"unspecified")
BACKEND_FAIL(SICK, sick,			"backend sick")
BACKEND_FAIL(BUSY, busy,			"backend busy")
BACKEND_FAIL(WORKSPACE, workspace,		"out of workspace")
BACKEND_FAIL(FIRST_BYTE, first_byte,		"first byte timeout")
BACKEND_FAIL(CONNECT, connect,			"connect timeout")
BACKEND_FAIL(BETWEEN_BYTES, between_bytes,	"between bytes timeout")
BACKEND_FAIL(REQBODY_ERROR, reqbody_error,	"reqbody read error")
BACKEND_FAIL(WRITE_ERROR, write_error,		"backend write error")
BACKEND_FAIL(RX_JUNK, rx_junk,			"recieved junk")
BACKEND_FAIL(RX_BAD, rx_bad,			"recieved bad response")
BACKEND_FAIL(CLOSE, close,			"backend close")
BACKEND_FAIL(OVERFLOW, overflow,		"overflow")

#undef BACKEND_FAIL

/*lint -restore */
