/*-
 * Copyright (c) 2008-2016 Varnish Software AS
 * All rights reserved.
 *
 * Author: Guillaume Quintard <guillaume.quintard@gmail.com>
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
 */

#define ITER_DONE(iter) (iter->buf == iter->end ? hpk_done : hpk_more)

struct dynhdr {
	struct hpk_hdr		header;
	VTAILQ_ENTRY(dynhdr)	list;
};

VTAILQ_HEAD(dynamic_table,dynhdr);

struct hpk_iter {
	struct hpk_ctx		*ctx;
	uint8_t			*orig;
	uint8_t			*buf;
	uint8_t			*end;
};

const struct txt * tbl_get_key(const struct hpk_ctx *ctx, uint32_t index);

const struct txt * tbl_get_value(const struct hpk_ctx *ctx, uint32_t index);
void push_header (struct hpk_ctx *ctx, const struct hpk_hdr *h);
