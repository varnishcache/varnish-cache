/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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

#include "config.h"

#include "cache/cache_varnishd.h"

#include <stdlib.h>

#include "vbh.h"

#include "waiter/waiter.h"
#include "waiter/waiter_priv.h"
#include "waiter/mgt_waiter.h"

#include "VSC_waiter.h"

static int v_matchproto_(vbh_cmp_t)
waited_cmp(void *priv, const void *a, const void *b)
{
	const struct waiter *ww;
	const struct waited *aa, *bb;

	CAST_OBJ_NOTNULL(ww, priv, WAITER_MAGIC);
	CAST_OBJ_NOTNULL(aa, a, WAITED_MAGIC);
	CAST_OBJ_NOTNULL(bb, b, WAITED_MAGIC);

	return (Wait_When(aa) < Wait_When(bb));
}

static void v_matchproto_(vbh_update_t)
waited_update(void *priv, void *p, unsigned u)
{
	struct waited *pp;

	(void)priv;
	CAST_OBJ_NOTNULL(pp, p, WAITED_MAGIC);
	pp->idx = u;
}

/**********************************************************************/

void
Wait_Call(const struct waiter *w, struct waited *wp,
    enum wait_event ev, double now)
{
	CHECK_OBJ_NOTNULL(w, WAITER_MAGIC);
	CHECK_OBJ_NOTNULL(wp, WAITED_MAGIC);
	AN(wp->func);
	assert(wp->idx == VBH_NOIDX);
	AN(w->vsc);

	switch (ev) {
	case WAITER_REMCLOSE:
		w->vsc->remclose++;
		break;
	case WAITER_TIMEOUT:
		w->vsc->timeout++;
		break;
	case WAITER_ACTION:
		w->vsc->action++;
		break;
	default:
		break;
	}

	wp->func(wp, ev, now);
}

/**********************************************************************/

void
Wait_HeapInsert(const struct waiter *w, struct waited *wp)
{
	CHECK_OBJ_NOTNULL(w, WAITER_MAGIC);
	CHECK_OBJ_NOTNULL(wp, WAITED_MAGIC);
	assert(wp->idx == VBH_NOIDX);
	AN(w->vsc);
	w->vsc->conns++;
	VBH_insert(w->heap, wp);
}

/*
 * XXX: wp is const because otherwise FlexeLint complains.  However, *wp
 * XXX: will actually change as a result of calling this function, via
 * XXX: the pointer stored in the bin-heap.  I can see how this const
 * XXX: could maybe confuse a compilers optimizer, but I do not expect
 * XXX: any harm to come from it.  Caveat Emptor.
 */

int
Wait_HeapDelete(const struct waiter *w, const struct waited *wp)
{
	CHECK_OBJ_NOTNULL(w, WAITER_MAGIC);
	CHECK_OBJ_NOTNULL(wp, WAITED_MAGIC);
	if (wp->idx == VBH_NOIDX)
		return (0);
	AN(w->vsc);
	w->vsc->conns--;
	VBH_delete(w->heap, wp->idx);
	return (1);
}

double
Wait_HeapDue(const struct waiter *w, struct waited **wpp)
{
	struct waited *wp;

	wp = VBH_root(w->heap);
	CHECK_OBJ_ORNULL(wp, WAITED_MAGIC);
	if (wp == NULL) {
		if (wpp != NULL)
			*wpp = NULL;
		return (0);
	}
	if (wpp != NULL)
		*wpp = wp;
	return (Wait_When(wp));
}

/**********************************************************************/

int
Wait_Enter(const struct waiter *w, struct waited *wp)
{

	CHECK_OBJ_NOTNULL(w, WAITER_MAGIC);
	CHECK_OBJ_NOTNULL(wp, WAITED_MAGIC);
	assert(wp->fd > 0);			// stdin never comes here
	AN(wp->func);
	wp->idx = VBH_NOIDX;
	return (w->impl->enter(w->priv, wp));
}

/**********************************************************************/

const char *
Waiter_GetName(void)
{

	if (waiter != NULL)
		return (waiter->name);
	else
		return ("(No Waiter?)");
}

struct waiter *
Waiter_New(const char *name)
{
	struct waiter *w;

	AN(waiter);
	AN(waiter->name);
	AN(waiter->init);
	AN(waiter->enter);
	AN(waiter->fini);

	w = calloc(1, sizeof (struct waiter) + waiter->size);
	AN(w);
	INIT_OBJ(w, WAITER_MAGIC);
	w->priv = (void*)(w + 1);
	w->impl = waiter;
	VTAILQ_INIT(&w->waithead);
	w->heap = VBH_new(w, waited_cmp, waited_update);

	AZ(w->vsc);
	w->vsc = VSC_waiter_New(NULL, NULL, "%s", name);
	AN(w->vsc);

	waiter->init(w);

	return (w);
}

void
Waiter_Destroy(struct waiter **wp)
{
	struct waiter *w;

	TAKE_OBJ_NOTNULL(w, wp, WAITER_MAGIC);

	AZ(VBH_root(w->heap));
	AN(w->impl->fini);
	w->impl->fini(w);
	FREE_OBJ(w);
}
