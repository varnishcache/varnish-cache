/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 * HTC status values
 */

/*lint -save -e525 -e539 */

// enum htc_status_e	n	short		long
HTC_STATUS(JUNK,	-5,	"junk",		"Received unexpected data")
HTC_STATUS(CLOSE,	-4,	"close",	"Connection closed") // unused?
HTC_STATUS(TIMEOUT,	-3,	"timeout",	"Timed out")
HTC_STATUS(OVERFLOW,	-2,	"overflow",	"Buffer/workspace too small")
HTC_STATUS(EOF,		-1,	"eof",		"Unexpected end of input")
HTC_STATUS(EMPTY,	 0,	"empty",	"Empty response")
HTC_STATUS(MORE,	 1,	"more",		"More data required")
HTC_STATUS(COMPLETE,	 2,	"complete",	"Data complete (no error)")
HTC_STATUS(IDLE,	 3,	"idle",		"Connection was closed while idle")
#undef HTC_STATUS

/*lint -restore */
