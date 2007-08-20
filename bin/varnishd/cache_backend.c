/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2007 Linpro AS
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
 * $Id$
 *
 * Manage backend connections and requests.
 *
 */

#include <stdlib.h>
#include <unistd.h>

#include "heritage.h"
#include "shmlog.h"
#include "cache.h"

static TAILQ_HEAD(,bereq) bereq_head = TAILQ_HEAD_INITIALIZER(bereq_head);

static MTX VBE_mtx;

struct backendlist backendlist = TAILQ_HEAD_INITIALIZER(backendlist);

/*--------------------------------------------------------------------
 * Get a http structure for talking to the backend.
 */

struct bereq *
VBE_new_bereq(void)
{
	struct bereq *bereq;
	volatile unsigned len;

	LOCK(&VBE_mtx);
	bereq = TAILQ_FIRST(&bereq_head);
	if (bereq != NULL)
		TAILQ_REMOVE(&bereq_head, bereq, list);
	UNLOCK(&VBE_mtx);
	if (bereq != NULL) {
		CHECK_OBJ(bereq, BEREQ_MAGIC);
	} else {
		len =  params->mem_workspace;
		bereq = calloc(sizeof *bereq + len, 1);
		if (bereq == NULL)
			return (NULL);
		bereq->magic = BEREQ_MAGIC;
		bereq->space = bereq + 1;
		bereq->len = len;
	}
	http_Setup(bereq->http, bereq->space, bereq->len);
	return (bereq);
}

/*--------------------------------------------------------------------*/
/* XXX: no backpressure on pool size */

void
VBE_free_bereq(struct bereq *bereq)
{

	CHECK_OBJ_NOTNULL(bereq, BEREQ_MAGIC);
	LOCK(&VBE_mtx);
	TAILQ_INSERT_HEAD(&bereq_head, bereq, list);
	UNLOCK(&VBE_mtx);
}

/*--------------------------------------------------------------------*/

struct backend *
VBE_NewBackend(struct backend_method *method)
{
	struct backend *b;

	b = calloc(sizeof *b, 1);
	XXXAN(b);
	b->magic = BACKEND_MAGIC;
	TAILQ_INIT(&b->connlist);
	b->method = method;
	b->refcount = 1;
	TAILQ_INSERT_TAIL(&backendlist, b, list);
	return (b);
}

/*--------------------------------------------------------------------*/

void
VBE_DropRef(struct backend *b)
{

	CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);

	b->refcount--;
	if (b->refcount > 0)
		return;
	TAILQ_REMOVE(&backendlist, b, list);
	free(b->vcl_name);
	free(b->portname);
	free(b->hostname);
	free(b);
}

/*--------------------------------------------------------------------*/

struct vbe_conn *
VBE_GetFd(struct sess *sp)
{

	return(sp->backend->method->getfd(sp));
}

/* Close a connection ------------------------------------------------*/

void
VBE_ClosedFd(struct worker *w, struct vbe_conn *vc)
{

	vc->backend->method->close(w, vc);
}

/* Recycle a connection ----------------------------------------------*/

void
VBE_RecycleFd(struct worker *w, struct vbe_conn *vc)
{

	vc->backend->method->recycle(w, vc);
}

/*--------------------------------------------------------------------*/

void
VBE_Init(void)
{

	MTX_INIT(&VBE_mtx);
	backend_method_simple.init();
}
