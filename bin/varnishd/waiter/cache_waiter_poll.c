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

	pthread_t		poll_thread;
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

static void
vwp_poll(struct vwp *vwp, int fd)
{

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

static void __match_proto__(waiter_inject_f)
vwp_inject(const struct waiter *w, struct waited *wp)
{
	struct vwp *vwp;

	CAST_OBJ_NOTNULL(vwp, w->priv, VWP_MAGIC);
	vwp_poll(vwp, wp->fd);
}

static void
vwp_unpoll(struct vwp *vwp, int fd)
{

	assert(fd >= 0);
	assert(fd < vwp->npoll);
	vwp_pollspace(vwp, (unsigned)fd);

	assert(vwp->pollfd[fd].fd == fd);
	assert(vwp->pollfd[fd].events == POLLIN);
	AZ(vwp->pollfd[fd].revents);

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
	double now, deadline;
	int fd;

	CAST_OBJ_NOTNULL(vwp, priv, VWP_MAGIC);
	THR_SetName("cache-poll");

	while (1) {
		assert(vwp->hpoll < vwp->npoll);
		while (vwp->hpoll > 0 && vwp->pollfd[vwp->hpoll].fd == -1)
			vwp->hpoll--;
		v = poll(vwp->pollfd, vwp->hpoll + 1, 100);
		assert(v >= 0);
		v2 = v;
		now = VTIM_real();
		deadline = now - *vwp->waiter->tmo;
		VTAILQ_FOREACH_SAFE(sp, &vwp->waiter->sesshead, list, sp2) {
			if (v != 0 && v2 == 0)
				break;
			CHECK_OBJ_NOTNULL(sp, WAITED_MAGIC);
			fd = sp->fd;
			assert(fd >= 0);
			assert(fd <= vwp->hpoll);
			assert(fd < vwp->npoll);
			assert(vwp->pollfd[fd].fd == fd);
			if (vwp->pollfd[fd].revents) {
				v2--;
				vwp->pollfd[fd].revents = 0;
				if (sp != vwp->waiter->pipe_w)
					vwp_unpoll(vwp, fd);
				WAIT_handle(vwp->waiter, sp, WAITER_ACTION,
				    now);
			} else if (sp->deadline <= deadline) {
				vwp_unpoll(vwp, fd);
				WAIT_handle(vwp->waiter, sp, WAITER_TIMEOUT,
				    now);
			}
		}
	}
	NEEDLESS_RETURN(NULL);
}

/*--------------------------------------------------------------------*/

static void __match_proto__(waiter_init_f)
vwp_poll_init(struct waiter *w)
{
	struct vwp *vwp;

	CHECK_OBJ_NOTNULL(w, WAITER_MAGIC);
	vwp = w->priv;
	INIT_OBJ(vwp, VWP_MAGIC);
	vwp->waiter = w;

	vwp_pollspace(vwp, 256);
	WAIT_UsePipe(w);
	AZ(pthread_create(&vwp->poll_thread, NULL, vwp_main, vwp));
}

/*--------------------------------------------------------------------*/

const struct waiter_impl waiter_poll = {
	.name =		"poll",
	.init =		vwp_poll_init,
	.inject =	vwp_inject,
	.size =		sizeof(struct vwp),
};
