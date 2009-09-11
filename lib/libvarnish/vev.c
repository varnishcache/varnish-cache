/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <stdio.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include "libvarnish.h"
#include "vev.h"
#include "miniobj.h"
#include "binary_heap.h"

#undef DEBUG_EVENTS

/* INFTIM indicates an infinite timeout for poll(2) */
#ifndef INFTIM
#define INFTIM -1
#endif

struct vevsig {
	struct vev_base		*vevb;
	struct vev		*vev;
	struct sigaction	sigact;
	unsigned char		happened;
};

static struct vevsig		*vev_sigs;
static int			vev_nsig;

struct vev_base {
	unsigned		magic;
#define VEV_BASE_MAGIC		0x477bcf3d
	VTAILQ_HEAD(,vev)	events;
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
vev_bh_update(void *priv, void *a, unsigned u)
{
	struct vev_base *evb;
	struct vev *e;

	CAST_OBJ_NOTNULL(evb, priv, VEV_BASE_MAGIC);
	CAST_OBJ_NOTNULL(e, a, VEV_MAGIC);
	e->__binheap_idx = u;
}

static int
vev_bh_cmp(void *priv, void *a, void *b)
{
	struct vev_base *evb;
	struct vev *ea, *eb;

	CAST_OBJ_NOTNULL(evb, priv, VEV_BASE_MAGIC);
	CAST_OBJ_NOTNULL(ea, a, VEV_MAGIC);
	CAST_OBJ_NOTNULL(eb, b, VEV_MAGIC);
	return (ea->__when < eb->__when);
}

/*--------------------------------------------------------------------*/

static int
vev_get_pfd(struct vev_base *evb)
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
vev_get_sig(int sig)
{
	struct vevsig *os;

	if (sig < vev_nsig)
		return (0);

	os = calloc(sizeof *os, (sig + 1L));
	if (os == NULL)
		return (ENOMEM);

	memcpy(os, vev_sigs, vev_nsig * sizeof *os);

	free(vev_sigs);
	vev_sigs = os;
	vev_nsig = sig + 1;

	return (0);
}

/*--------------------------------------------------------------------*/

static void
vev_sighandler(int sig)
{
	struct vevsig *es;

	assert(sig < vev_nsig);
	assert(vev_sigs != NULL);
	es = &vev_sigs[sig];
	if (!es->happened)
		es->vevb->psig++;
	es->happened = 1;
}

/*--------------------------------------------------------------------*/

struct vev_base *
vev_new_base(void)
{
	struct vev_base *evb;

	evb = calloc(sizeof *evb, 1);
	if (evb == NULL)
		return (evb);
	if (vev_get_pfd(evb)) {
		free(evb);
		return (NULL);
	}
	evb->magic = VEV_BASE_MAGIC;
	VTAILQ_INIT(&evb->events);
	evb->binheap = binheap_new(evb, vev_bh_cmp, vev_bh_update);
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
vev_destroy_base(struct vev_base *evb)
{
	CHECK_OBJ_NOTNULL(evb, VEV_BASE_MAGIC);
	assert(evb->thread == pthread_self());
	evb->magic = 0;
	free(evb);
}

/*--------------------------------------------------------------------*/

struct vev *
vev_new(void)
{
	struct vev *e;

	e = calloc(sizeof *e, 1);
	if (e != NULL) {
		e->fd = -1;
	}
	return (e);
}

/*--------------------------------------------------------------------*/

int
vev_add(struct vev_base *evb, struct vev *e)
{
	struct vevsig *es;

	CHECK_OBJ_NOTNULL(evb, VEV_BASE_MAGIC);
	assert(e->magic != VEV_MAGIC);
	assert(e->callback != NULL);
	assert(e->sig >= 0);
	assert(e->timeout >= 0.0);
	assert(e->fd < 0 || e->fd_flags);
	assert(evb->thread == pthread_self());
	DBG(evb, "ev_add(%p) fd = %d\n", e, e->fd);

	if (e->sig > 0 && vev_get_sig(e->sig))
		return (ENOMEM);

	if (e->fd >= 0 && vev_get_pfd(evb))
		return (ENOMEM);

	if (e->sig > 0) {
		es = &vev_sigs[e->sig];
		if (es->vev != NULL)
			return (EBUSY);
		assert(es->happened == 0);
		es->vev = e;
		es->vevb = evb;
		es->sigact.sa_flags = e->sig_flags;
		es->sigact.sa_handler = vev_sighandler;
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

	e->magic = VEV_MAGIC;	/* before binheap_insert() */

	if (e->timeout != 0.0) {
		e->__when += TIM_mono() + e->timeout;
		binheap_insert(evb->binheap, e);
		assert(e->__binheap_idx > 0);
		DBG(evb, "... bidx = %d\n", e->__binheap_idx);
	} else {
		e->__when = 0.0;
		e->__binheap_idx = 0;
	}

	e->__vevb = evb;
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
vev_del(struct vev_base *evb, struct vev *e)
{
	struct vevsig *es;

	CHECK_OBJ_NOTNULL(evb, VEV_BASE_MAGIC);
	CHECK_OBJ_NOTNULL(e, VEV_MAGIC);
	DBG(evb, "ev_del(%p) fd = %d\n", e, e->fd);
	assert(evb == e->__vevb);
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
		assert(e->sig < vev_nsig);
		es = &vev_sigs[e->sig];
		assert(es->vev == e);
		es->vev = NULL;
		es->vevb = NULL;
		es->sigact.sa_flags = e->sig_flags;
		es->sigact.sa_handler = SIG_DFL;
		assert(sigaction(e->sig, &es->sigact, NULL) == 0);
		es->happened = 0;
	}

	VTAILQ_REMOVE(&evb->events, e, __list);

	e->magic = 0;
	e->__vevb = NULL;

	evb->disturbed = 1;
}

/*--------------------------------------------------------------------*/

int
vev_schedule(struct vev_base *evb)
{
	int i;

	CHECK_OBJ_NOTNULL(evb, VEV_BASE_MAGIC);
	assert(evb->thread == pthread_self());
	do
		i = vev_schedule_one(evb);
	while (i == 1);
	return (i);
}

/*--------------------------------------------------------------------*/

static void
vev_compact_pfd(struct vev_base *evb)
{
	unsigned u;
	struct pollfd *p;
	struct vev *ep;
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
vev_sched_timeout(struct vev_base *evb, struct vev *e, double t)
{
	int i;


	i = e->callback(e, 0);
	if (i) {
		vev_del(evb, e);
		free(e);
	} else {
		e->__when = t + e->timeout;
		binheap_delete(evb->binheap, e->__binheap_idx);
		binheap_insert(evb->binheap, e);
	}
	return (1);
}

static int
vev_sched_signal(struct vev_base *evb)
{
	int i, j;
	struct vevsig *es;
	struct vev *e;

	es = vev_sigs;
	for (j = 0; j < vev_nsig; j++, es++) {
		if (!es->happened || es->vevb != evb)
			continue;
		evb->psig--;
		es->happened = 0;
		e = es->vev;
		assert(e != NULL);
		i = e->callback(e, EV_SIG);
		if (i) {
			vev_del(evb, e);
			free(e);
		}
	}
	return (1);
}

int
vev_schedule_one(struct vev_base *evb)
{
	double t;
	struct vev *e, *e2, *e3;
	int i, j, tmo;
	struct pollfd *pfd;

	CHECK_OBJ_NOTNULL(evb, VEV_BASE_MAGIC);
	assert(evb->thread == pthread_self());
	e = binheap_root(evb->binheap);
	if (e != NULL) {
		CHECK_OBJ_NOTNULL(e, VEV_MAGIC);
		assert(e->__binheap_idx == 1);
		t = TIM_mono();
		if (e->__when <= t)
			return (vev_sched_timeout(evb, e, t));
		tmo = (int)((e->__when - t) * 1e3);
		if (tmo == 0)
			tmo = 1;
	} else
		tmo = INFTIM;

	if (evb->compact_pfd)
		vev_compact_pfd(evb);

	if (tmo == INFTIM && evb->lpfd == 0)
		return (0);

	if (evb->psig)
		return (vev_sched_signal(evb));
	assert(evb->lpfd < evb->npfd);
	i = poll(evb->pfd, evb->lpfd, tmo);
	if (i == -1 && errno == EINTR)
		return (vev_sched_signal(evb));
	if (i == 0) {
		assert(e != NULL);
		t = TIM_mono();
		if (e->__when <= t)
			return (vev_sched_timeout(evb, e, t));
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
			vev_del(evb, e);
			evb->disturbed = 0;
			free(e);
		}
	}
	assert(i == 0);
	return (1);
}
