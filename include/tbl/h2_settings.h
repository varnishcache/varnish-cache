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
 * RFC7540 section 11.3
 *
 * Upper
 * lower
 * tag
 * default
 * min
 * max
 * range_error
 */

/*lint -save -e525 -e539 */

H2_SETTING(					// rfc7540,l,2097,2103
	HEADER_TABLE_SIZE,
	header_table_size,
	0x1,
	4096,					// rfc7540,l,4224,4224
	0,
	0xffffffff,
	0
)

#ifndef H2_SETTINGS_PARAM_ONLY
H2_SETTING(					// rfc7540,l,2105,2114
	ENABLE_PUSH,
	enable_push,
	0x2,
	1,					// rfc7540,l,4225,4225
	0,
	1,
	H2CE_PROTOCOL_ERROR
)
#endif

H2_SETTING(					// rfc7540,l,2116,2121
	MAX_CONCURRENT_STREAMS,
	max_concurrent_streams,
	0x3,
	0xffffffff,				// rfc7540,l,4226,4226
	0,
	0xffffffff,
	0
)

H2_SETTING(					// rfc7540,l,2139,2148
	INITIAL_WINDOW_SIZE,
	initial_window_size,
	0x4,
	65535,					// rfc7540,l,4227,4227
	0,
	0x7fffffff,
	H2CE_FLOW_CONTROL_ERROR
)

H2_SETTING(					// rfc7540,l,2150,2157
	MAX_FRAME_SIZE,
	max_frame_size,
	0x5,
	16384,					// rfc7540,l,4228,4228
	16384,
	0x00ffffff,
	H2CE_PROTOCOL_ERROR
)

H2_SETTING(					// rfc7540,l,2159,2167
	MAX_HEADER_LIST_SIZE,
	max_header_list_size,
	0x6,
	0x7fffffff,				// rfc7540,l,4229,4229
	0,
	0xffffffff,
	0
)
#undef H2_SETTING

/*lint -restore */
