/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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

#include "config.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vdef.h"
#include "vas.h"
#include "miniobj.h"

#include "vqueue.h"
#include "vre.h"
#include "vtim.h"
#include "vtree.h"

#include "vapi/vsl.h"

#include "vsl_api.h"

#define VTX_CACHE 10
#define VTX_BUFSIZE_MIN 64
#define VTX_SHMCHUNKS 3

static const char * const vsl_t_names[VSL_t__MAX] = {
	[VSL_t_unknown]	= "unknown",
	[VSL_t_sess]	= "sess",
	[VSL_t_req]	= "req",
	[VSL_t_bereq]	= "bereq",
	[VSL_t_raw]	= "raw",
};

static const char * const vsl_r_names[VSL_r__MAX] = {
	[VSL_r_unknown]	= "unknown",
	[VSL_r_http_1]	= "HTTP/1",
	[VSL_r_rxreq]	= "rxreq",
	[VSL_r_esi]	= "esi",
	[VSL_r_restart]	= "restart",
	[VSL_r_pass]	= "pass",
	[VSL_r_fetch]	= "fetch",
	[VSL_r_bgfetch]	= "bgfetch",
	[VSL_r_pipe]	= "pipe",
};

struct vtx;
VTAILQ_HEAD(vtxhead, vtx);

struct vslc_raw {
	unsigned		magic;
#define VSLC_RAW_MAGIC		0x247EBD44

	struct VSL_cursor	cursor;

	const uint32_t		*ptr;
};

struct synth {
	unsigned		magic;
#define SYNTH_MAGIC		0xC654479F

	VTAILQ_ENTRY(synth)	list;
	size_t			offset;
	uint32_t		data[VSL_OVERHEAD + VSL_WORDS(64)];
};
VTAILQ_HEAD(synthhead, synth);

enum chunk_t {
	chunk_t__unassigned,
	chunk_t_shm,
	chunk_t_buf,
};

struct chunk {
	unsigned				magic;
#define CHUNK_MAGIC				0x48DC0194
	enum chunk_t				type;
	union {
		struct {
			struct VSLC_ptr		start;
			VTAILQ_ENTRY(chunk)	shmref;
		} shm;
		struct {
			uint32_t		*data;
			size_t			space;
		} buf;
	};
	size_t					len;
	struct vtx				*vtx;
	VTAILQ_ENTRY(chunk)			list;
};
VTAILQ_HEAD(chunkhead, chunk);

struct vslc_vtx {
	unsigned		magic;
#define VSLC_VTX_MAGIC		0x74C6523F

	struct VSL_cursor	cursor;

	struct vtx		*vtx;
	struct synth		*synth;
	struct chunk		*chunk;
	size_t			chunkstart;
	size_t			offset;
};

struct vtx_key {
	uint64_t		vxid;
	VRBT_ENTRY(vtx_key)	entry;
};
VRBT_HEAD(vtx_tree, vtx_key);

struct vtx {
	struct vtx_key		key;
	unsigned		magic;
#define VTX_MAGIC		0xACC21D09
	VTAILQ_ENTRY(vtx)	list_child;
	VTAILQ_ENTRY(vtx)	list_vtx;

	double			t_start;
	unsigned		flags;
#define VTX_F_BEGIN		0x1 /* Begin record processed */
#define VTX_F_END		0x2 /* End record processed */
#define VTX_F_COMPLETE		0x4 /* Marked complete. No new children
				       should be appended */
#define VTX_F_READY		0x8 /* This vtx and all it's children are
				       complete */

	enum VSL_transaction_e	type;
	enum VSL_reason_e	reason;

	struct vtx		*parent;
	struct vtxhead		child;
	unsigned		n_child;
	unsigned		n_childready;
	unsigned		n_descend;

	struct synthhead	synth;

	struct chunk		shmchunks[VTX_SHMCHUNKS];
	struct chunkhead	shmchunks_free;

	struct chunkhead	chunks;
	size_t			len;

	struct vslc_vtx		c;
};

struct VSLQ {
	unsigned		magic;
#define VSLQ_MAGIC		0x23A8BE97

	struct VSL_data		*vsl;
	struct VSL_cursor	*c;
	struct vslq_query	*query;

	enum VSL_grouping_e	grouping;

	/* Structured mode */
	struct vtx_tree		tree;
	struct vtxhead		ready;
	struct vtxhead		incomplete;
	int			n_outstanding;
	struct chunkhead	shmrefs;
	struct vtxhead		cache;
	unsigned		n_cache;

	/* Rate limiting */
	double			credits;
	vtim_mono		last_use;

	/* Raw mode */
	struct {
		struct vslc_raw		c;
		struct VSL_transaction	trans;
		struct VSL_transaction	*ptrans[2];
		struct VSLC_ptr		start;
		ssize_t			len;
		ssize_t			offset;
	} raw;
};

static void vtx_synth_rec(struct vtx *vtx, unsigned tag, const char *fmt, ...);
/*lint -esym(534, vtx_diag) */
static int vtx_diag(struct vtx *vtx, const char *msg);
/*lint -esym(534, vtx_diag_tag) */
static int vtx_diag_tag(struct vtx *vtx, const uint32_t *ptr,
    const char *reason);

static inline int
vtx_keycmp(const struct vtx_key *a, const struct vtx_key *b)
{
	if (a->vxid < b->vxid)
		return (-1);
	if (a->vxid > b->vxid)
		return (1);
	return (0);
}

VRBT_GENERATE_REMOVE_COLOR(vtx_tree, vtx_key, entry, static)
VRBT_GENERATE_REMOVE(vtx_tree, vtx_key, entry, static)
VRBT_GENERATE_INSERT_COLOR(vtx_tree, vtx_key, entry, static)
VRBT_GENERATE_INSERT_FINISH(vtx_tree, vtx_key, entry, static)
VRBT_GENERATE_INSERT(vtx_tree, vtx_key, entry, vtx_keycmp, static)
VRBT_GENERATE_FIND(vtx_tree, vtx_key, entry, vtx_keycmp, static)

static enum vsl_status v_matchproto_(vslc_next_f)
vslc_raw_next(const struct VSL_cursor *cursor)
{
	struct vslc_raw *c;

	CAST_OBJ_NOTNULL(c, cursor->priv_data, VSLC_RAW_MAGIC);
	assert(&c->cursor == cursor);

	AN(c->ptr);
	if (c->cursor.rec.ptr == NULL) {
		c->cursor.rec.ptr = c->ptr;
		return (vsl_more);
	} else {
		c->cursor.rec.ptr = NULL;
		return (vsl_end);
	}
}

static enum vsl_status v_matchproto_(vslc_reset_f)
vslc_raw_reset(const struct VSL_cursor *cursor)
{
	struct vslc_raw *c;

	CAST_OBJ_NOTNULL(c, cursor->priv_data, VSLC_RAW_MAGIC);
	assert(&c->cursor == cursor);

	AN(c->ptr);
	c->cursor.rec.ptr = NULL;

	return (vsl_end);
}

static const struct vslc_tbl vslc_raw_tbl = {
	.magic	= VSLC_TBL_MAGIC,
	.delete	= NULL,
	.next	= vslc_raw_next,
	.reset	= vslc_raw_reset,
	.check	= NULL,
};

static enum vsl_status v_matchproto_(vslc_next_f)
vslc_vtx_next(const struct VSL_cursor *cursor)
{
	struct vslc_vtx *c;
	const uint32_t *ptr;
	unsigned overrun;

	CAST_OBJ_NOTNULL(c, cursor->priv_data, VSLC_VTX_MAGIC);
	assert(&c->cursor == cursor);
	CHECK_OBJ_NOTNULL(c->vtx, VTX_MAGIC);

	do {
		CHECK_OBJ_ORNULL(c->synth, SYNTH_MAGIC);
		if (c->synth != NULL && c->synth->offset == c->offset) {
			/* We're at the offset of the next synth record,
			   point to it and advance the pointer */
			c->cursor.rec.ptr = c->synth->data;
			c->synth = VTAILQ_NEXT(c->synth, list);
		} else {
			overrun = c->offset > c->vtx->len;
			AZ(overrun);
			if (c->offset == c->vtx->len)
				return (vsl_end);

			/* Advance chunk pointer */
			if (c->chunk == NULL) {
				c->chunk = VTAILQ_FIRST(&c->vtx->chunks);
				c->chunkstart = 0;
			}
			CHECK_OBJ_NOTNULL(c->chunk, CHUNK_MAGIC);
			while (c->offset >= c->chunkstart + c->chunk->len) {
				c->chunkstart += c->chunk->len;
				c->chunk = VTAILQ_NEXT(c->chunk, list);
				CHECK_OBJ_NOTNULL(c->chunk, CHUNK_MAGIC);
			}

			/* Point to the next stored record */
			if (c->chunk->type == chunk_t_shm)
				ptr = c->chunk->shm.start.ptr;
			else {
				assert(c->chunk->type == chunk_t_buf);
				ptr = c->chunk->buf.data;
			}
			c->cursor.rec.ptr = ptr + c->offset - c->chunkstart;
			c->offset += VSL_NEXT(c->cursor.rec.ptr) -
			    c->cursor.rec.ptr;
		}
	} while (VSL_TAG(c->cursor.rec.ptr) == SLT__Batch);

	return (vsl_more);
}

static enum vsl_status v_matchproto_(vslc_reset_f)
vslc_vtx_reset(const struct VSL_cursor *cursor)
{
	struct vslc_vtx *c;

	CAST_OBJ_NOTNULL(c, cursor->priv_data, VSLC_VTX_MAGIC);
	assert(&c->cursor == cursor);
	CHECK_OBJ_NOTNULL(c->vtx, VTX_MAGIC);
	c->synth = VTAILQ_FIRST(&c->vtx->synth);
	c->chunk = NULL;
	c->chunkstart = 0;
	c->offset = 0;
	c->cursor.rec.ptr = NULL;

	return (vsl_end);
}

static const struct vslc_tbl vslc_vtx_tbl = {
	.magic	= VSLC_TBL_MAGIC,
	.delete	= NULL,
	.next	= vslc_vtx_next,
	.reset	= vslc_vtx_reset,
	.check	= NULL,
};

/* Create a buf chunk */
static struct chunk *
chunk_newbuf(struct vtx *vtx, const uint32_t *ptr, size_t len)
{
	struct chunk *chunk;

	ALLOC_OBJ(chunk, CHUNK_MAGIC);
	XXXAN(chunk);
	chunk->type = chunk_t_buf;
	chunk->vtx = vtx;
	chunk->buf.space = VTX_BUFSIZE_MIN;
	while (chunk->buf.space < len)
		chunk->buf.space *= 2;
	chunk->buf.data = malloc(sizeof (uint32_t) * chunk->buf.space);
	AN(chunk->buf.data);
	memcpy(chunk->buf.data, ptr, sizeof (uint32_t) * len);
	chunk->len = len;
	return (chunk);
}

/* Free a buf chunk */
static void
chunk_freebuf(struct chunk **pchunk)
{
	struct chunk *chunk;

	TAKE_OBJ_NOTNULL(chunk, pchunk, CHUNK_MAGIC);
	assert(chunk->type == chunk_t_buf);
	free(chunk->buf.data);
	FREE_OBJ(chunk);
}

/* Append a set of records to a chunk */
static void
chunk_appendbuf(struct chunk *chunk, const uint32_t *ptr, size_t len)
{

	CHECK_OBJ_NOTNULL(chunk, CHUNK_MAGIC);
	assert(chunk->type == chunk_t_buf);
	if (chunk->buf.space < chunk->len + len) {
		while (chunk->buf.space < chunk->len + len)
			chunk->buf.space *= 2;
		chunk->buf.data = realloc(chunk->buf.data,
		    sizeof (uint32_t) * chunk->buf.space);
	}
	memcpy(chunk->buf.data + chunk->len, ptr, sizeof (uint32_t) * len);
	chunk->len += len;
}

/* Transform a shm chunk to a buf chunk */
static void
chunk_shm_to_buf(struct VSLQ *vslq, struct chunk *chunk)
{
	struct vtx *vtx;
	struct chunk *buf;

	CHECK_OBJ_NOTNULL(chunk, CHUNK_MAGIC);
	assert(chunk->type == chunk_t_shm);
	vtx = chunk->vtx;
	CHECK_OBJ_NOTNULL(vtx, VTX_MAGIC);

	buf = VTAILQ_PREV(chunk, chunkhead, list);
	if (buf != NULL && buf->type == chunk_t_buf)
		/* Previous is a buf chunk, append to it */
		chunk_appendbuf(buf, chunk->shm.start.ptr, chunk->len);
	else {
		/* Create a new buf chunk and insert it before this */
		buf = chunk_newbuf(vtx, chunk->shm.start.ptr, chunk->len);
		AN(buf);
		VTAILQ_INSERT_BEFORE(chunk, buf, list);
	}

	/* Reset cursor chunk pointer, vslc_vtx_next will set it correctly */
	vtx->c.chunk = NULL;

	/* Remove from the shmref list and vtx, and put chunk back
	   on the free list */
	VTAILQ_REMOVE(&vslq->shmrefs, chunk, shm.shmref);
	VTAILQ_REMOVE(&vtx->chunks, chunk, list);
	VTAILQ_INSERT_HEAD(&vtx->shmchunks_free, chunk, list);
}

/* Append a set of records to a vtx structure */
static enum vsl_status
vtx_append(struct VSLQ *vslq, struct vtx *vtx, const struct VSLC_ptr *start,
    size_t len)
{
	struct chunk *chunk;
	enum vsl_check i;

	AN(vtx);
	AN(len);
	AN(start);

	i = VSL_Check(vslq->c, start);
	if (i == vsl_check_e_inval)
		return (vsl_e_overrun);

	if (i == vsl_check_valid && !VTAILQ_EMPTY(&vtx->shmchunks_free)) {
		/* Shmref it */
		chunk = VTAILQ_FIRST(&vtx->shmchunks_free);
		CHECK_OBJ_NOTNULL(chunk, CHUNK_MAGIC);
		assert(chunk->type == chunk_t_shm);
		assert(chunk->vtx == vtx);
		VTAILQ_REMOVE(&vtx->shmchunks_free, chunk, list);
		chunk->shm.start = *start;
		chunk->len = len;
		VTAILQ_INSERT_TAIL(&vtx->chunks, chunk, list);

		/* Append to shmref list */
		VTAILQ_INSERT_TAIL(&vslq->shmrefs, chunk, shm.shmref);
	} else {
		/* Buffer it */
		chunk = VTAILQ_LAST(&vtx->chunks, chunkhead);
		CHECK_OBJ_ORNULL(chunk, CHUNK_MAGIC);
		if (chunk != NULL && chunk->type == chunk_t_buf) {
			/* Tail is a buf chunk, append to that */
			chunk_appendbuf(chunk, start->ptr, len);
		} else {
			/* Append new buf chunk */
			chunk = chunk_newbuf(vtx, start->ptr, len);
			AN(chunk);
			VTAILQ_INSERT_TAIL(&vtx->chunks, chunk, list);
		}
	}
	vtx->len += len;
	return (vsl_more);
}

/* Allocate a new vtx structure */
static struct vtx *
vtx_new(struct VSLQ *vslq)
{
	struct vtx *vtx;
	int i;

	AN(vslq);
	if (vslq->n_cache) {
		AZ(VTAILQ_EMPTY(&vslq->cache));
		vtx = VTAILQ_FIRST(&vslq->cache);
		VTAILQ_REMOVE(&vslq->cache, vtx, list_child);
		vslq->n_cache--;
	} else {
		ALLOC_OBJ(vtx, VTX_MAGIC);
		AN(vtx);

		VTAILQ_INIT(&vtx->child);
		VTAILQ_INIT(&vtx->shmchunks_free);
		for (i = 0; i < VTX_SHMCHUNKS; i++) {
			vtx->shmchunks[i].magic = CHUNK_MAGIC;
			vtx->shmchunks[i].type = chunk_t_shm;
			vtx->shmchunks[i].vtx = vtx;
			VTAILQ_INSERT_TAIL(&vtx->shmchunks_free,
			    &vtx->shmchunks[i], list);
		}
		VTAILQ_INIT(&vtx->chunks);
		VTAILQ_INIT(&vtx->synth);
		vtx->c.magic = VSLC_VTX_MAGIC;
		vtx->c.vtx = vtx;
		vtx->c.cursor.priv_tbl = &vslc_vtx_tbl;
		vtx->c.cursor.priv_data = &vtx->c;
	}

	CHECK_OBJ_NOTNULL(vtx, VTX_MAGIC);
	vtx->key.vxid = 0;
	vtx->t_start = VTIM_mono();
	vtx->flags = 0;
	vtx->type = VSL_t_unknown;
	vtx->reason = VSL_r_unknown;
	vtx->parent = NULL;
	vtx->n_child = 0;
	vtx->n_childready = 0;
	vtx->n_descend = 0;
	vtx->len = 0;
	AN(vslc_vtx_reset(&vtx->c.cursor) == vsl_end);

	return (vtx);
}

/* Disuse a vtx and all it's children, freeing any resources held. Free or
   cache the vtx for later use */
static void
vtx_retire(struct VSLQ *vslq, struct vtx **pvtx)
{
	struct vtx *vtx;
	struct vtx *child;
	struct synth *synth;
	struct chunk *chunk;

	AN(vslq);
	TAKE_OBJ_NOTNULL(vtx, pvtx, VTX_MAGIC);

	AN(vtx->flags & VTX_F_COMPLETE);
	AN(vtx->flags & VTX_F_READY);
	AZ(vtx->parent);

	while (!VTAILQ_EMPTY(&vtx->child)) {
		child = VTAILQ_FIRST(&vtx->child);
		assert(child->parent == vtx);
		AN(vtx->n_child);
		assert(vtx->n_descend >= child->n_descend + 1);
		VTAILQ_REMOVE(&vtx->child, child, list_child);
		child->parent = NULL;
		vtx->n_child--;
		vtx->n_descend -= child->n_descend + 1;
		vtx_retire(vslq, &child);
		AZ(child);
	}
	AZ(vtx->n_child);
	AZ(vtx->n_descend);
	vtx->n_childready = 0;
	// remove rval is no way to check if element was present
	(void)VRBT_REMOVE(vtx_tree, &vslq->tree, &vtx->key);
	vtx->key.vxid = 0;
	vtx->flags = 0;

	while (!VTAILQ_EMPTY(&vtx->synth)) {
		synth = VTAILQ_FIRST(&vtx->synth);
		CHECK_OBJ_NOTNULL(synth, SYNTH_MAGIC);
		VTAILQ_REMOVE(&vtx->synth, synth, list);
		FREE_OBJ(synth);
	}

	while (!VTAILQ_EMPTY(&vtx->chunks)) {
		chunk = VTAILQ_FIRST(&vtx->chunks);
		CHECK_OBJ_NOTNULL(chunk, CHUNK_MAGIC);
		VTAILQ_REMOVE(&vtx->chunks, chunk, list);
		if (chunk->type == chunk_t_shm) {
			VTAILQ_REMOVE(&vslq->shmrefs, chunk, shm.shmref);
			VTAILQ_INSERT_HEAD(&vtx->shmchunks_free, chunk, list);
		} else {
			assert(chunk->type == chunk_t_buf);
			chunk_freebuf(&chunk);
			AZ(chunk);
		}
	}
	vtx->len = 0;
	AN(vslq->n_outstanding);
	vslq->n_outstanding--;

	if (vslq->n_cache < VTX_CACHE) {
		VTAILQ_INSERT_HEAD(&vslq->cache, vtx, list_child);
		vslq->n_cache++;
	} else
		FREE_OBJ(vtx);

}

/* Lookup a vtx by vxid from the managed list */
static struct vtx *
vtx_lookup(const struct VSLQ *vslq, uint64_t vxid)
{
	struct vtx_key lkey, *key;
	struct vtx *vtx;

	AN(vslq);
	lkey.vxid = vxid;
	key = VRBT_FIND(vtx_tree, &vslq->tree, &lkey);
	if (key == NULL)
		return (NULL);
	CAST_OBJ_NOTNULL(vtx, (void *)key, VTX_MAGIC);
	return (vtx);
}

/* Insert a new vtx into the managed list */
static struct vtx *
vtx_add(struct VSLQ *vslq, uint64_t vxid)
{
	struct vtx *vtx;

	AN(vslq);
	vtx = vtx_new(vslq);
	AN(vtx);
	vtx->key.vxid = vxid;
	AZ(VRBT_INSERT(vtx_tree, &vslq->tree, &vtx->key));
	VTAILQ_INSERT_TAIL(&vslq->incomplete, vtx, list_vtx);
	vslq->n_outstanding++;
	return (vtx);
}

/* Mark a vtx complete, update child counters and if possible push it or
   it's top parent to the ready state */
static void
vtx_mark_complete(struct VSLQ *vslq, struct vtx *vtx)
{

	AN(vslq);
	AN(vtx->flags & VTX_F_END);
	AZ(vtx->flags & VTX_F_COMPLETE);

	if (vtx->type == VSL_t_unknown)
		vtx_diag(vtx, "vtx of unknown type marked complete");

	vtx->flags |= VTX_F_COMPLETE;
	VTAILQ_REMOVE(&vslq->incomplete, vtx, list_vtx);

	while (1) {
		AZ(vtx->flags & VTX_F_READY);
		if (vtx->flags & VTX_F_COMPLETE &&
		    vtx->n_child == vtx->n_childready)
			vtx->flags |= VTX_F_READY;
		else
			return;
		if (vtx->parent == NULL) {
			/* Top level vtx ready */
			VTAILQ_INSERT_TAIL(&vslq->ready, vtx, list_vtx);
			return;
		}
		vtx = vtx->parent;
		vtx->n_childready++;
		assert(vtx->n_child >= vtx->n_childready);
	}
}

/* Add a child to a parent, and update child counters */
static void
vtx_set_parent(struct vtx *parent, struct vtx *child)
{

	CHECK_OBJ_NOTNULL(parent, VTX_MAGIC);
	CHECK_OBJ_NOTNULL(child, VTX_MAGIC);
	assert(parent != child);
	AZ(parent->flags & VTX_F_COMPLETE);
	AZ(child->flags & VTX_F_COMPLETE);
	AZ(child->parent);
	child->parent = parent;
	VTAILQ_INSERT_TAIL(&parent->child, child, list_child);
	parent->n_child++;
	do
		parent->n_descend += 1 + child->n_descend;
	while ((parent = parent->parent) != NULL);
}

/* Parse a begin or link record. Returns the number of elements that was
   successfully parsed. */
static int
vtx_parse_link(const char *str, enum VSL_transaction_e *ptype,
    uint64_t *pvxid, enum VSL_reason_e *preason, uint64_t *psub)
{
	char type[16], reason[16];
	uintmax_t vxid, sub;
	int i;
	enum VSL_transaction_e et;
	enum VSL_reason_e er;

	AN(str);
	AN(ptype);
	AN(pvxid);
	AN(preason);

	i = sscanf(str, "%15s %ju %15s %ju", type, &vxid, reason, &sub);
	if (i < 1)
		return (0);

	/* transaction type */
	for (et = VSL_t_unknown; et < VSL_t__MAX; et++)
		if (!strcmp(type, vsl_t_names[et]))
			break;
	if (et >= VSL_t__MAX)
		et = VSL_t_unknown;
	*ptype = et;
	if (i == 1)
		return (1);

	/* vxid */
	assert((vxid & ~VSL_IDENTMASK) == 0);
	*pvxid = vxid;
	if (i == 2)
		return (2);

	/* transaction reason */
	for (er = VSL_r_unknown; er < VSL_r__MAX; er++)
		if (!strcmp(reason, vsl_r_names[er]))
			break;
	if (er >= VSL_r__MAX)
		er = VSL_r_unknown;
	*preason = er;
	if (i == 3)
		return (3);

	/* request sub-level */
	if (psub != NULL)
		*psub = sub;
	return (4);
}

/* Parse and process a begin record */
static int
vtx_scan_begin(struct VSLQ *vslq, struct vtx *vtx, const uint32_t *ptr)
{
	int i;
	enum VSL_transaction_e type;
	enum VSL_reason_e reason;
	uint64_t p_vxid;
	struct vtx *p_vtx;

	assert(VSL_TAG(ptr) == SLT_Begin);

	AZ(vtx->flags & VTX_F_READY);

	i = vtx_parse_link(VSL_CDATA(ptr), &type, &p_vxid, &reason, NULL);
	if (i < 3)
		return (vtx_diag_tag(vtx, ptr, "parse error"));
	if (type == VSL_t_unknown)
		(void)vtx_diag_tag(vtx, ptr, "unknown vxid type");

	/* Check/set vtx type */
	if (vtx->type != VSL_t_unknown && vtx->type != type)
		/* Type not matching the one previously set by a link
		   record */
		(void)vtx_diag_tag(vtx, ptr, "type mismatch");
	vtx->type = type;
	vtx->reason = reason;

	if (p_vxid == 0)
		/* Zero means no parent */
		return (0);
	if (p_vxid == vtx->key.vxid)
		return (vtx_diag_tag(vtx, ptr, "link to self"));

	if (vslq->grouping == VSL_g_vxid)
		return (0);	/* No links */
	if (vslq->grouping == VSL_g_request && vtx->type == VSL_t_req &&
	    vtx->reason == VSL_r_rxreq)
		return (0);	/* No links */

	if (vtx->parent != NULL) {
		if (vtx->parent->key.vxid != p_vxid) {
			/* This vtx already belongs to a different
			   parent */
			return (vtx_diag_tag(vtx, ptr, "link mismatch"));
		} else
			/* Link already exists */
			return (0);
	}

	p_vtx = vtx_lookup(vslq, p_vxid);
	if (p_vtx == NULL) {
		/* Not seen parent yet. Create it. */
		p_vtx = vtx_add(vslq, p_vxid);
		AN(p_vtx);
	} else {
		CHECK_OBJ_NOTNULL(p_vtx, VTX_MAGIC);
		if (p_vtx->flags & VTX_F_COMPLETE)
			return (vtx_diag_tag(vtx, ptr, "link too late"));
	}

	/* Create link */
	vtx_set_parent(p_vtx, vtx);

	return (0);
}

/* Parse and process a link record */
static int
vtx_scan_link(struct VSLQ *vslq, struct vtx *vtx, const uint32_t *ptr)
{
	int i;
	enum VSL_transaction_e c_type;
	enum VSL_reason_e c_reason;
	uint64_t c_vxid;
	struct vtx *c_vtx;

	assert(VSL_TAG(ptr) == SLT_Link);

	AZ(vtx->flags & VTX_F_READY);

	i = vtx_parse_link(VSL_CDATA(ptr), &c_type, &c_vxid, &c_reason, NULL);
	if (i < 3)
		return (vtx_diag_tag(vtx, ptr, "parse error"));
	if (c_type == VSL_t_unknown)
		(void)vtx_diag_tag(vtx, ptr, "unknown vxid type");

	if (vslq->grouping == VSL_g_vxid)
		return (0);	/* No links */
	if (vslq->grouping == VSL_g_request && vtx->type == VSL_t_sess)
		return (0);	/* No links */

	if (c_vxid == 0)
		return (vtx_diag_tag(vtx, ptr, "illegal link vxid"));
	if (c_vxid == vtx->key.vxid)
		return (vtx_diag_tag(vtx, ptr, "link to self"));

	/* Lookup and check child vtx */
	c_vtx = vtx_lookup(vslq, c_vxid);
	if (c_vtx == NULL) {
		/* Child not seen before. Insert it and create link */
		c_vtx = vtx_add(vslq, c_vxid);
		AN(c_vtx);
		AZ(c_vtx->parent);
		c_vtx->type = c_type;
		c_vtx->reason = c_reason;
		vtx_set_parent(vtx, c_vtx);
		return (0);
	}

	CHECK_OBJ_NOTNULL(c_vtx, VTX_MAGIC);
	if (c_vtx->parent == vtx)
		/* Link already exists */
		return (0);
	if (c_vtx->parent != NULL && c_vtx->parent != vtx)
		return (vtx_diag_tag(vtx, ptr, "duplicate link"));
	if (c_vtx->flags & VTX_F_COMPLETE)
		return (vtx_diag_tag(vtx, ptr, "link too late"));
	if (c_vtx->type != VSL_t_unknown && c_vtx->type != c_type)
		(void)vtx_diag_tag(vtx, ptr, "type mismatch");

	c_vtx->type = c_type;
	c_vtx->reason = c_reason;
	vtx_set_parent(vtx, c_vtx);
	return (0);
}

/* Scan the records of a vtx, performing processing actions on specific
   records */
static void
vtx_scan(struct VSLQ *vslq, struct vtx *vtx)
{
	const uint32_t *ptr;
	enum VSL_tag_e tag;

	while (!(vtx->flags & VTX_F_COMPLETE) &&
	    vslc_vtx_next(&vtx->c.cursor) == 1) {
		ptr = vtx->c.cursor.rec.ptr;
		if (VSL_ID(ptr) != vtx->key.vxid) {
			(void)vtx_diag_tag(vtx, ptr, "vxid mismatch");
			continue;
		}

		tag = VSL_TAG(ptr);
		assert(tag != SLT__Batch);

		switch (tag) {
		case SLT_Begin:
			if (vtx->flags & VTX_F_BEGIN)
				(void)vtx_diag_tag(vtx, ptr, "duplicate begin");
			else {
				(void)vtx_scan_begin(vslq, vtx, ptr);
				vtx->flags |= VTX_F_BEGIN;
			}
			break;

		case SLT_Link:
			(void)vtx_scan_link(vslq, vtx, ptr);
			break;

		case SLT_End:
			AZ(vtx->flags & VTX_F_END);
			vtx->flags |= VTX_F_END;
			vtx_mark_complete(vslq, vtx);
			break;

		default:
			break;
		}
	}
}

/* Force a vtx into complete status by synthing the necessary outstanding
   records */
static void
vtx_force(struct VSLQ *vslq, struct vtx *vtx, const char *reason)
{

	AZ(vtx->flags & VTX_F_COMPLETE);
	AZ(vtx->flags & VTX_F_READY);
	vtx_scan(vslq, vtx);
	if (!(vtx->flags & VTX_F_BEGIN))
		vtx_synth_rec(vtx, SLT_Begin, "%s %u synth",
		    vsl_t_names[vtx->type], 0);
	vtx_diag(vtx, reason);
	if (!(vtx->flags & VTX_F_END))
		vtx_synth_rec(vtx, SLT_End, "synth");
	vtx_scan(vslq, vtx);
	AN(vtx->flags & VTX_F_COMPLETE);
}

static int
vslq_ratelimit(struct VSLQ *vslq)
{
	vtim_mono now;
	vtim_dur delta;

	CHECK_OBJ_NOTNULL(vslq, VSLQ_MAGIC);
	CHECK_OBJ_NOTNULL(vslq->vsl, VSL_MAGIC);

	now = VTIM_mono();
	delta = now - vslq->last_use;
	vslq->credits += (delta / vslq->vsl->R_opt_p) * vslq->vsl->R_opt_l;
	vslq->credits = vmin_t(double, vslq->credits, vslq->vsl->R_opt_l);
	vslq->last_use = now;

	if (vslq->credits < 1.0)
		return (0);

	vslq->credits -= 1.0;
	return (1);
}

/* Build transaction array, do the query and callback. Returns 0 or the
   return value from func */
static int
vslq_callback(struct VSLQ *vslq, struct vtx *vtx, VSLQ_dispatch_f *func,
    void *priv)
{
	unsigned n = vtx->n_descend + 1;
	v_vla_(struct vtx, *vtxs, n);
	v_vla_(struct VSL_transaction, trans, n);
	v_vla_(struct VSL_transaction, *ptrans, n + 1);
	unsigned i, j;

	AN(vslq);
	CHECK_OBJ_NOTNULL(vtx, VTX_MAGIC);
	AN(vtx->flags & VTX_F_READY);
	AN(func);

	if (vslq->grouping == VSL_g_session &&
	    vtx->type != VSL_t_sess)
		return (0);
	if (vslq->grouping == VSL_g_request &&
	    vtx->type != VSL_t_req)
		return (0);

	/* Build transaction array */
	AN(vslc_vtx_reset(&vtx->c.cursor) == vsl_end);
	vtxs[0] = vtx;
	trans[0].level = 1;
	trans[0].vxid = vtx->key.vxid;
	trans[0].vxid_parent = 0;
	trans[0].type = vtx->type;
	trans[0].reason = vtx->reason;
	trans[0].c = &vtx->c.cursor;
	i = 1;
	j = 0;
	while (j < i) {
		VTAILQ_FOREACH(vtx, &vtxs[j]->child, list_child) {
			assert(i < n);
			AN(vslc_vtx_reset(&vtx->c.cursor) == vsl_end);
			vtxs[i] = vtx;
			if (vtx->reason == VSL_r_restart)
				/* Restarts stay at the same level as parent */
				trans[i].level = trans[j].level;
			else
				trans[i].level = trans[j].level + 1;
			trans[i].vxid = vtx->key.vxid;
			trans[i].vxid_parent = trans[j].vxid;
			trans[i].type = vtx->type;
			trans[i].reason = vtx->reason;
			trans[i].c = &vtx->c.cursor;
			i++;
		}
		j++;
	}
	assert(i == n);

	/* Build pointer array */
	for (i = 0; i < n; i++)
		ptrans[i] = &trans[i];
	ptrans[i] = NULL;

	/* Query test goes here */
	if (vslq->query != NULL && !vslq_runquery(vslq->query, ptrans))
		return (0);

	if (vslq->vsl->R_opt_l != 0 && !vslq_ratelimit(vslq))
		return (0);

	/* Callback */
	return ((func)(vslq->vsl, ptrans, priv));
}

/* Create a synthetic log record. The record will be inserted at the
   current cursor offset */
static void
vtx_synth_rec(struct vtx *vtx, unsigned tag, const char *fmt, ...)
{
	struct synth *synth, *it;
	va_list ap;
	char *buf;
	int l, buflen;
	uint64_t vxid;

	ALLOC_OBJ(synth, SYNTH_MAGIC);
	AN(synth);

	buf = VSL_DATA(synth->data);
	buflen = sizeof(synth->data) - VSL_BYTES(VSL_OVERHEAD);
	va_start(ap, fmt);
	l = vsnprintf(buf, buflen, fmt, ap);
	assert(l >= 0);
	va_end(ap);
	if (l > buflen - 1)
		l = buflen - 1;
	buf[l++] = '\0';	/* NUL-terminated */
	vxid = vtx->key.vxid;
	switch (vtx->type) {
	case VSL_t_req:
		vxid |= VSL_CLIENTMARKER;
		break;
	case VSL_t_bereq:
		vxid |= VSL_BACKENDMARKER;
		break;
	default:
		break;
	}
	synth->data[2] = vxid >> 32;
	synth->data[1] = vxid;
	synth->data[0] = (((tag & VSL_IDMASK) << VSL_IDSHIFT) |
	    (VSL_VERSION_3 << VSL_VERSHIFT) | l);
	synth->offset = vtx->c.offset;

	VTAILQ_FOREACH_REVERSE(it, &vtx->synth, synthhead, list) {
		/* Make sure the synth list is sorted on offset */
		CHECK_OBJ_NOTNULL(it, SYNTH_MAGIC);
		if (synth->offset >= it->offset)
			break;
	}
	if (it != NULL)
		VTAILQ_INSERT_AFTER(&vtx->synth, it, synth, list);
	else
		VTAILQ_INSERT_HEAD(&vtx->synth, synth, list);

	/* Update cursor */
	CHECK_OBJ_ORNULL(vtx->c.synth, SYNTH_MAGIC);
	if (vtx->c.synth == NULL || vtx->c.synth->offset > synth->offset)
		vtx->c.synth = synth;
}

/* Add a diagnostic SLT_VSL synth record to the vtx. */
static int
vtx_diag(struct vtx *vtx, const char *msg)
{

	vtx_synth_rec(vtx, SLT_VSL, msg);
	return (-1);
}

/* Add a SLT_VSL diag synth record to the vtx. Takes an offending record
   that will be included in the log record */
static int
vtx_diag_tag(struct vtx *vtx, const uint32_t *ptr, const char *reason)
{

	vtx_synth_rec(vtx, SLT_VSL, "%s (%ju:%s \"%.*s\")", reason, VSL_ID(ptr),
	    VSL_tags[VSL_TAG(ptr)], (int)VSL_LEN(ptr), VSL_CDATA(ptr));
	return (-1);
}

struct VSLQ *
VSLQ_New(struct VSL_data *vsl, struct VSL_cursor **cp,
    enum VSL_grouping_e grouping, const char *querystring)
{
	struct vslq_query *query;
	struct VSLQ *vslq;

	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);
	if (grouping >= VSL_g__MAX) {
		(void)vsl_diag(vsl, "Illegal query grouping");
		return (NULL);
	}
	if (querystring != NULL) {
		query = vslq_newquery(vsl, grouping, querystring);
		if (query == NULL)
			return (NULL);
	} else
		query = NULL;

	ALLOC_OBJ(vslq, VSLQ_MAGIC);
	AN(vslq);
	vslq->vsl = vsl;
	if (cp != NULL) {
		vslq->c = *cp;
		*cp = NULL;
	}
	vslq->grouping = grouping;
	vslq->query = query;
	if (vslq->vsl->R_opt_l != 0) {
		vslq->last_use = VTIM_mono();
		vslq->credits = 1;
	}

	/* Setup normal mode */
	VRBT_INIT(&vslq->tree);
	VTAILQ_INIT(&vslq->ready);
	VTAILQ_INIT(&vslq->incomplete);
	VTAILQ_INIT(&vslq->shmrefs);
	VTAILQ_INIT(&vslq->cache);

	/* Setup raw mode */
	vslq->raw.c.magic = VSLC_RAW_MAGIC;
	vslq->raw.c.cursor.priv_tbl = &vslc_raw_tbl;
	vslq->raw.c.cursor.priv_data = &vslq->raw.c;
	vslq->raw.trans.level = 0;
	vslq->raw.trans.type = VSL_t_raw;
	vslq->raw.trans.reason = VSL_r_unknown;
	vslq->raw.trans.c = &vslq->raw.c.cursor;
	vslq->raw.ptrans[0] = &vslq->raw.trans;
	vslq->raw.ptrans[1] = NULL;

	return (vslq);
}

void
VSLQ_Delete(struct VSLQ **pvslq)
{
	struct VSLQ *vslq;
	struct vtx *vtx;

	TAKE_OBJ_NOTNULL(vslq, pvslq, VSLQ_MAGIC);

	(void)VSLQ_Flush(vslq, NULL, NULL);
	AZ(vslq->n_outstanding);

	if (vslq->c != NULL) {
		VSL_DeleteCursor(vslq->c);
		vslq->c = NULL;
	}

	if (vslq->query != NULL)
		vslq_deletequery(&vslq->query);
	AZ(vslq->query);

	while (!VTAILQ_EMPTY(&vslq->cache)) {
		AN(vslq->n_cache);
		vtx = VTAILQ_FIRST(&vslq->cache);
		CHECK_OBJ_NOTNULL(vtx, VTX_MAGIC);
		VTAILQ_REMOVE(&vslq->cache, vtx, list_child);
		vslq->n_cache--;
		FREE_OBJ(vtx);
	}

	FREE_OBJ(vslq);
}

void
VSLQ_SetCursor(struct VSLQ *vslq, struct VSL_cursor **cp)
{

	CHECK_OBJ_NOTNULL(vslq, VSLQ_MAGIC);

	if (vslq->c != NULL) {
		(void)VSLQ_Flush(vslq, NULL, NULL);
		AZ(vslq->n_outstanding);
		VSL_DeleteCursor(vslq->c);
		vslq->c = NULL;
	}

	if (cp != NULL) {
		AN(*cp);
		vslq->c = *cp;
		*cp = NULL;
	}
}

/* Regard each log line as a single transaction, feed it through the query
   and do the callback */
static int
vslq_raw(struct VSLQ *vslq, VSLQ_dispatch_f *func, void *priv)
{
	enum vsl_status r = vsl_more;
	int i;

	assert(vslq->grouping == VSL_g_raw);

	assert(vslq->raw.offset <= vslq->raw.len);
	do {
		if (vslq->raw.offset == vslq->raw.len) {
			r = VSL_Next(vslq->c);
			if (r != vsl_more)
				return (r);
			AN(vslq->c->rec.ptr);
			vslq->raw.start = vslq->c->rec;
			if (VSL_TAG(vslq->c->rec.ptr) == SLT__Batch)
				vslq->raw.len = VSL_END(vslq->c->rec.ptr,
				    VSL_BATCHLEN(vslq->c->rec.ptr)) -
				    vslq->c->rec.ptr;
			else
				vslq->raw.len = VSL_NEXT(vslq->raw.start.ptr) -
				    vslq->raw.start.ptr;
			assert(vslq->raw.len > 0);
			vslq->raw.offset = 0;
		}

		vslq->raw.c.ptr = vslq->raw.start.ptr + vslq->raw.offset;
		vslq->raw.c.cursor.rec.ptr = NULL;
		vslq->raw.trans.vxid = VSL_ID(vslq->raw.c.ptr);
		vslq->raw.offset += VSL_NEXT(vslq->raw.c.ptr) - vslq->raw.c.ptr;
	} while (VSL_TAG(vslq->raw.c.ptr) == SLT__Batch);

	assert (r == vsl_more);

	if (func == NULL)
		return (r);

	if (vslq->query != NULL &&
	    !vslq_runquery(vslq->query, vslq->raw.ptrans))
		return (r);

	if (vslq->vsl->R_opt_l != 0 && !vslq_ratelimit(vslq))
		return (r);

	i = (func)(vslq->vsl, vslq->raw.ptrans, priv);
	if (i)
		return (i);

	return (r);
}

/* Check the beginning of the shmref list, and buffer refs that are at
 * warning level.
 */
static enum vsl_status
vslq_shmref_check(struct VSLQ *vslq)
{
	struct chunk *chunk;
	enum vsl_check i;

	while ((chunk = VTAILQ_FIRST(&vslq->shmrefs)) != NULL) {
		CHECK_OBJ_NOTNULL(chunk, CHUNK_MAGIC);
		assert(chunk->type == chunk_t_shm);
		i = VSL_Check(vslq->c, &chunk->shm.start);
		switch (i) {
		case vsl_check_valid:
			/* First on list is OK, refs behind it must also
			   be OK */
			return (vsl_more);
		case vsl_check_warn:
			/* Buffer this chunk */
			chunk_shm_to_buf(vslq, chunk);
			break;
		default:
			/* Too late to buffer */
			return (vsl_e_overrun);
		}
	}

	return (vsl_more);
}

static unsigned
vslq_candidate(struct VSLQ *vslq, const uint32_t *ptr)
{
	enum VSL_transaction_e type;
	enum VSL_reason_e reason;
	struct VSL_data *vsl;
	enum VSL_tag_e tag;
	uint64_t p_vxid, sub;
	int i;

	CHECK_OBJ_NOTNULL(vslq, VSLQ_MAGIC);
	AN(ptr);

	assert(vslq->grouping != VSL_g_raw);
	if (vslq->grouping == VSL_g_session)
		return (1); /* All are needed */

	vsl = vslq->vsl;
	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);
	if (vslq->grouping == VSL_g_vxid) {
		if (!vsl->c_opt && !vsl->b_opt)
			AZ(vsl->E_opt);
		else if (!vsl->b_opt && !VSL_CLIENT(ptr))
			return (0);
		else if (!vsl->c_opt && !VSL_BACKEND(ptr))
			return (0);
		/* Need to parse the Begin tag - fallthrough to below */
	}

	tag = VSL_TAG(ptr);
	assert(tag == SLT_Begin);
	i = vtx_parse_link(VSL_CDATA(ptr), &type, &p_vxid, &reason, &sub);
	if (i < 3 || type == VSL_t_unknown)
		return (0);

	if (vslq->grouping == VSL_g_request && type == VSL_t_sess)
		return (0);

	if (vslq->grouping == VSL_g_vxid && i > 3 && sub > 0 && !vsl->E_opt)
		return (0);

	return (1);
}

/* Process next input record */
static enum vsl_status
vslq_next(struct VSLQ *vslq)
{
	const uint32_t *ptr;
	struct VSL_cursor *c;
	enum vsl_status r;
	enum VSL_tag_e tag;
	ssize_t len;
	uint64_t vxid;
	unsigned keep;
	struct vtx *vtx;

	c = vslq->c;
	r = VSL_Next(c);
	if (r != vsl_more)
		return (r);

	assert (r == vsl_more);

	tag = (enum VSL_tag_e)VSL_TAG(c->rec.ptr);
	if (tag == SLT__Batch) {
		vxid = VSL_BATCHID(c->rec.ptr);
		len = VSL_END(c->rec.ptr, VSL_BATCHLEN(c->rec.ptr)) -
		    c->rec.ptr;
		if (len == 0)
			return (r);
		ptr = VSL_NEXT(c->rec.ptr);
		tag = (enum VSL_tag_e)VSL_TAG(ptr);
	} else {
		vxid = VSL_ID(c->rec.ptr);
		len = VSL_NEXT(c->rec.ptr) - c->rec.ptr;
		ptr = c->rec.ptr;
	}
	assert(len > 0);
	if (vxid == 0)
		/* Skip non-transactional records */
		return (r);

	vtx = vtx_lookup(vslq, vxid);
	keep = tag != SLT_Begin || vslq_candidate(vslq, ptr);
	if (vtx == NULL && tag == SLT_Begin && keep) {
		vtx = vtx_add(vslq, vxid);
		AN(vtx);
	}
	if (vtx != NULL) {
		AN(keep);
		r = vtx_append(vslq, vtx, &c->rec, len);
		if (r == vsl_more)
			vtx_scan(vslq, vtx);
	}

	return (r);
}

/* Test query and report any ready transactions */
static int
vslq_process_ready(struct VSLQ *vslq, VSLQ_dispatch_f *func, void *priv)
{
	struct vtx *vtx;
	int i = 0;

	AN(vslq);

	while (!VTAILQ_EMPTY(&vslq->ready)) {
		vtx = VTAILQ_FIRST(&vslq->ready);
		CHECK_OBJ_NOTNULL(vtx, VTX_MAGIC);
		VTAILQ_REMOVE(&vslq->ready, vtx, list_vtx);
		AN(vtx->flags & VTX_F_READY);
		if (func != NULL)
			i = vslq_callback(vslq, vtx, func, priv);
		vtx_retire(vslq, &vtx);
		AZ(vtx);
		if (i)
			return (i);
	}

	return (0);
}

/* Process the input cursor, calling the callback function on matching
   transaction sets */
int
VSLQ_Dispatch(struct VSLQ *vslq, VSLQ_dispatch_f *func, void *priv)
{
	enum vsl_status r;
	int i;
	double now;
	struct vtx *vtx;

	CHECK_OBJ_NOTNULL(vslq, VSLQ_MAGIC);

	/* Check that we have a cursor */
	if (vslq->c == NULL)
		return (vsl_e_abandon);

	if (vslq->grouping == VSL_g_raw)
		return (vslq_raw(vslq, func, priv));

	/* Process next cursor input */
	r = vslq_next(vslq);
	if (r != vsl_more)
		/* At end of log or cursor reports error condition */
		return (r);

	/* Check shmref list and buffer if necessary */
	r = vslq_shmref_check(vslq);
	if (r != vsl_more)
		/* Buffering of shm ref failed */
		return (r);

	assert (r == vsl_more);

	/* Check vtx timeout */
	now = VTIM_mono();
	while (!VTAILQ_EMPTY(&vslq->incomplete)) {
		vtx = VTAILQ_FIRST(&vslq->incomplete);
		CHECK_OBJ_NOTNULL(vtx, VTX_MAGIC);
		if (now - vtx->t_start < vslq->vsl->T_opt)
			break;
		vtx_force(vslq, vtx, "timeout");
		AN(vtx->flags & VTX_F_COMPLETE);
	}

	/* Check store limit */
	while (vslq->n_outstanding > vslq->vsl->L_opt &&
	    !(VTAILQ_EMPTY(&vslq->incomplete))) {
		vtx = VTAILQ_FIRST(&vslq->incomplete);
		CHECK_OBJ_NOTNULL(vtx, VTX_MAGIC);
		vtx_force(vslq, vtx, "store overflow");
		AN(vtx->flags & VTX_F_COMPLETE);
		i = vslq_process_ready(vslq, func, priv);
		if (i)
			/* User return code */
			return (i);
	}

	/* Check ready list */
	if (!VTAILQ_EMPTY(&vslq->ready)) {
		i = vslq_process_ready(vslq, func, priv);
		if (i)
			/* User return code */
			return (i);
	}

	return (vsl_more);
}

/* Flush any incomplete vtx held on to. Do callbacks if func != NULL */
int
VSLQ_Flush(struct VSLQ *vslq, VSLQ_dispatch_f *func, void *priv)
{
	struct vtx *vtx;

	CHECK_OBJ_NOTNULL(vslq, VSLQ_MAGIC);

	while (!VTAILQ_EMPTY(&vslq->incomplete)) {
		vtx = VTAILQ_FIRST(&vslq->incomplete);
		CHECK_OBJ_NOTNULL(vtx, VTX_MAGIC);
		AZ(vtx->flags & VTX_F_COMPLETE);
		vtx_force(vslq, vtx, "flush");
	}

	return (vslq_process_ready(vslq, func, priv));
}
