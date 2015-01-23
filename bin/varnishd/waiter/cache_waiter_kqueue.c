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
 * XXX: We need to pass sessions back into the event engine when they are
 * reused.  Not sure what the most efficient way is for that.  For now
 * write the session pointer to a pipe which the event engine monitors.
 */

#include "config.h"

#if defined(HAVE_KQUEUE)

#include <sys/types.h>
#include <sys/event.h>

#include <stdlib.h>
#include <unistd.h>

#include "cache/cache.h"

#include "waiter/waiter.h"
#include "waiter/waiter_priv.h"
#include "vtim.h"

#define NKEV	100

struct vwk {
	unsigned		magic;
#define VWK_MAGIC		0x1cc2acc2
	struct waiter		*waiter;

	pthread_t		thread;
	int			kq;
	struct kevent		ki[NKEV];
	unsigned		nki;
};

/*--------------------------------------------------------------------*/

static void
vwk_kq_flush(struct vwk *vwk)
{
	int i;

	if (vwk->nki == 0)
		return;
	i = kevent(vwk->kq, vwk->ki, vwk->nki, NULL, 0, NULL);
	AZ(i);
	vwk->nki = 0;
}

static void
vwk_kq_sess(struct vwk *vwk, struct waited *sp, short arm)
{

	CHECK_OBJ_NOTNULL(sp, WAITED_MAGIC);
	assert(sp->fd >= 0);
	EV_SET(&vwk->ki[vwk->nki], sp->fd, EVFILT_READ, arm, 0, 0, sp);
	if (++vwk->nki == NKEV)
		vwk_kq_flush(vwk);
}

/*--------------------------------------------------------------------*/

static void
vwk_inject(const struct waiter *w, struct waited *wp)
{
	struct vwk *vwk;

	CAST_OBJ_NOTNULL(vwk, w->priv, VWK_MAGIC);
	if (wp == w->pipe_w)
		vwk_kq_sess(vwk, wp, EV_ADD);
	else
		vwk_kq_sess(vwk, wp, EV_ADD | EV_ONESHOT);
}

#if 0
static void
vwk_evict(const struct waiter *w, struct waited *wp)
{
	struct vwk *vwk;

	CAST_OBJ_NOTNULL(vwk, w->priv, VWK_MAGIC);
	vwk_kq_sess(vwk, wp, EV_DELETE);
}
#endif

/*--------------------------------------------------------------------*/

static void
vwk_sess_ev(const struct vwk *vwk, const struct kevent *kp, double now)
{
	struct waited *sp;

	AN(kp->udata);
	CAST_OBJ_NOTNULL(sp, kp->udata, WAITED_MAGIC);

	if (kp->data > 0) {
		Wait_Handle(vwk->waiter, sp, WAITER_ACTION, now);
		return;
	} else if (kp->flags & EV_EOF) {
		Wait_Handle(vwk->waiter, sp, WAITER_REMCLOSE, now);
		return;
	} else {
		WRONG("unknown kqueue state");
	}
}

/*--------------------------------------------------------------------*/

static void *
vwk_thread(void *priv)
{
	struct vwk *vwk;
	struct kevent ke[NKEV], *kp;
	int j, n;
	double now;

	CAST_OBJ_NOTNULL(vwk, priv, VWK_MAGIC);
	THR_SetName("cache-kqueue");

	vwk_kq_flush(vwk);

	vwk->nki = 0;
	while (!vwk->waiter->dismantle) {
		n = kevent(vwk->kq, vwk->ki, vwk->nki, ke, NKEV, NULL);
		assert(n <= NKEV);
		if (n == 0) {
			/* This happens on OSX in m00011.vtc */
			(void)usleep(10000);
		}
		vwk->nki = 0;
		now = VTIM_real();
		for (kp = ke, j = 0; j < n; j++, kp++) {
			assert(kp->filter == EVFILT_READ);
			vwk_sess_ev(vwk, kp, now);
		}
	}
	NEEDLESS_RETURN(NULL);
}

/*--------------------------------------------------------------------*/

static void __match_proto__(waiter_init_f)
vwk_init(struct waiter *w)
{
	struct vwk *vwk;

	CHECK_OBJ_NOTNULL(w, WAITER_MAGIC);
	vwk = w->priv;
	INIT_OBJ(vwk, VWK_MAGIC);
	vwk->waiter = w;

	vwk->kq = kqueue();
	assert(vwk->kq >= 0);

	Wait_UsePipe(w);

	AZ(pthread_create(&vwk->thread, NULL, vwk_thread, vwk));
}

/*--------------------------------------------------------------------*/

static void __match_proto__(waiter_fini_f)
vwk_fini(struct waiter *w)
{
	struct vwk *vwk;
	void *vp;

	CAST_OBJ_NOTNULL(vwk, w->priv, VWK_MAGIC);
	AZ(pthread_join(vwk->thread, &vp));
	AZ(close(vwk->kq));
}

/*--------------------------------------------------------------------*/

const struct waiter_impl waiter_kqueue = {
	.name =		"kqueue",
	.init =		vwk_init,
	.fini =		vwk_fini,
	.inject =	vwk_inject,
	// .evict =	vwk_evict,
	.size =		sizeof(struct vwk),
};

#endif /* defined(HAVE_KQUEUE) */
