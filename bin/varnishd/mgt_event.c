/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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
 */

#include "config.h"

#include <stdio.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include "mgt.h"
#include "mgt_event.h"
#include "miniobj.h"
#include "binary_heap.h"

#undef DEBUG_EVENTS

/* INFTIM indicates an infinite timeout for poll(2) */
#ifndef INFTIM
#define INFTIM -1
#endif

struct evsig {
	struct evbase		*evb;
	struct ev		*ev;
	struct sigaction	sigact;
	unsigned char		happened;
};

static struct evsig		*ev_sigs;
static int			ev_nsig;

struct evbase {
	unsigned		magic;
#define EVBASE_MAGIC		0x0cfd976f
	VTAILQ_HEAD(,ev)		events;
	struct pollfd		*pfd;
	unsigned		npfd;
	unsigned		lpfd;
	struct binheap		*binheap;
	unsigned char		compact_pfd;
	unsigned char		disturbed;
	unsigned		psig;
	pthread_t		thread;
#ifdef DEBUG_EVENTS
	FILE			*debug;
#endif
};

/*--------------------------------------------------------------------*/

#ifdef DEBUG_EVENTS
#define DBG(evb, ...) do {				\
	if ((evb)->debug != NULL)			\
		fprintf((evb)->debug, __VA_ARGS__);	\
	} while (0);
#else
#define DBG(evb, ...)	/* ... */
#endif

/*--------------------------------------------------------------------*/

static void
ev_bh_update(void *priv, void *a, unsigned u)
{
	struct evbase *evb;
	struct ev *e;

	CAST_OBJ_NOTNULL(evb, priv, EVBASE_MAGIC);
	CAST_OBJ_NOTNULL(e, a, EV_MAGIC);
	e->__binheap_idx = u;
}

static int
ev_bh_cmp(void *priv, void *a, void *b)
{
	struct evbase *evb;
	struct ev *ea, *eb;

	CAST_OBJ_NOTNULL(evb, priv, EVBASE_MAGIC);
	CAST_OBJ_NOTNULL(ea, a, EV_MAGIC);
	CAST_OBJ_NOTNULL(eb, b, EV_MAGIC);
	return (ea->__when < eb->__when);
}

/*--------------------------------------------------------------------*/

static int
ev_get_pfd(struct evbase *evb)
{
	unsigned u;
	void *p;

	if (evb->lpfd + 1 < evb->npfd)
		return (0);

	if (evb->npfd < 8)
		u = 8;
	else if (evb->npfd > 256)
		u = evb->npfd + 256;
	else
		u = evb->npfd * 2;
	p = realloc(evb->pfd, sizeof *evb->pfd * u);
	if (p == NULL)
		return (1);
	evb->npfd = u;
	evb->pfd = p;
	return (0);
}

/*--------------------------------------------------------------------*/

static int
ev_get_sig(int sig)
{
	struct evsig *os;

	if (sig < ev_nsig)
		return (0);

	os = calloc(sizeof *os, (sig + 1));
	if (os == NULL)
		return (ENOMEM);

	memcpy(os, ev_sigs, ev_nsig * sizeof *os);

	free(ev_sigs);
	ev_sigs = os;
	ev_nsig = sig + 1;

	return (0);
}

/*--------------------------------------------------------------------*/

static void
ev_sighandler(int sig)
{
	struct evsig *es;

	assert(sig < ev_nsig);
	assert(ev_sigs != NULL);
	es = &ev_sigs[sig];
	if (!es->happened)
		es->evb->psig++;
	es->happened = 1;
}

/*--------------------------------------------------------------------*/

struct evbase *
ev_new_base(void)
{
	struct evbase *evb;

	evb = calloc(sizeof *evb, 1);
	if (evb == NULL)
		return (evb);
	if (ev_get_pfd(evb)) {
		free(evb);
		return (NULL);
	}
	evb->magic = EVBASE_MAGIC;
	VTAILQ_INIT(&evb->events);
	evb->binheap = binheap_new(evb, ev_bh_cmp, ev_bh_update);
	evb->thread = pthread_self();
#ifdef DEBUG_EVENTS
	evb->debug = fopen("/tmp/_.events", "w");
	AN(evb->debug);
	setbuf(evb->debug, NULL);
	DBG(evb, "\n\nStart debugging\n");
#endif
	return (evb);
}

/*--------------------------------------------------------------------*/

void
ev_destroy_base(struct evbase *evb)
{
	CHECK_OBJ_NOTNULL(evb, EVBASE_MAGIC);
	assert(evb->thread == pthread_self());
	evb->magic = 0;
	free(evb);
}

/*--------------------------------------------------------------------*/

struct ev *
ev_new(void)
{
	struct ev *e;

	e = calloc(sizeof *e, 1);
	if (e != NULL) {
		e->fd = -1;
	}
	return (e);
}

/*--------------------------------------------------------------------*/

int
ev_add(struct evbase *evb, struct ev *e)
{
	struct evsig *es;

	CHECK_OBJ_NOTNULL(evb, EVBASE_MAGIC);
	assert(e->magic != EV_MAGIC);
	assert(e->callback != NULL);
	assert(e->sig >= 0);
	assert(e->timeout >= 0.0);
	assert(e->fd < 0 || e->fd_flags);
	assert(evb->thread == pthread_self());
	DBG(evb, "ev_add(%p) fd = %d\n", e, e->fd);

	if (e->sig > 0 && ev_get_sig(e->sig))
		return (ENOMEM);

	if (e->fd >= 0 && ev_get_pfd(evb))
		return (ENOMEM);

	if (e->sig > 0) {
		es = &ev_sigs[e->sig];
		if (es->ev != NULL)
			return (EBUSY);
		assert(es->happened == 0);
		es->ev = e;
		es->evb = evb;
		es->sigact.sa_flags = e->sig_flags;
		es->sigact.sa_handler = ev_sighandler;
	} else {
		es = NULL;
	}

	if (e->fd >= 0) {
		assert(evb->lpfd < evb->npfd);
		evb->pfd[evb->lpfd].fd = e->fd;
		evb->pfd[evb->lpfd].events =
		    e->fd_flags & (EV_RD|EV_WR|EV_ERR|EV_HUP);
		e->__poll_idx = evb->lpfd;
		evb->lpfd++;
		DBG(evb, "... pidx = %d lpfd = %d\n",
		    e->__poll_idx, evb->lpfd);
	} else
		e->__poll_idx = -1;

	e->magic = EV_MAGIC;	/* before binheap_insert() */

	if (e->timeout != 0.0) {
		e->__when += TIM_mono() + e->timeout;
		binheap_insert(evb->binheap, e);
		assert(e->__binheap_idx > 0);
		DBG(evb, "... bidx = %d\n", e->__binheap_idx);
	} else {
		e->__when = 0.0;
		e->__binheap_idx = 0;
	}

	e->__evb = evb;
	e->__privflags = 0;
	if (e->fd < 0)
		VTAILQ_INSERT_TAIL(&evb->events, e, __list);
	else
		VTAILQ_INSERT_HEAD(&evb->events, e, __list);

	if (e->sig > 0) {
		assert(es != NULL);
		assert(sigaction(e->sig, &es->sigact, NULL) == 0);
	}

	return (0);
}

/*--------------------------------------------------------------------*/

void
ev_del(struct evbase *evb, struct ev *e)
{
	struct evsig *es;

	CHECK_OBJ_NOTNULL(evb, EVBASE_MAGIC);
	CHECK_OBJ_NOTNULL(e, EV_MAGIC);
	DBG(evb, "ev_del(%p) fd = %d\n", e, e->fd);
	assert(evb == e->__evb);
	assert(evb->thread == pthread_self());

	if (e->__binheap_idx != 0)
		binheap_delete(evb->binheap, e->__binheap_idx);
	assert(e->__binheap_idx == 0);

	if (e->fd >= 0) {
		DBG(evb, "... pidx = %d\n", e->__poll_idx);
		evb->pfd[e->__poll_idx].fd = -1;
		if (e->__poll_idx == evb->lpfd - 1)
			evb->lpfd--;
		else
			evb->compact_pfd++;
		e->fd = -1;
		DBG(evb, "... lpfd = %d\n", evb->lpfd);
	}

	if (e->sig > 0) {
		assert(e->sig < ev_nsig);
		es = &ev_sigs[e->sig];
		assert(es->ev == e);
		es->ev = NULL;
		es->evb = NULL;
		es->sigact.sa_flags = e->sig_flags;
		es->sigact.sa_handler = SIG_DFL;
		assert(sigaction(e->sig, &es->sigact, NULL) == 0);
		es->happened = 0;
	}

	VTAILQ_REMOVE(&evb->events, e, __list);

	e->magic = 0;
	e->__evb = NULL;

	evb->disturbed = 1;
}

/*--------------------------------------------------------------------*/

int
ev_schedule(struct evbase *evb)
{
	int i;

	CHECK_OBJ_NOTNULL(evb, EVBASE_MAGIC);
	assert(evb->thread == pthread_self());
	do
		i = ev_schedule_one(evb);
	while (i == 1);
	return (i);
}

/*--------------------------------------------------------------------*/

static void
ev_compact_pfd(struct evbase *evb)
{
	unsigned u;
	struct pollfd *p;
	struct ev *ep;
	int lfd;

	DBG(evb, "compact_pfd() lpfd = %d\n", evb->lpfd);
	p = evb->pfd;
	for (u = 0; u < evb->lpfd; u++, p++) {
		DBG(evb, "...[%d] fd = %d\n", u, p->fd);
		if (p->fd >= 0)
			continue;
		if (u == evb->lpfd - 1)
			break;
		lfd = evb->pfd[evb->lpfd - 1].fd;
		VTAILQ_FOREACH(ep, &evb->events, __list)
			if (ep->fd == lfd)
				break;
		AN(ep);
		DBG(evb, "...[%d] move %p pidx %d\n", u, ep, ep->__poll_idx);
		*p = evb->pfd[--evb->lpfd];
		ep->__poll_idx = u;
	}
	evb->lpfd = u;
	evb->compact_pfd = 0;
	DBG(evb, "... lpfd = %d\n", evb->lpfd);
}

/*--------------------------------------------------------------------*/

static int
ev_sched_timeout(struct evbase *evb, struct ev *e, double t)
{
	int i;


	i = e->callback(e, 0);
	if (i) {
		ev_del(evb, e);
		free(e);
	} else {
		e->__when = t + e->timeout;
		binheap_delete(evb->binheap, e->__binheap_idx);
		binheap_insert(evb->binheap, e);
	}
	return (1);
}

static int
ev_sched_signal(struct evbase *evb)
{
	int i, j;
	struct evsig *es;
	struct ev *e;

	es = ev_sigs;
	for (j = 0; j < ev_nsig; j++, es++) {
		if (!es->happened || es->evb != evb)
			continue;
		evb->psig--;
		es->happened = 0;
		e = es->ev;
		assert(e != NULL);
		i = e->callback(e, EV_SIG);
		if (i) {
			ev_del(evb, e);
			free(e);
		}
	}
	return (1);
}

int
ev_schedule_one(struct evbase *evb)
{
	double t;
	struct ev *e, *e2, *e3;
	int i, j, tmo;
	struct pollfd *pfd;

	CHECK_OBJ_NOTNULL(evb, EVBASE_MAGIC);
	assert(evb->thread == pthread_self());
	e = binheap_root(evb->binheap);
	if (e != NULL) {
		CHECK_OBJ_NOTNULL(e, EV_MAGIC);
		assert(e->__binheap_idx == 1);
		t = TIM_mono();
		if (e->__when <= t)
			return (ev_sched_timeout(evb, e, t));
		tmo = (int)((e->__when - t) * 1e3);
		if (tmo == 0)
			tmo = 1;
	} else
		tmo = INFTIM;

	if (evb->compact_pfd)
		ev_compact_pfd(evb);

	if (tmo == INFTIM && evb->lpfd == 0)
		return (0);

	if (evb->psig)
		return (ev_sched_signal(evb));
	assert(evb->lpfd < evb->npfd);
	i = poll(evb->pfd, evb->lpfd, tmo);
	if(i == -1 && errno == EINTR)
		return (ev_sched_signal(evb));
	if (i == 0) {
		assert(e != NULL);
		t = TIM_mono();
		if (e->__when <= t)
			return (ev_sched_timeout(evb, e, t));
	}
	evb->disturbed = 0;
	VTAILQ_FOREACH_SAFE(e, &evb->events, __list, e2) {
		if (i == 0)
			break;
		if (e->fd < 0)
			continue;
		assert(e->__poll_idx < evb->lpfd);
		pfd = &evb->pfd[e->__poll_idx];
		assert(pfd->fd == e->fd);
		if (!pfd->revents)
			continue;
		DBG(evb, "callback(%p) fd = %d what = 0x%x pidx = %d\n",
		    e, e->fd, pfd->revents, e->__poll_idx);
		j = e->callback(e, pfd->revents);
		i--;
		if (evb->disturbed) {
			VTAILQ_FOREACH(e3, &evb->events, __list) {
				if (e3 == e) {
					e3 = VTAILQ_NEXT(e, __list);
					break;
				} else if (e3 == e2)
					break;
			}
			e2 = e3;
			evb->disturbed = 0;
		}
		if (j) {
			ev_del(evb, e);
			evb->disturbed = 0;
			free(e);
		}
	}
	assert(i == 0);
	return (1);
}
