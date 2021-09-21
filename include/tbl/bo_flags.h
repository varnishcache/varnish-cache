/*-
 * Copyright (c) 2014-2015 Varnish Software AS
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

/*
 * filters: whether this flag determines beresp.filters default
 *
 * lower, vcl_r, vcl_beresp_r, vcl_beresp_w, filters, doc */
BO_FLAG(do_esi,		0, 1, 1, 1, "")
BO_FLAG(do_gzip,	0, 1, 1, 1, "")
BO_FLAG(do_gunzip,	0, 1, 1, 1, "")
BO_FLAG(do_stream,	0, 1, 1, 0, "")
BO_FLAG(uncacheable,	0, 0, 0, 0, "")
BO_FLAG(was_304,	0, 1, 0, 0, "")
BO_FLAG(is_bgfetch,	1, 0, 0, 0, "")
BO_FLAG(is_hitmiss,	1, 0, 0, 0, "")
BO_FLAG(is_hitpass,	1, 0, 0, 0, "")
BO_FLAG(send_failed,	1, 0, 0, 0, "")
BO_FLAG(fetch_failed,	0, 1, 0, 0, "")
#undef BO_FLAG

/*lint -restore */
