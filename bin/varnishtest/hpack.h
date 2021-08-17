/*-
 * Copyright (c) 2008-2016 Varnish Software AS
 * All rights reserved.
 *
 * Author: Guillaume Quintard <guillaume.quintard@gmail.com>
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
 */

#include <stdint.h>

enum hpk_result{
	hpk_more = 0,
	hpk_done,
	hpk_err,
};

enum hpk_indexed {
	hpk_unset = 0,
	hpk_idx,
	hpk_inc,
	hpk_not,
	hpk_never,
};

struct hpk_txt {
	char *ptr;
	int len;
	int huff;
};

struct hpk_hdr {
	struct hpk_txt key;
	struct hpk_txt value;
	enum hpk_indexed t;
	unsigned i;
};

struct hpk_ctx;
struct hpk_iter;

struct hpk_ctx * HPK_NewCtx(uint32_t tblsize);
void HPK_FreeCtx(struct hpk_ctx *ctx);

struct hpk_iter * HPK_NewIter(struct hpk_ctx *ctx, void *buf, int size);
void HPK_FreeIter(struct hpk_iter *iter);

enum hpk_result HPK_DecHdr(struct hpk_iter *iter, struct hpk_hdr *header);
enum hpk_result HPK_EncHdr(struct hpk_iter *iter, const struct hpk_hdr *header);

int gethpk_iterLen(const struct hpk_iter *iter);

enum hpk_result HPK_ResizeTbl(struct hpk_ctx *ctx, uint32_t num);

const struct hpk_hdr * HPK_GetHdr(const struct hpk_ctx *ctx, uint32_t index);

uint32_t HPK_GetTblSize(const struct hpk_ctx *ctx);
uint32_t HPK_GetTblMaxSize(const struct hpk_ctx *ctx);
uint32_t HPK_GetTblLength(const struct hpk_ctx *ctx);

#if 0
/* DEBUG */
void dump_dyn_tbl(const struct hpk_ctx *ctx);
#endif
