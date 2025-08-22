/*-
 * Copyright 2025 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Author: Nils Goroll <slink@uplex.de>
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
 * Special-purpose stevedore for use with transient synth objects as those
 * created from vcl_synth {}: Does not copy, only references on-workspace
 * data
 */

#include "config.h"

#include <stdlib.h>

#include "cache/cache_varnishd.h"

#include "cache/cache_obj.h"
#include "cache/cache_objhead.h"

#include "storage/storage.h"
#include "storage/storage_simple.h"

#include "vend.h"

struct ssy_st {
	unsigned			magic;
#define	SSY_ST_MAGIC			0x236918b3
	VSTAILQ_ENTRY(ssy_st)		list;
	struct vscarab			scarab;
};

//lint -emacro({413}, SSY_ST_SIZE)
#define SSY_ST_SIZE(cap) (offsetof(struct ssy_st, scarab) + VSCARAB_SIZE(cap))

struct ssy {
	unsigned			magic;
#define SSY_MAGIC			0xfb41f362
	unsigned			flags;
#define SSY_F_LEN			1
	struct ws			*ws;
	VSTAILQ_HEAD(,ssy_st)		head;
	// getattr needs to return *pointer* to len as big endian
	uint8_t				len_be[sizeof(uint64_t)];
};

static void v_matchproto_(objfree_f)
ssy_objfree(struct worker *wrk, struct objcore *oc)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	// object is on workspace, nothing to free here
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	memset(oc->stobj, 0, sizeof *oc->stobj);
	wrk->stats->n_object--;
}

static struct ssy_st *
ssy_st_alloc(struct ssy *ssy)
{
	struct ssy_st *st;
	const size_t cap = 8;			// XXX GOOD?
	const size_t sz = SSY_ST_SIZE(cap);

	CHECK_OBJ_NOTNULL(ssy, SSY_MAGIC);
	st = WS_Alloc(ssy->ws, sz);
	if (st == NULL)
		return (st);
	INIT_OBJ(st, SSY_ST_MAGIC);
	VSCARAB_INIT(&st->scarab, cap);
	VSTAILQ_INSERT_TAIL(&ssy->head, st, list);
	return (st);
}

/*
 * deliberate API violation: does not return space, but a VSCARAB
 *
 * XXX own stevedore method?
 */
static int v_matchproto_(objgetspace_f)
ssy_getspace(struct worker *wrk, struct objcore *oc, ssize_t *sz,
    uint8_t **ptr)
{
	struct ssy *ssy;
	struct ssy_st *st;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CAST_OBJ_NOTNULL(ssy, oc->stobj->priv, SSY_MAGIC);
	(void) sz;

	st = VSTAILQ_LAST(&ssy->head, ssy_st, list);
	CHECK_OBJ_NOTNULL(st, SSY_ST_MAGIC);
	CHECK_OBJ_NOTNULL(&st->scarab, VSCARAB_MAGIC);

	if (st->scarab.used == st->scarab.capacity) {
		st = ssy_st_alloc(ssy);
		if (st == NULL)
			return (0);
	}
	*ptr = (void*)(&st->scarab);
	return (1);
}

/*
 * VAI for ssy is really simple because we never block, hence need no
 * notification
 */

struct ssy_hdl {
	struct vai_hdl_preamble	preamble;
#define SSY_HDL_MAGIC		0x07a0be62
	//struct ssy		*ssy;
	struct ssy_st		*st;
	struct viov		*viov;
};

static int
ssy_ai_lease(struct worker *wrk, vai_hdl vhdl, struct vscarab *dst)
{
	struct ssy_hdl *hdl;
	struct viov *dviov;
	int r = 0;

	(void) wrk;
	CAST_VAI_HDL_NOTNULL(hdl, vhdl, SSY_HDL_MAGIC);
	VSCARAB_CHECK_NOTNULL(dst);

	if (hdl->st == NULL) {
		dst->flags |= VSCARAB_F_END;
		return (r);
	}

	CHECK_OBJ(hdl->st, SSY_ST_MAGIC);
	VSCARAB_CHECK(&hdl->st->scarab);
	AN(hdl->viov);

	do {
		VSCARAB_FOREACH_RESUME(hdl->viov, &hdl->st->scarab) {
			dviov = VSCARAB_GET(dst);
			if (dviov == NULL)
				break;
			*dviov = *hdl->viov;
			dviov->lease = VAI_LEASE_NORET;
			r++;
		}
		if (hdl->viov == NULL) {
			hdl->st = VSTAILQ_NEXT(hdl->st, list);
			if (hdl->st == NULL)
				dst->flags |= VSCARAB_F_END;
			else
				hdl->viov = &hdl->st->scarab.s[0];
		}
	} while (hdl->viov != NULL);

	return (r);
}

// just malloc
static int
ssy_ai_buffer(struct worker *wrk, vai_hdl vhdl, struct vscarab *scarab)
{
	struct ssy_hdl *hdl;
	struct viov *vio;
	void *p;
	int r = 0;

	(void) wrk;
	CAST_VAI_HDL_NOTNULL(hdl, vhdl, SSY_HDL_MAGIC);

	VSCARAB_FOREACH(vio, scarab)
		if (vio->iov.iov_len > UINT_MAX)
			return (-EINVAL);

	VSCARAB_FOREACH(vio, scarab) {
		p = malloc(vio->iov.iov_len);
		AN(p);
		vio->iov.iov_base = p;
		vio->lease = ptr2lease(p);
		r++;
	}
	return (r);
}

// just free buffers
static void v_matchproto_(vai_return_f)
ssy_ai_return(struct worker *wrk, vai_hdl vhdl, struct vscaret *scaret)
{
	struct ssy_hdl *hdl;
	uint64_t *p;

	(void) wrk;
	CAST_VAI_HDL_NOTNULL(hdl, vhdl, SSY_HDL_MAGIC);
	VSCARET_CHECK_NOTNULL(scaret);
	VSCARET_FOREACH(p, scaret) {
		 if (*p != VAI_LEASE_NORET)
			free (lease2ptr(*p));
	}
}

static void v_matchproto_(vai_fini_f)
ssy_ai_fini(struct worker *wrk, vai_hdl *vai_hdlp)
{
	struct ssy_hdl *hdl;

	(void)wrk;
	AN(vai_hdlp);
	CAST_VAI_HDL_NOTNULL(hdl, *vai_hdlp, SSY_HDL_MAGIC);
	*vai_hdlp = NULL;
}

static void * v_matchproto_(objsetattr_f)
ssy_setattr(struct worker *wrk, struct objcore *oc, enum obj_attr attr,
    ssize_t len, const void *ptr)
{
	static uint64_t ignored;

	(void)wrk;
	(void)oc;
	assert(attr == OA_LEN);
	(void)len;
	(void)ptr;
	return (&ignored);
}

static const void * v_matchproto_(objgetattr_f)
ssy_getattr(struct worker *wrk, struct objcore *oc, enum obj_attr attr,
   ssize_t *len)
{
	struct ssy *ssy;
	struct ssy_st *st;
	struct viov *viov;
	static const uint8_t flags = 0;
	uint64_t l = 0;

	(void)wrk;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	if (attr == OA_FLAGS) {
		AZ(len);
		return (&flags);
	}

	if (attr != OA_LEN)
		return (NULL);

	AN(len);
	CAST_OBJ_NOTNULL(ssy, oc->stobj->priv, SSY_MAGIC);
	assert(attr == OA_LEN);

	if ((ssy->flags & SSY_F_LEN) == 0) {
		VSTAILQ_FOREACH(st, &ssy->head, list) {
			VSCARAB_FOREACH(viov, &st->scarab)
				l += viov->iov.iov_len;
		}
		vbe64enc(ssy->len_be, l);
		ssy->flags |= SSY_F_LEN;
	}
	*len = sizeof ssy->len_be;
	return (ssy->len_be);
}

static vai_hdl v_matchproto_(vai_init_f)
ssy_ai_init(struct worker *wrk, struct objcore *oc, struct ws *ws,
    vai_notify_cb *notify, void *notify_priv)
{
	struct ssy_hdl *hdl;
	struct ssy *ssy;

	(void)wrk;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CAST_OBJ_NOTNULL(ssy, oc->stobj->priv, SSY_MAGIC);
	(void)notify;
	(void)notify_priv;

	if (ws == NULL)
		ws = ssy->ws;
	hdl = WS_Alloc(ws, sizeof *hdl);
	if (hdl == NULL)
		return (NULL);

	INIT_VAI_HDL(hdl, SSY_HDL_MAGIC);
	hdl->st = VSTAILQ_FIRST(&ssy->head);
	CHECK_OBJ(hdl->st, SSY_ST_MAGIC);
	VSCARAB_CHECK(&hdl->st->scarab);
	hdl->viov = &hdl->st->scarab.s[0];

	AN(hdl);
	hdl->preamble.vai_lease = ssy_ai_lease;
	hdl->preamble.vai_buffer = ssy_ai_buffer;
	hdl->preamble.vai_return = ssy_ai_return;
	hdl->preamble.vai_fini = ssy_ai_fini;
	return (hdl);
}

static const struct obj_methods ssy_methods = {
	.objfree	= ssy_objfree,
	.objiterator	= SML_iterator,
	.objgetspace	= ssy_getspace,
	.objextend	= NULL,		// don't call, asserts in cache_obj.c
	.objtrimstore	= NULL,
	.objbocdone	= NULL,
	.objslim	= NULL,
	.objgetattr	= ssy_getattr,	// only OA_LEN, OA_FLAGS, others not
	.objsetattr	= ssy_setattr,	// only OA_LEN, ignores value
	.objtouch	= NULL,
	.vai_init	= ssy_ai_init
};

static int v_matchproto_(storage_allocobj_f)
ssy_allocobj(struct worker *wrk, const struct stevedore *stv,
    struct objcore *oc, unsigned wsl)
{
	struct req *req;
	struct ssy *ssy;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AZ(wsl);

	// XXX API?
	req = THR_GetRequest();
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	ssy = WS_Alloc(req->ws, sizeof *ssy);
	if (ssy == NULL)
		return (0);
	INIT_OBJ(ssy, SSY_MAGIC);
	ssy->ws = req->ws;
	VSTAILQ_INIT(&ssy->head);
	if (ssy_st_alloc(ssy) == NULL)
		return (0);
	oc->stobj->stevedore = stv;
	oc->stobj->priv = ssy;
	return (1);
}

const struct stevedore ssy_stevedore = {
	.magic		=	STEVEDORE_MAGIC,
	.name		=	"synth",
	.vclname	=	"storage.synth",
	.ident		=	"synth",
	.allocobj	=	ssy_allocobj,
	.methods	=	&ssy_methods
};
