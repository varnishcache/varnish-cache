/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006 Varnish Software AS
 * Copyright (c) 2007 OmniTI Computer Consulting, Inc.
 * Copyright (c) 2007 Theo Schlossnagle
 * Copyright (c) 2010-2016 UPLEX, Nils Goroll
 * All rights reserved.
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
 * On concurrency:
 *
 * There are several options for the enter method to add an fd for the waiter
 * thread to look after:
 *
 * - share the binheap (requiring a mutex) - implemented for epoll and kqueues
 *
 * - send events to be entered through the events interface and keep the binheap
 *   private to the waiter thread - implemented here.
 *
 * - some other message passing / mailbox
 *
 * It has not yet been determined which option is best. In the best case, by
 * sharing the binheap, we can save two port syscalls - but not always:
 *
 * - if the waited event has a timeout earlier than the first element on the
 *   binheap, we need to kick the waiter thread anyway
 *
 * - if the waiter thread is busy, it will get the passed waited event together
 *   with other events
 *
 * on the other end we need to sync on the mtx to protect the binheap.  Solaris
 * uses userland adaptive mutexes: if the thread holding the lock is running,
 * spinlock, otherwise syscall.
 *
 * and the critical section for the mtx is basically "whenever not blocking in
 * port_getn", which does not sound too good with respect to scalability.
 *
 * At any rate, we could save even more syscalls by increasing nevents
 * (port_getn returns when nevents exist or the timeout is reached). This would
 * increase our latency reacting on POLLIN events.
 *
 */

#include "config.h"

#if defined(HAVE_PORT_CREATE)

#include <sys/time.h>

#include <errno.h>
#include <port.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache/cache.h"

#include "waiter/waiter_priv.h"
#include "waiter/mgt_waiter.h"
#include "vtim.h"

// XXX replace with process.max-port-events bound to a sensible maximum
#define MAX_EVENTS 256

struct vws {
	unsigned		magic;
#define VWS_MAGIC		0x0b771473
	struct waiter		*waiter;
	pthread_t		thread;
	double			next;
	int			dport;
	unsigned		nwaited;
	int			die;
};

static inline void
vws_add(struct vws *vws, int fd, void *data)
{
	// POLLIN should be all we need here
	AZ(port_associate(vws->dport, PORT_SOURCE_FD, fd, POLLIN, data));
}

static inline void
vws_del(struct vws *vws, int fd)
{
	port_dissociate(vws->dport, PORT_SOURCE_FD, fd);
}

static inline void
vws_port_ev(struct vws *vws, struct waiter *w, port_event_t *ev, double now) {
	struct waited *wp;
	if(ev->portev_source == PORT_SOURCE_USER) {
		CAST_OBJ_NOTNULL(wp, ev->portev_user, WAITED_MAGIC);
		assert(wp->fd >= 0);
		vws->nwaited++;
		Wait_HeapInsert(vws->waiter, wp);
		vws_add(vws, wp->fd, wp);
	} else {
		assert(ev->portev_source == PORT_SOURCE_FD);
		CAST_OBJ_NOTNULL(wp, ev->portev_user, WAITED_MAGIC);
		assert(wp->fd >= 0);
		vws->nwaited--;
		/*
		 * port_getn does not implicitly disassociate
		 *
		 * Ref: http://opensolaris.org/jive/thread.jspa?\
		 *          threadID=129476&tstart=0
		 */
		vws_del(vws, wp->fd);
		AN(Wait_HeapDelete(w, wp));
		Wait_Call(w, wp, ev->portev_events & POLLERR ?
		    WAITER_REMCLOSE : WAITER_ACTION,
		    now);
	}
}

static void *
vws_thread(void *priv)
{
	struct waited *wp;
	struct waiter *w;
	struct vws *vws;
	double now, then;
	struct timespec ts;
	const double	max_t  = 100.0;
	port_event_t ev[MAX_EVENTS];
	u_int nevents;
	int ei, ret;

	CAST_OBJ_NOTNULL(vws, priv, VWS_MAGIC);
	w = vws->waiter;
	CHECK_OBJ_NOTNULL(w, WAITER_MAGIC);
	THR_SetName("cache-ports");
	THR_Init();

	now = VTIM_real();

	while (!vws->die) {
		while (1) {
			then = Wait_HeapDue(w, &wp);
			if (wp == NULL) {
				vws->next = now + max_t;
				break;
			} else if (then > now) {
				vws->next = then;
				break;
			}
			CHECK_OBJ_NOTNULL(wp, WAITED_MAGIC);
			vws_del(vws, wp->fd);
			AN(Wait_HeapDelete(w, wp));
			Wait_Call(w, wp, WAITER_TIMEOUT, now);
		}
		then = vws->next - now;
		ts.tv_sec = (time_t)floor(then);
		ts.tv_nsec = (long)(1e9 * (then - ts.tv_sec));

		/*
		 * min number of events we accept.  could consider to scale up
		 * for efficiency, but as we always get all waiting events up to
		 * the maximum, we'd only optimize the idle case sacrificing
		 * some latency
		 */
		nevents = 1;

		/*
		 * see disucssion in
		 * - https://issues.apache.org/bugzilla/show_bug.cgi?id=47645
		 * - http://mail.opensolaris.org/pipermail/\
		 *       networking-discuss/2009-August/011979.html
		 *
		 * comment from apr/poll/unix/port.c :
		 *
		 * This confusing API can return an event at the same time
		 * that it reports EINTR or ETIME.
		 *
		 */

		ret = port_getn(vws->dport, ev, MAX_EVENTS, &nevents, &ts);
		now = VTIM_real();

		if (ret < 0 && errno == EBADF) {
			/* close on dport is our stop signal */
			AN(vws->die);
			break;
		}

		if (ret < 0)
			assert((errno == EINTR) || (errno == ETIME));

		for (ei = 0; ei < nevents; ei++)
			vws_port_ev(vws, w, &ev[ei], now);
	}
	return NULL;
}

/*--------------------------------------------------------------------*/

static int
vws_enter(void *priv, struct waited *wp)
{
	int r;
	struct vws *vws;

	CAST_OBJ_NOTNULL(vws, priv, VWS_MAGIC);
	r = port_send(vws->dport, 0, TRUST_ME(wp));
	if (r == -1 && errno == EAGAIN)
		return (-1);
	AZ(r);
	return (0);
}

/*--------------------------------------------------------------------*/

static void __match_proto__(waiter_init_f)
vws_init(struct waiter *w)
{
	struct vws *vws;

	CHECK_OBJ_NOTNULL(w, WAITER_MAGIC);
	vws = w->priv;
	INIT_OBJ(vws, VWS_MAGIC);
	vws->waiter = w;
	vws->dport = port_create();
	assert(vws->dport >= 0);

	AZ(pthread_create(&vws->thread, NULL, vws_thread, vws));
}

/*--------------------------------------------------------------------*/

static void __match_proto__(waiter_fini_f)
vws_fini(struct waiter *w)
{
	struct vws *vws;
	void *vp;

	CAST_OBJ_NOTNULL(vws, w->priv, VWS_MAGIC);
	vws->die = 1;
	AZ(close(vws->dport));
	AZ(pthread_join(vws->thread, &vp));
}

/*--------------------------------------------------------------------*/

const struct waiter_impl waiter_ports = {
	.name =		"ports",
	.init =		vws_init,
	.fini =		vws_fini,
	.enter =	vws_enter,
	.size =		sizeof(struct vws),
};

#endif /* defined(HAVE_PORT_CREATE) */
