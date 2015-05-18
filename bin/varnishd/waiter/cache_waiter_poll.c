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

#include "cache/cache.h"

#include "waiter/waiter.h"
#include "waiter/waiter_priv.h"
#include "vtim.h"

struct vwp {
	unsigned		magic;
#define VWP_MAGIC		0x4b2cc735
	struct waiter		*waiter;

	pthread_t		thread;
	struct pollfd		*pollfd;
	unsigned		npoll;
	unsigned		hpoll;
};

/*--------------------------------------------------------------------*/

static void
vwp_pollspace(struct vwp *vwp, unsigned fd)
{
	struct pollfd *newpollfd = vwp->pollfd;
	unsigned newnpoll;

	if (fd < vwp->npoll)
		return;
	newnpoll = vwp->npoll;
	if (newnpoll == 0)
		newnpoll = 1;
	while (fd >= newnpoll)
		newnpoll = newnpoll * 2;
	VSL(SLT_Debug, 0, "Acceptor poll space increased to %u", newnpoll);
	newpollfd = realloc(newpollfd, newnpoll * sizeof *newpollfd);
	XXXAN(newpollfd);
	memset(newpollfd + vwp->npoll, 0,
	    (newnpoll - vwp->npoll) * sizeof *newpollfd);
	vwp->pollfd = newpollfd;
	while (vwp->npoll < newnpoll)
		vwp->pollfd[vwp->npoll++].fd = -1;
	assert(fd < vwp->npoll);
}

/*--------------------------------------------------------------------*/

static void __match_proto__(waiter_inject_f)
vwp_inject(const struct waiter *w, struct waited *wp)
{
	struct vwp *vwp;
	int fd;

	CAST_OBJ_NOTNULL(vwp, w->priv, VWP_MAGIC);
	CHECK_OBJ_NOTNULL(wp, WAITED_MAGIC);
	fd = wp->fd;
VSL(SLT_Debug, 0, "POLL Inject %d", fd);
	assert(fd >= 0);
	vwp_pollspace(vwp, (unsigned)fd);
	assert(fd < vwp->npoll);
	if (vwp->hpoll < fd)
		vwp->hpoll = fd;

	assert(vwp->pollfd[fd].fd == -1);
	AZ(vwp->pollfd[fd].events);
	AZ(vwp->pollfd[fd].revents);

	vwp->pollfd[fd].fd = fd;
	vwp->pollfd[fd].events = POLLIN;
}

static void __match_proto__(waiter_evict_f)
vwp_evict(const struct waiter *w, struct waited *wp)
{
	struct vwp *vwp;
	int fd;

	CAST_OBJ_NOTNULL(vwp, w->priv, VWP_MAGIC);
	CHECK_OBJ_NOTNULL(wp, WAITED_MAGIC);
	fd = wp->fd;
VSL(SLT_Debug, 0, "POLL Evict %d", fd);
	assert(fd >= 0);
	assert(fd < vwp->npoll);
	vwp_pollspace(vwp, (unsigned)fd);

	vwp->pollfd[fd].fd = -1;
	vwp->pollfd[fd].events = 0;
}

/*--------------------------------------------------------------------*/

static void *
vwp_main(void *priv)
{
	int v, v2;
	struct vwp *vwp;
	struct waited *sp, *sp2;
	double now, idle;
	int fd;

	CAST_OBJ_NOTNULL(vwp, priv, VWP_MAGIC);
	THR_SetName("cache-poll");

	while (!vwp->waiter->dismantle) {
		assert(vwp->hpoll < vwp->npoll);
		while (vwp->hpoll > 0 && vwp->pollfd[vwp->hpoll].fd == -1)
			vwp->hpoll--;
		v = poll(vwp->pollfd, vwp->hpoll + 1, -1);
		assert(v >= 0);
		v2 = v;
		now = VTIM_real();
		idle = now - *vwp->waiter->tmo;
		VTAILQ_FOREACH_SAFE(sp, &vwp->waiter->waithead, list, sp2) {
			if (v != 0 && v2 == 0)
				break;
			CHECK_OBJ_NOTNULL(sp, WAITED_MAGIC);
			fd = sp->fd;
			VSL(SLT_Debug, 0,
			    "POLL Handle %d %x", fd, vwp->pollfd[fd].revents);
			assert(fd >= 0);
			assert(fd <= vwp->hpoll);
			assert(fd < vwp->npoll);
			assert(vwp->pollfd[fd].fd == fd);
			if (vwp->pollfd[fd].revents) {
				v2--;
				vwp->pollfd[fd].revents = 0;
				Wait_Handle(vwp->waiter, sp, WAITER_ACTION,
				    now);
			} else if (sp->idle <= idle) {
				Wait_Handle(vwp->waiter, sp, WAITER_TIMEOUT,
				    now);
			}
		}
		Wait_Handle(vwp->waiter, NULL, WAITER_ACTION, now);
	}
	NEEDLESS_RETURN(NULL);
}

/*--------------------------------------------------------------------*/

static void __match_proto__(waiter_init_f)
vwp_init(struct waiter *w)
{
	struct vwp *vwp;

	CHECK_OBJ_NOTNULL(w, WAITER_MAGIC);
	vwp = w->priv;
	INIT_OBJ(vwp, VWP_MAGIC);
	vwp->waiter = w;

	vwp_pollspace(vwp, 256);
	Wait_UsePipe(w);
	AZ(pthread_create(&vwp->thread, NULL, vwp_main, vwp));
}

/*--------------------------------------------------------------------*/

static void __match_proto__(waiter_fini_f)
vwp_fini(struct waiter *w)
{
	struct vwp *vwp;
	void *vp;

	CAST_OBJ_NOTNULL(vwp, w->priv, VWP_MAGIC);
	AZ(pthread_join(vwp->thread, &vp));
	free(vwp->pollfd);
}

/*--------------------------------------------------------------------*/

const struct waiter_impl waiter_poll = {
	.name =		"poll",
	.init =		vwp_init,
	.fini =		vwp_fini,
	.inject =	vwp_inject,
	.evict =	vwp_evict,
	.size =		sizeof(struct vwp),
};
