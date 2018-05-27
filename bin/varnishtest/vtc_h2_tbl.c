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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vdef.h"

#include "vas.h"
#include "vqueue.h"

#include "hpack.h"
#include "vtc_h2_priv.h"

/* TODO: fix that crazy workaround */
#define STAT_HDRS(i, k, v) \
	static char key_ ## i[] = k; \
	static char value_ ## i[] = v;
#include "vtc_h2_stattbl.h"
#undef STAT_HDRS

/*lint -save -e778 */
static const struct hpk_hdr sttbl[] = {
	{{NULL, 0, 0}, {NULL, 0, 0}, hpk_idx, 0},
#define STAT_HDRS(j, k, v) \
{ \
	.key = { \
		.ptr = key_ ## j, \
		.len = sizeof(k) - 1, \
		.huff = 0 \
	}, \
	.value = { \
		.ptr = value_ ## j, \
		.len = sizeof(v) - 1, \
		.huff = 0 \
	}, \
	.t = hpk_idx, \
	.i = j, \
},
#include "vtc_h2_stattbl.h"
#undef STAT_HDRS
};
/*lint -restore */

struct hpk_ctx {
	const struct hpk_hdr *sttbl;
	struct dynamic_table      dyntbl;
	uint32_t maxsize;
	uint32_t size;
};


struct hpk_iter *
HPK_NewIter(struct hpk_ctx *ctx, void *buf, int size)
{
	struct hpk_iter *iter = malloc(sizeof(*iter));
	assert(iter);
	assert(ctx);
	assert(buf);
	assert(size);
	iter->ctx = ctx;
	iter->orig = buf;
	iter->buf = buf;
	iter->end = iter->buf + size;
	return (iter);
}

void
HPK_FreeIter(struct hpk_iter *iter)
{
	free(iter);
}

static void
pop_header(struct hpk_ctx *ctx)
{
	assert(!VTAILQ_EMPTY(&ctx->dyntbl));
	struct dynhdr *h = VTAILQ_LAST(&ctx->dyntbl, dynamic_table);
	VTAILQ_REMOVE(&ctx->dyntbl, h, list);
	ctx->size -= h->header.key.len + h->header.value.len + 32;
	free(h->header.key.ptr);
	free(h->header.value.ptr);
	free(h);
}

void
push_header (struct hpk_ctx *ctx, const struct hpk_hdr *oh)
{
	const struct hpk_hdr *ih;
	struct dynhdr *h;
	uint32_t len;

	assert(ctx->size <= ctx->maxsize);
	AN(oh);

	if (!ctx->maxsize)
		return;
	len = oh->value.len + 32;
	if (oh->key.ptr)
		len += oh->key.len;
	else {
		AN(oh->i);
		ih = HPK_GetHdr(ctx, oh->i);
		AN(ih);
		len += ih->key.len;
	}

	while (!VTAILQ_EMPTY(&ctx->dyntbl) && ctx->maxsize - ctx->size < len)
		pop_header(ctx);
	if (ctx->maxsize - ctx->size >= len) {
		h = malloc(sizeof(*h));
		AN(h);
		h->header.t = hpk_idx;

		if (oh->key.ptr) {
			h->header.key.len = oh->key.len;
			h->header.key.ptr = malloc(oh->key.len + 1L);
			AN(h->header.key.ptr);
			memcpy(h->header.key.ptr,
			    oh->key.ptr, oh->key.len + 1L);
		} else {
			AN(oh->i);
			ih = HPK_GetHdr(ctx, oh->i);
			AN(ih);

			h->header.key.len = ih->key.len;
			h->header.key.ptr = malloc(ih->key.len + 1L);
			AN(h->header.key.ptr);
			memcpy(h->header.key.ptr,
			    ih->key.ptr, ih->key.len + 1L);
		}

		h->header.value.len = oh->value.len;
		h->header.value.ptr = malloc(oh->value.len + 1L);
		AN(h->header.value.ptr);
		memcpy(h->header.value.ptr, oh->value.ptr, oh->value.len + 1L);

		VTAILQ_INSERT_HEAD(&ctx->dyntbl, h, list);
		ctx->size += len;
	}

}

enum hpk_result
HPK_ResizeTbl(struct hpk_ctx *ctx, uint32_t num)
{
	ctx->maxsize = num;
	while (!VTAILQ_EMPTY(&ctx->dyntbl) && ctx->maxsize < ctx->size)
		pop_header(ctx);
	return (hpk_done);
}

static const struct txt *
tbl_get_field(const struct hpk_ctx *ctx, uint32_t idx, int key)
{
	struct dynhdr *dh;
	assert(ctx);
	if (idx > 61 + ctx->size)
		return (NULL);
	else if (idx <= 61) {
		if (key)
			return (&ctx->sttbl[idx].key);
		else
			return (&ctx->sttbl[idx].value);
	}

	idx -= 62;
	VTAILQ_FOREACH(dh, &ctx->dyntbl, list)
		if (!idx--)
			break;
	if (idx && dh) {
		if (key)
			return (&dh->header.key);
		else
			return (&dh->header.value);
	} else
		return (NULL);
}

const struct txt *
tbl_get_key(const struct hpk_ctx *ctx, uint32_t idx)
{
	return (tbl_get_field(ctx, idx, 1));
}

const struct txt *
tbl_get_value(const struct hpk_ctx *ctx, uint32_t idx)
{
	return (tbl_get_field(ctx, idx, 0));
}

const struct hpk_hdr *
HPK_GetHdr(const struct hpk_ctx *ctx, uint32_t idx)
{
	uint32_t oi = idx;
	struct dynhdr *dh;
	assert(ctx);
	if (idx > 61 + ctx->size)
		return (NULL);
	else if (idx <= 61)
		return (&ctx->sttbl[idx]);

	idx -= 62;
	VTAILQ_FOREACH(dh, &ctx->dyntbl, list)
		if (!idx--)
			break;
	if (idx && dh) {
		dh->header.i = oi;
		return (&dh->header);
	} else
		return (NULL);
}

uint32_t
HPK_GetTblSize(const struct hpk_ctx *ctx)
{
	return (ctx->size);
}

uint32_t
HPK_GetTblMaxSize(const struct hpk_ctx *ctx)
{
	return (ctx->maxsize);
}

uint32_t
HPK_GetTblLength(const struct hpk_ctx *ctx)
{
	struct dynhdr *dh;
	uint32_t l = 0;
	VTAILQ_FOREACH(dh, &ctx->dyntbl, list)
		l++;
	return (l);
}

#if 0
void
dump_dyn_tbl(const struct hpk_ctx *ctx)
{
	int i = 0;
	struct dynhdr *dh;
	printf("DUMPING %u/%u\n", ctx->size, ctx->maxsize);
	VTAILQ_FOREACH(dh, &ctx->dyntbl, list) {
		printf(" (%d) %s: %s\n",
		    i++, dh->header.key.ptr, dh->header.value.ptr);
	}
	printf("DONE\n");
}
#endif

struct hpk_ctx *
HPK_NewCtx(uint32_t maxsize)
{
	struct hpk_ctx *ctx = calloc(1, sizeof(*ctx));
	assert(ctx);
	ctx->sttbl = sttbl;
	ctx->maxsize = maxsize;
	ctx->size = 0;
	return (ctx);
}

void
HPK_FreeCtx(struct hpk_ctx *ctx)
{

	while (!VTAILQ_EMPTY(&ctx->dyntbl))
		pop_header(ctx);
	free(ctx);
}
