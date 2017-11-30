/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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

#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>

#include "cache/cache_varnishd.h"

#include "waiter/waiter.h"
#include "waiter/waiter_priv.h"
#include "vtim.h"

struct vwp {
	unsigned		magic;
#define VWP_MAGIC		0x4b2cc735
	struct waiter		*waiter;

	int			pipes[2];

	pthread_t		thread;
	struct pollfd		*pollfd;
	struct waited		**idx;
	size_t			npoll;
	size_t			hpoll;
};

/*--------------------------------------------------------------------
 * It would make much more sense to not use two large vectors, but
 * the poll(2) API forces us to use at least one, so ... KISS.
 */

static void
vwp_extend_pollspace(struct vwp *vwp)
{
	size_t inc;

	if (vwp->npoll < (1<<12))
		inc = (1<<10);
	else if (vwp->npoll < (1<<14))
		inc = (1<<12);
	else if (vwp->npoll < (1<<16))
		inc = (1<<14);
	else
		inc = (1<<16);

	VSL(SLT_Debug, 0, "Acceptor poll space increased by %zu to %zu",
	    inc, vwp->npoll + inc);

	vwp->pollfd = realloc(vwp->pollfd,
	    (vwp->npoll + inc) * sizeof(*vwp->pollfd));
	AN(vwp->pollfd);
	memset(vwp->pollfd + vwp->npoll, 0, inc * sizeof(*vwp->pollfd));

	vwp->idx = realloc(vwp->idx, (vwp->npoll + inc) * sizeof(*vwp->idx));
	AN(vwp->idx);
	memset(vwp->idx + vwp->npoll, 0, inc * sizeof(*vwp->idx));

	for (; inc > 0; inc--)
		vwp->pollfd[vwp->npoll++].fd = -1;
}

/*--------------------------------------------------------------------*/

static void
vwp_add(struct vwp *vwp, struct waited *wp)
{

VSL(SLT_Debug, wp->fd, "ADD");
	CHECK_OBJ_NOTNULL(wp, WAITED_MAGIC);
	CHECK_OBJ_NOTNULL(vwp, VWP_MAGIC);
	if (vwp->hpoll == vwp->npoll)
		vwp_extend_pollspace(vwp);
	assert(vwp->hpoll < vwp->npoll);
	assert(vwp->pollfd[vwp->hpoll].fd == -1);
	AZ(vwp->idx[vwp->hpoll]);
	vwp->pollfd[vwp->hpoll].fd = wp->fd;
	vwp->pollfd[vwp->hpoll].events = POLLIN;
	vwp->idx[vwp->hpoll] = wp;
	vwp->hpoll++;
	Wait_HeapInsert(vwp->waiter, wp);
}

static void
vwp_del(struct vwp *vwp, int n)
{
	vwp->hpoll--;
	if (n != vwp->hpoll) {
		vwp->pollfd[n] = vwp->pollfd[vwp->hpoll];
		vwp->idx[n] = vwp->idx[vwp->hpoll];
	}
VSL(SLT_Debug, vwp->pollfd[vwp->hpoll].fd, "DEL");
	memset(&vwp->pollfd[vwp->hpoll], 0, sizeof(*vwp->pollfd));
	vwp->pollfd[vwp->hpoll].fd = -1;
	vwp->idx[vwp->hpoll] = NULL;
}

/*--------------------------------------------------------------------*/

static void
vwp_dopipe(struct vwp *vwp)
{
	struct waited *w[128];
	ssize_t ss;
	int i;

	ss = read(vwp->pipes[0], w, sizeof w);
	assert(ss > 0);
	i = 0;
	while (ss) {
		if (w[i] == NULL) {
			assert(ss == sizeof w[0]);
			assert(vwp->hpoll == 1);
			pthread_exit(NULL);
		}
		CHECK_OBJ_NOTNULL(w[i], WAITED_MAGIC);
		assert(w[i]->fd > 0);			// no stdin
		vwp_add(vwp, w[i++]);
		ss -= sizeof w[0];
	}
}

/*--------------------------------------------------------------------*/

static void *
vwp_main(void *priv)
{
	int v;
	struct vwp *vwp;
	struct waiter *w;
	struct waited *wp;
	double now, then;
	int i;

	THR_SetName("cache-poll");
	THR_Init();
	CAST_OBJ_NOTNULL(vwp, priv, VWP_MAGIC);
	w = vwp->waiter;

	while (1) {
		then = Wait_HeapDue(w, &wp);
		if (wp == NULL)
			i = -1;
		else
			i = (int)floor(1e3 * (then - VTIM_real()));
		assert(vwp->hpoll > 0);
		AN(vwp->pollfd);
		v = poll(vwp->pollfd, vwp->hpoll, i);
		assert(v >= 0);
		now = VTIM_real();
		if (vwp->pollfd[0].revents)
			v--;
		for (i = 1; i < vwp->hpoll;) {
			assert(vwp->pollfd[i].fd != vwp->pipes[0]);
			wp = vwp->idx[i];
			CHECK_OBJ_NOTNULL(wp, WAITED_MAGIC);

			if (v == 0 && Wait_HeapDue(w, NULL) > now)
				break;
			if (vwp->pollfd[i].revents)
				v--;
			then = Wait_When(wp);
			if (then <= now) {
				AN(Wait_HeapDelete(w, wp));
				Wait_Call(w, wp, WAITER_TIMEOUT, now);
				vwp_del(vwp, i);
			} else if (vwp->pollfd[i].revents & POLLIN) {
				assert(wp->fd > 0);
				assert(wp->fd == vwp->pollfd[i].fd);
				AN(Wait_HeapDelete(w, wp));
				Wait_Call(w, wp, WAITER_ACTION, now);
				vwp_del(vwp, i);
			} else {
				i++;
			}
		}
		if (vwp->pollfd[0].revents)
			vwp_dopipe(vwp);
	}
	NEEDLESS(return NULL);
}

/*--------------------------------------------------------------------*/

static int v_matchproto_(waiter_enter_f)
vwp_enter(void *priv, struct waited *wp)
{
	struct vwp *vwp;

	CAST_OBJ_NOTNULL(vwp, priv, VWP_MAGIC);

	if (write(vwp->pipes[1], &wp, sizeof wp) != sizeof wp)
		return (-1);
	return (0);
}

/*--------------------------------------------------------------------*/

static void v_matchproto_(waiter_init_f)
vwp_init(struct waiter *w)
{
	struct vwp *vwp;

	CHECK_OBJ_NOTNULL(w, WAITER_MAGIC);
	vwp = w->priv;
	INIT_OBJ(vwp, VWP_MAGIC);
	vwp->waiter = w;
	AZ(pipe(vwp->pipes));
	// XXX: set write pipe non-blocking

	vwp->hpoll = 1;
	vwp_extend_pollspace(vwp);
	vwp->pollfd[0].fd = vwp->pipes[0];
	vwp->pollfd[0].events = POLLIN;
	AZ(pthread_create(&vwp->thread, NULL, vwp_main, vwp));
}

/*--------------------------------------------------------------------
 * It is the callers responsibility to trigger all fd's waited on to
 * fail somehow.
 */

static void v_matchproto_(waiter_fini_f)
vwp_fini(struct waiter *w)
{
	struct vwp *vwp;
	void *vp;

	CAST_OBJ_NOTNULL(vwp, w->priv, VWP_MAGIC);
	vp = NULL;
	while (vwp->hpoll > 1)
		(void)usleep(100000);
	// XXX: set write pipe blocking
	assert(write(vwp->pipes[1], &vp, sizeof vp) == sizeof vp);
	AZ(pthread_join(vwp->thread, &vp));
	closefd(&vwp->pipes[0]);
	closefd(&vwp->pipes[1]);
	free(vwp->pollfd);
	free(vwp->idx);
}

/*--------------------------------------------------------------------*/

#include "waiter/mgt_waiter.h"

const struct waiter_impl waiter_poll = {
	.name =		"poll",
	.init =		vwp_init,
	.fini =		vwp_fini,
	.enter =	vwp_enter,
	.size =		sizeof(struct vwp),
};
