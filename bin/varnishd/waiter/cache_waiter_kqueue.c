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

#if defined(HAVE_KQUEUE)

#include <sys/types.h>
#include <sys/event.h>

#include <errno.h>
#include <stdlib.h>

#include "cache/cache.h"

#include "waiter/waiter.h"
#include "waiter/waiter_priv.h"
#include "vtim.h"

#define NKEV	256

struct vwk {
	unsigned		magic;
#define VWK_MAGIC		0x1cc2acc2
	int			kq;
	struct waiter		*waiter;
	pthread_t		thread;

	VTAILQ_HEAD(,waited)	list;
	struct lock		mtx;
};

/*--------------------------------------------------------------------*/

static void *
vwk_thread(void *priv)
{
	struct vwk *vwk;
	struct kevent ke[NKEV], *kp;
	int j, n;
	double now, idle, last_idle;
	struct timespec ts;
	struct waited *wp, *wp2;

	CAST_OBJ_NOTNULL(vwk, priv, VWK_MAGIC);
	THR_SetName("cache-kqueue");

	last_idle = 0.0;
	while (1) {
		now = .3 * *vwk->waiter->tmo;
		ts.tv_sec = (time_t)floor(now);
		ts.tv_nsec = (long)(1e9 * (now - ts.tv_sec));
		n = kevent(vwk->kq, NULL, 0, ke, NKEV, &ts);
		if (n < 0 && errno == EBADF)
			break;
		assert(n <= NKEV);
		now = VTIM_real();
		idle = now - *vwk->waiter->tmo;
		for (kp = ke, j = 0; j < n; j++, kp++) {
			assert(kp->filter == EVFILT_READ);
			CAST_OBJ_NOTNULL(wp, ke[j].udata, WAITED_MAGIC);
			Lck_Lock(&vwk->mtx);
			VTAILQ_REMOVE(&vwk->list, wp, list);
			Lck_Unlock(&vwk->mtx);
			if (wp->idle <= idle)
				vwk->waiter->func(wp, WAITER_TIMEOUT, now);
			else if (kp->flags & EV_EOF)
				vwk->waiter->func(wp, WAITER_REMCLOSE, now);
			else
				vwk->waiter->func(wp, WAITER_ACTION, now);
		}
		if (now - last_idle > .3 * *vwk->waiter->tmo) {
			last_idle = now;
			n = 0;
			Lck_Lock(&vwk->mtx);
			VTAILQ_FOREACH_SAFE(wp, &vwk->list, list, wp2) {
				if (wp->idle > idle)
					continue;
				EV_SET(ke + n, wp->fd,
				    EVFILT_READ, EV_DELETE, 0, 0, wp);
				if (++n == NKEV)
					break;
			}
			if (n > 0)
				AZ(kevent(vwk->kq, ke, n, NULL, 0, NULL));
			for (j = 0; j < n; j++) {
				CAST_OBJ_NOTNULL(wp, ke[j].udata, WAITED_MAGIC);
				VTAILQ_REMOVE(&vwk->list, wp, list);
				vwk->waiter->func(wp, WAITER_TIMEOUT, now);
			}
			Lck_Unlock(&vwk->mtx);
		}
	}
	return(NULL);
}

/*--------------------------------------------------------------------*/

static int __match_proto__(waiter_enter_f)
vwk_enter(void *priv, struct waited *wp)
{
	struct vwk *vwk;
	struct kevent ke;

	CAST_OBJ_NOTNULL(vwk, priv, VWK_MAGIC);
	EV_SET(&ke, wp->fd, EVFILT_READ, EV_ADD|EV_ONESHOT, 0, 0, wp);
	Lck_Lock(&vwk->mtx);
	VTAILQ_INSERT_TAIL(&vwk->list, wp, list);
	AZ(kevent(vwk->kq, &ke, 1, NULL, 0, NULL));
	Lck_Unlock(&vwk->mtx);
	return(0);
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
	VTAILQ_INIT(&vwk->list);
	Lck_New(&vwk->mtx, lck_misc);

	AZ(pthread_create(&vwk->thread, NULL, vwk_thread, vwk));
}

/*--------------------------------------------------------------------
 * It is the callers responsibility to trigger all fd's waited on to
 * fail somehow.
 */

static void __match_proto__(waiter_fini_f)
vwk_fini(struct waiter *w)
{
	struct vwk *vwk;
	void *vp;

	CAST_OBJ_NOTNULL(vwk, w->priv, VWK_MAGIC);
	Lck_Lock(&vwk->mtx);
	while (!VTAILQ_EMPTY(&vwk->list)) {
		Lck_Unlock(&vwk->mtx);
		(void)usleep(100000);
		Lck_Lock(&vwk->mtx);
	}
	AZ(close(vwk->kq));
	vwk->kq = -1;
	Lck_Unlock(&vwk->mtx);
	AZ(pthread_join(vwk->thread, &vp));
	Lck_Delete(&vwk->mtx);
}

/*--------------------------------------------------------------------*/

const struct waiter_impl waiter_kqueue = {
	.name =		"kqueue",
	.init =		vwk_init,
	.fini =		vwk_fini,
	.enter =	vwk_enter,
	.size =		sizeof(struct vwk),
};

#endif /* defined(HAVE_KQUEUE) */
