/*-
 * Copyright (c) 2007-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Dag-Erling Smørgav <des@des.no>
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
 * STEVEDORE: one who works at or is responsible for loading and
 * unloading ships in port.  Example: "on the wharves, stevedores were
 * unloading cargo from the far corners of the world." Origin: Spanish
 * estibador, from estibar to pack.  First Known Use: 1788
 */

#include "config.h"

#include "cache/cache_varnishd.h"

#include <stdio.h>
#include <stdlib.h>

#include "storage/storage.h"
#include "vrt_obj.h"

extern const char *mgt_stv_h2_rxbuf;
struct stevedore *stv_h2_rxbuf = NULL;

static pthread_mutex_t stv_mtx;

/*--------------------------------------------------------------------
 * XXX: trust pointer writes to be atomic
 */

const struct stevedore *
STV_next(void)
{
	static struct stevedore *stv;
	struct stevedore *r;

	PTOK(pthread_mutex_lock(&stv_mtx));
	if (!STV__iter(&stv))
		AN(STV__iter(&stv));
	if (stv == stv_transient) {
		stv = NULL;
		AN(STV__iter(&stv));
	}
	r = stv;
	PTOK(pthread_mutex_unlock(&stv_mtx));
	AN(r);
	return (r);
}

/*-------------------------------------------------------------------
 * Allocate storage for an object, based on the header information.
 * XXX: If we know (a hint of) the length, we could allocate space
 * XXX: for the body in the same allocation while we are at it.
 */

int
STV_NewObject(struct worker *wrk, struct objcore *oc,
    const struct stevedore *stv, unsigned wsl)
{
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);

	wrk->strangelove = cache_param->nuke_limit;
	AN(stv->allocobj);
	if (stv->allocobj(wrk, stv, oc, wsl) == 0) {
		VSLb(wrk->vsl, SLT_Error,
		    "Failed to create object from %s %s",
		    stv->name, stv->ident);
		return (0);
	}
	oc->oa_present = 0;
	wrk->stats->n_object++;
	VSLb(wrk->vsl, SLT_Storage, "%s %s",
	    oc->stobj->stevedore->name, oc->stobj->stevedore->ident);
	return (1);
}

/*-------------------------------------------------------------------*/

struct stv_buffer {
	unsigned		magic;
#define STV_BUFFER_MAGIC	0xf39cb6c2
	const struct stevedore	*stv;
	size_t			size;
	uintptr_t		priv;
};

struct stv_buffer *
STV_AllocBuf(struct worker *wrk, const struct stevedore *stv, size_t size)
{
	struct stv_buffer *stvbuf;
	uint8_t *buf;
	uintptr_t priv = 0;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);

	if (size == 0)
		return (NULL);
	if (stv->allocbuf == NULL)
		return (NULL);

	wrk->strangelove = cache_param->nuke_limit;
	buf = stv->allocbuf(wrk, stv, size + PRNDUP(sizeof *stvbuf), &priv);
	if (buf == NULL)
		return (NULL);

	assert(PAOK(buf));
	stvbuf = (void *)buf;
	INIT_OBJ(stvbuf, STV_BUFFER_MAGIC);
	stvbuf->stv = stv;
	stvbuf->priv = priv;
	stvbuf->size = size;

	return (stvbuf);
}

void
STV_FreeBuf(struct worker *wrk, struct stv_buffer **pstvbuf)
{
	struct stv_buffer *stvbuf;
	const struct stevedore *stv;
	uintptr_t priv;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	TAKE_OBJ_NOTNULL(stvbuf, pstvbuf, STV_BUFFER_MAGIC);
	CHECK_OBJ_NOTNULL(stvbuf->stv, STEVEDORE_MAGIC);

	stv = stvbuf->stv;
	priv = stvbuf->priv;
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
	ZERO_OBJ(stvbuf, sizeof *stvbuf);

	AN(stv->freebuf);
	stv->freebuf(wrk, stv, priv);
}

void *
STV_GetBufPtr(struct stv_buffer *stvbuf, size_t *psize)
{
	CHECK_OBJ_NOTNULL(stvbuf, STV_BUFFER_MAGIC);
	if (psize)
		*psize = stvbuf->size;
	return (&stvbuf[1]);
}

/*-------------------------------------------------------------------*/

void
STV_open(void)
{
	struct stevedore *stv;
	char buf[1024];

	ASSERT_CLI();
	PTOK(pthread_mutex_init(&stv_mtx, &mtxattr_errorcheck));

	/* This string was prepared for us before the fork, and should
	 * point to a configured stevedore. */
	AN(mgt_stv_h2_rxbuf);

	stv_h2_rxbuf = NULL;
	STV_Foreach(stv) {
		CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
		bprintf(buf, "storage.%s", stv->ident);
		stv->vclname = strdup(buf);
		AN(stv->vclname);
		if (stv->open != NULL)
			stv->open(stv);
		if (!strcmp(stv->ident, mgt_stv_h2_rxbuf))
			stv_h2_rxbuf = stv;
	}
	AN(stv_h2_rxbuf);
}

void
STV_close(void)
{
	struct stevedore *stv;
	int i;

	ASSERT_CLI();
	for (i = 1; i >= 0; i--) {
		/* First send close warning */
		STV_Foreach(stv)
			if (stv->close != NULL)
				stv->close(stv, i);
	}
}

/*-------------------------------------------------------------------
 * Notify the stevedores of BAN related events. A non-zero return
 * value indicates that the stevedore is unable to persist the
 * event.
 */

int
STV_BanInfoDrop(const uint8_t *ban, unsigned len)
{
	struct stevedore *stv;
	int r = 0;

	STV_Foreach(stv)
		if (stv->baninfo != NULL)
			r |= stv->baninfo(stv, BI_DROP, ban, len);

	return (r);
}

int
STV_BanInfoNew(const uint8_t *ban, unsigned len)
{
	struct stevedore *stv;
	int r = 0;

	STV_Foreach(stv)
		if (stv->baninfo != NULL)
			r |= stv->baninfo(stv, BI_NEW, ban, len);

	return (r);
}

/*-------------------------------------------------------------------
 * Export a complete ban list to the stevedores for persistence.
 * The stevedores should clear any previous ban lists and replace
 * them with this list.
 */

void
STV_BanExport(const uint8_t *bans, unsigned len)
{
	struct stevedore *stv;

	STV_Foreach(stv)
		if (stv->banexport != NULL)
			stv->banexport(stv, bans, len);
}

/*--------------------------------------------------------------------
 * VRT functions for stevedores
 */

static const struct stevedore *
stv_find(const char *nm)
{
	struct stevedore *stv;

	STV_Foreach(stv)
		if (!strcmp(stv->ident, nm))
			return (stv);
	return (NULL);
}

int
VRT_Stv(const char *nm)
{

	if (stv_find(nm) != NULL)
		return (1);
	return (0);
}

const char * v_matchproto_()
VRT_STEVEDORE_string(VCL_STEVEDORE s)
{
	if (s == NULL)
		return (NULL);
	CHECK_OBJ_NOTNULL(s, STEVEDORE_MAGIC);
	return (s->vclname);
}

VCL_STEVEDORE
VRT_stevedore(const char *nm)
{
	return (stv_find(nm));
}

#define VRTSTVVAR(nm, vtype, ctype, dval)		\
ctype							\
VRT_stevedore_##nm(VCL_STEVEDORE stv)			\
{							\
	if (stv == NULL)				\
		return (0);				\
	if (stv->var_##nm == NULL)			\
		return (dval);				\
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);	\
	return (stv->var_##nm(stv));			\
}
#include "tbl/vrt_stv_var.h"
