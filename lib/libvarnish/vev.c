/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Varnish Software AS
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

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vdef.h"
#include "miniobj.h"
#include "vas.h"

#include "binary_heap.h"
#include "vev.h"
#include "vtim.h"

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
	struct pollfd		*pfd;
	struct vev		**pev;
	unsigned		npfd;
	unsigned		lpfd;
	struct binheap		*binheap;
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
//#define DBG(evb, ...)	fprintf(stderr, __VA_ARGS__);
#endif

/*--------------------------------------------------------------------*/

static void __match_proto__(binheap_update_t)
vev_bh_update(void *priv, void *a, unsigned u)
{
	struct vev_base *evb;
	struct vev *e;

	CAST_OBJ_NOTNULL(evb, priv, VEV_BASE_MAGIC);
	CAST_OBJ_NOTNULL(e, a, VEV_MAGIC);
	assert(u < evb->lpfd);
	e->__binheap_idx = u;
	if (u != BINHEAP_NOIDX) {
		evb->pev[u] = e;
		evb->pfd[u].fd = e->fd;
		evb->pfd[u].events =
		    e->fd_flags & (EV_RD|EV_WR|EV_ERR|EV_HUP);
	}
}

static int __match_proto__(binheap_cmp_t)
vev_bh_cmp(void *priv, const void *a, const void *b)
{
	struct vev_base *evb;
	const struct vev *ea, *eb;

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

	if (evb->lpfd + 1 < evb->npfd)
		return (0);

	if (evb->npfd < 8)
		u = 8;
	else if (evb->npfd > 256)
		u = evb->npfd + 256;
	else
		u = evb->npfd * 2;
	evb->npfd = u;
	evb->pfd = realloc(evb->pfd, sizeof(*evb->pfd) * u);
	AN(evb->pfd);
	evb->pev = realloc(evb->pev, sizeof(*evb->pev) * u);
	AN(evb->pev);
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
	evb->lpfd = BINHEAP_NOIDX + 1;
	if (vev_get_pfd(evb)) {
		free(evb);
		return (NULL);
	}
	evb->magic = VEV_BASE_MAGIC;
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

	if (vev_get_pfd(evb))
		return (ENOMEM);

	if (e->sig > 0) {
		if (vev_get_sig(e->sig))
			return (ENOMEM);

		assert(e->fd < 0);
		es = &vev_sigs[e->sig];
		if (es->vev != NULL)
			return (EBUSY);
		AZ(es->happened);
		es->vev = e;
		es->vevb = evb;
		es->sigact.sa_flags = e->sig_flags;
		es->sigact.sa_handler = vev_sighandler;
	} else {
		es = NULL;
	}

	e->magic = VEV_MAGIC;	/* before binheap_insert() */

	if (e->timeout != 0.0)
		e->__when += VTIM_mono() + e->timeout;
	else
		e->__when = 9e99;

	evb->lpfd++;
	binheap_insert(evb->binheap, e);
	assert(e->__binheap_idx != BINHEAP_NOIDX);

	e->__vevb = evb;
	e->__privflags = 0;

	if (e->sig > 0) {
		assert(es != NULL);
		AZ(sigaction(e->sig, &es->sigact, NULL));
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
	DBG(evb, "ev_del(%p) fd = %d i=%u L=%d\n", e, e->fd, e->__binheap_idx, evb->lpfd);
	assert(evb == e->__vevb);
	assert(evb->thread == pthread_self());
	assert(evb->pev[e->__binheap_idx] == e);

	assert(e->__binheap_idx != BINHEAP_NOIDX);
	e->fd = -1;
	binheap_delete(evb->binheap, e->__binheap_idx);
	assert(e->__binheap_idx == BINHEAP_NOIDX);
	evb->lpfd--;

	if (e->sig > 0) {
		assert(e->sig < vev_nsig);
		es = &vev_sigs[e->sig];
		assert(es->vev == e);
		es->vev = NULL;
		es->vevb = NULL;
		es->sigact.sa_flags = e->sig_flags;
		es->sigact.sa_handler = SIG_DFL;
		AZ(sigaction(e->sig, &es->sigact, NULL));
		es->happened = 0;
	}

	e->magic = 0;
	e->__vevb = NULL;
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
	struct vev *e;
	int i, j, k, tmo;

	CHECK_OBJ_NOTNULL(evb, VEV_BASE_MAGIC);
	assert(evb->thread == pthread_self());
	assert(evb->lpfd < evb->npfd);

	if (evb->psig)
		return (vev_sched_signal(evb));

	e = binheap_root(evb->binheap);
	if (e != NULL) {
		CHECK_OBJ_NOTNULL(e, VEV_MAGIC);
		assert(e->__binheap_idx == BINHEAP_NOIDX + 1);
		t = VTIM_mono();
		if (e->__when <= t)
			return (vev_sched_timeout(evb, e, t));
		tmo = (int)((e->__when - t) * 1e3);
		if (tmo == 0)
			tmo = 1;
	} else
		tmo = INFTIM;

	if (tmo == INFTIM && evb->lpfd == BINHEAP_NOIDX + 1)
		return (0);

	i = poll(evb->pfd + 1, evb->lpfd - 1, tmo);
	if (i == -1 && errno == EINTR)
		return (vev_sched_signal(evb));

	if (i == 0) {
		assert(e != NULL);
		t = VTIM_mono();
		if (e->__when <= t)
			return (vev_sched_timeout(evb, e, t));
	}

	k = 0;
	for(j = 1; j < evb->lpfd; j++) {
		evb->pev[j]->fd_events = evb->pfd[j].revents;
		if (evb->pev[j]->fd_events)
			k++;
	}
	assert(k == i);

	DBG(evb, "EVENTS %d\n", i);
	while (i > 0) {
		for(j = BINHEAP_NOIDX + 1; j < evb->lpfd; j++) {
			e = evb->pev[j];
			if (e->fd_events == 0)
				continue;
			DBG(evb, "EVENT %p j=%d fd=%d ev=0x%x %d\n",
			    e, j, e->fd, e->fd_events, i);
			k = e->callback(e, e->fd_events);
			e->fd_events = 0;
			i--;
			if (k) {
				vev_del(evb, e);
				free(e);
			}
		}
	}
	AZ(i);
	return (1);
}
