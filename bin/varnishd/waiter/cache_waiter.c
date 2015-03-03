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
#include <stdio.h>
#include <stdlib.h>

#include "cache/cache.h"

#include "vfil.h"
#include "vtim.h"

#include "waiter/waiter.h"
#include "waiter/waiter_priv.h"

#define NEV 8192

static VTAILQ_HEAD(, waiter)	waiters = VTAILQ_HEAD_INITIALIZER(waiters);
static int			nwaiters;
static struct lock		wait_mtx;
static pthread_t		wait_thr;

static void *
wait_poker_thread(void *arg)
{
	struct waiter *w;
	double now;

	(void)arg;
	THR_SetName("Waiter timer");
	while (1) {
		/* Avoid thundering herds and resonances */
		(void)usleep(990013/nwaiters);

		now = VTIM_real();

		Lck_Lock(&wait_mtx);
		w = VTAILQ_FIRST(&waiters);
		VTAILQ_REMOVE(&waiters, w, list);
		VTAILQ_INSERT_TAIL(&waiters, w, list);
		assert(w->pipes[1] >= 0);

		if (w->next_idle + *w->tmo < now)
			(void)write(w->pipes[1], &w->pipe_w, sizeof w->pipe_w);
		Lck_Unlock(&wait_mtx);
	}
	NEEDLESS_RETURN(NULL);
}

const char *
Wait_GetName(void)
{

	if (waiter != NULL)
		return (waiter->name);
	else
		return ("no_waiter");
}

struct waiter *
Wait_New(waiter_handle_f *func, volatile double *tmo)
{
	struct waiter *w;

	AN(waiter);
	AN(waiter->name);
	AN(waiter->init);

	w = calloc(1, sizeof (struct waiter) + waiter->size);
	AN(w);
	INIT_OBJ(w, WAITER_MAGIC);
	w->priv = (void*)(w + 1);
	w->impl = waiter;
	w->func = func;
	w->tmo = tmo;
	w->pipes[0] = w->pipes[1] = -1;
	VTAILQ_INIT(&w->waithead);

	waiter->init(w);
	AN(w->impl->pass || w->pipes[1] >= 0);

	Lck_Lock(&wait_mtx);
	VTAILQ_INSERT_TAIL(&waiters, w, list);
	nwaiters++;

	/* We assume all waiters either use pipes or don't use pipes */
	if (w->pipes[1] >= 0 && nwaiters == 1)
		AZ(pthread_create(&wait_thr, NULL, wait_poker_thread, NULL));
	Lck_Unlock(&wait_mtx);
	return (w);
}

void
Wait_Destroy(struct waiter **wp)
{
	struct waiter *w;
	struct waited *wx = NULL;
	int written;
	double now;

	AN(wp);
	w = *wp;
	*wp = NULL;
	CHECK_OBJ_NOTNULL(w, WAITER_MAGIC);

	Lck_Lock(&wait_mtx);
	VTAILQ_REMOVE(&waiters, w, list);
	w->dismantle = 1;
	Lck_Unlock(&wait_mtx);

	if (w->pipes[1] >= 0) {
		while (1) {
			written = write(w->pipes[1], &wx, sizeof wx);
			if (written == sizeof wx)
				break;
			(void)usleep(10000);
		}
	}
	AN(w->impl->fini);
	w->impl->fini(w);
	now = VTIM_real();
	while (1) {
		wx = VTAILQ_FIRST(&w->waithead);
		if (wx == NULL)
			break;
		VTAILQ_REMOVE(&w->waithead, wx, list);
		if (wx == w->pipe_w)
			FREE_OBJ(wx);
		else
			w->func(wx, WAITER_CLOSE, now);
	}
	FREE_OBJ(w);
}

void
Wait_UsePipe(struct waiter *w)
{
	CHECK_OBJ_NOTNULL(w, WAITER_MAGIC);

	AN(waiter->inject);
	AZ(pipe(w->pipes));
	AZ(VFIL_nonblocking(w->pipes[0]));
	AZ(VFIL_nonblocking(w->pipes[1]));
	ALLOC_OBJ(w->pipe_w, WAITED_MAGIC);
	w->pipe_w->fd = w->pipes[0];
	w->pipe_w->idle = 0;
	VTAILQ_INSERT_HEAD(&w->waithead, w->pipe_w, list);
	waiter->inject(w, w->pipe_w);
}

int
Wait_Enter(const struct waiter *w, struct waited *wp)
{
	ssize_t written;
	uintptr_t up;

	CHECK_OBJ_NOTNULL(w, WAITER_MAGIC);
	CHECK_OBJ_NOTNULL(wp, WAITED_MAGIC);
	assert(wp->fd > 0);		// stdin never comes here
	AZ(w->dismantle);

	if (w->impl->pass != NULL)
		return (w->impl->pass(w->priv, wp));

	assert(w->pipes[1] > 0);

	up = (uintptr_t)wp;
	written = write(w->pipes[1], &up, sizeof up);
	if (written != sizeof up && (errno == EAGAIN || errno == EWOULDBLOCK))
		return (-1);
	assert (written == sizeof up);
	return (0);
}

static void
wait_updidle(struct waiter *w, double now)
{
	struct waited *wp;

	wp = VTAILQ_FIRST(&w->waithead);
	if (wp == NULL)
		return;
	if (wp == w->pipe_w) {
		VTAILQ_REMOVE(&w->waithead, wp, list);
		VTAILQ_INSERT_TAIL(&w->waithead, wp, list);
		wp->idle = now;
		wp = VTAILQ_FIRST(&w->waithead);
	}
	w->next_idle = wp->idle;
}

void
Wait_Handle(struct waiter *w, struct waited *wp, enum wait_event ev, double now)
{
	uintptr_t ss[NEV];
	struct waited *wp2;
	int i, j, dotimer = 0;

	CHECK_OBJ_NOTNULL(w, WAITER_MAGIC);
	CHECK_OBJ_ORNULL(wp, WAITED_MAGIC);

	if (wp != NULL) {
		if (wp == w->pipe_w) {
			w->do_pipe = 1;
			VTAILQ_REMOVE(&w->waithead, w->pipe_w, list);
			wp->idle = now;
			VTAILQ_INSERT_TAIL(&w->waithead, w->pipe_w, list);
		} else {
			if (w->impl->evict != NULL)
				w->impl->evict(w, wp);

			VTAILQ_REMOVE(&w->waithead, wp, list);
			w->func(wp, ev, now);
			wait_updidle(w, now);
		}
		return;
	}

	AZ(wp);

	if (!w->do_pipe)
		return;

	w->do_pipe = 0;

	i = read(w->pipes[0], ss, sizeof ss);
	if (i == -1 && errno == EAGAIN)
		return;

	for (j = 0; i >= sizeof ss[0]; j++, i -= sizeof ss[0]) {
		if (ss[j] == 0) {
			AN(w->dismantle);
			continue;
		}
		ss[j] &= ~1;
		CAST_OBJ_NOTNULL(wp2, (void*)ss[j], WAITED_MAGIC);
		if (wp2 == w->pipe_w) {
			dotimer = 1;
		} else {
			assert(wp2->fd >= 0);
			VTAILQ_INSERT_TAIL(&w->waithead, wp2, list);
			w->impl->inject(w, wp2);
		}
	}
	AZ(i);

	wait_updidle(w, now);

	if (!dotimer)
		return;

	VTAILQ_FOREACH_SAFE(wp, &w->waithead, list, wp2) {
		if (wp == w->pipe_w)
			continue;
		if (wp->idle + *w->tmo > now)
			break;
		if (w->impl->evict != NULL)
			w->impl->evict(w, wp);
		VTAILQ_REMOVE(&w->waithead, wp, list);
		w->func(wp, WAITER_TIMEOUT, now);
	}
	wait_updidle(w, now);
}

void
Wait_Init(void)
{

	Lck_New(&wait_mtx, lck_misc);
}
