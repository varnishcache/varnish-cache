/*-
 * Copyright (c) 2016 Varnish Software
 * All rights reserved.
 *
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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

VHD_RET(ERR_ARG,	-1, "Invalid HPACK instruction")
VHD_RET(ERR_INT,	-2, "Integer overflow")
VHD_RET(ERR_IDX,	-3, "Invalid table index")
VHD_RET(ERR_LEN,	-4, "Invalid length")
VHD_RET(ERR_HUF,	-5, "Invalid huffman code")
VHD_RET(ERR_UPD,	-6, "Spurious update")
VHD_RET(OK,		 0, "OK")
VHD_RET(MORE,		 1, "Feed me")
VHD_RET(NAME,		 2, "Name")
VHD_RET(VALUE,		 3, "Value")
VHD_RET(NAME_SEC,	 4, "Name never index")
VHD_RET(VALUE_SEC,	 5, "Value never index")
VHD_RET(BUF,		 6, "Stuffed")
VHD_RET(AGAIN,		 7, "Call again")
#undef VHD_RET

/*lint -restore */
