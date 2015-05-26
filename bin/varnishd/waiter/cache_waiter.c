/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 */

#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache/cache.h"

#include "waiter/waiter.h"
#include "waiter/waiter_priv.h"
#include "waiter/mgt_waiter.h"

int
Wait_Enter(const struct waiter *w, struct waited *wp)
{

	CHECK_OBJ_NOTNULL(w, WAITER_MAGIC);
	CHECK_OBJ_NOTNULL(wp, WAITED_MAGIC);
	assert(wp->fd > 0);			// stdin never comes here
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
Waiter_New(waiter_handle_f *func, volatile double *tmo)
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
	w->func = func;
	w->tmo = tmo;
	VTAILQ_INIT(&w->waithead);

	waiter->init(w);

	return (w);
}

void
Waiter_Destroy(struct waiter **wp)
{
	struct waiter *w;

	AN(wp);
	w = *wp;
	*wp = NULL;
	CHECK_OBJ_NOTNULL(w, WAITER_MAGIC);

	AN(w->impl->fini);
	w->impl->fini(w);
	FREE_OBJ(w);
}
