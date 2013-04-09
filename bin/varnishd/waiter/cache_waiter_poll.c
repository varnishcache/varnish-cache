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
#include "vtim.h"
#include "vfil.h"

#define NEEV	128

struct vwp {
	unsigned		magic;
#define VWP_MAGIC		0x4b2cc735
	int			pipes[2];
	pthread_t		poll_thread;
	struct pollfd		*pollfd;
	unsigned		npoll;
	unsigned		hpoll;

	VTAILQ_HEAD(,sess)	sesshead;
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
	assert(vwp->pollfd[fd].events == 0);
	assert(vwp->pollfd[fd].revents == 0);

	vwp->pollfd[fd].fd = fd;
	vwp->pollfd[fd].events = POLLIN;
}

static void
vwp_unpoll(struct vwp *vwp, int fd)
{

	assert(fd >= 0);
	assert(fd < vwp->npoll);
	vwp_pollspace(vwp, (unsigned)fd);

	assert(vwp->pollfd[fd].fd == fd);
	assert(vwp->pollfd[fd].events == POLLIN);
	assert(vwp->pollfd[fd].revents == 0);

	vwp->pollfd[fd].fd = -1;
	vwp->pollfd[fd].events = 0;
}

/*--------------------------------------------------------------------*/

static void *
vwp_main(void *priv)
{
	int v, v2;
	struct vwp *vwp;
	struct sess *ss[NEEV], *sp, *sp2;
	double now, deadline;
	int i, j, fd;

	CAST_OBJ_NOTNULL(vwp, priv, VWP_MAGIC);
	THR_SetName("cache-poll");

	vwp_poll(vwp, vwp->pipes[0]);

	while (1) {
		assert(vwp->hpoll < vwp->npoll);
		while (vwp->hpoll > 0 && vwp->pollfd[vwp->hpoll].fd == -1)
			vwp->hpoll--;
		assert(vwp->pipes[0] <= vwp->hpoll);
		assert(vwp->pollfd[vwp->pipes[0]].fd == vwp->pipes[0]);
		assert(vwp->pollfd[vwp->pipes[1]].fd == -1);
		v = poll(vwp->pollfd, vwp->hpoll + 1, 100);
		assert(v >= 0);
		now = VTIM_real();
		deadline = now - cache_param->timeout_idle;
		v2 = v;
		VTAILQ_FOREACH_SAFE(sp, &vwp->sesshead, list, sp2) {
			if (v != 0 && v2 == 0)
				break;
			CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
			fd = sp->fd;
			assert(fd >= 0);
			assert(fd <= vwp->hpoll);
			assert(fd < vwp->npoll);
			assert(vwp->pollfd[fd].fd == fd);
			if (vwp->pollfd[fd].revents) {
				v2--;
				vwp->pollfd[fd].revents = 0;
				VTAILQ_REMOVE(&vwp->sesshead, sp, list);
				vwp_unpoll(vwp, fd);
				SES_Handle(sp, now);
			} else if (sp->t_idle <= deadline) {
				VTAILQ_REMOVE(&vwp->sesshead, sp, list);
				vwp_unpoll(vwp, fd);
				// XXX: not yet (void)VTCP_linger(sp->fd, 0);
				SES_Delete(sp, SC_RX_TIMEOUT, now);
			}
		}
		if (v2 && vwp->pollfd[vwp->pipes[0]].revents) {

			if (vwp->pollfd[vwp->pipes[0]].revents != POLLIN)
				VSL(SLT_Debug, 0, "pipe.revents= 0x%x",
				    vwp->pollfd[vwp->pipes[0]].revents);
			assert(vwp->pollfd[vwp->pipes[0]].revents == POLLIN);
			vwp->pollfd[vwp->pipes[0]].revents = 0;
			v2--;
			i = read(vwp->pipes[0], ss, sizeof ss);
			assert(i >= 0);
			assert(((unsigned)i % sizeof ss[0]) == 0);
			for (j = 0; j * sizeof ss[0] < i; j++) {
				CHECK_OBJ_NOTNULL(ss[j], SESS_MAGIC);
				assert(ss[j]->fd >= 0);
				VTAILQ_INSERT_TAIL(&vwp->sesshead, ss[j], list);
				vwp_poll(vwp, ss[j]->fd);
			}
		}
		assert(v2 == 0);
	}
	NEEDLESS_RETURN(NULL);
}

/*--------------------------------------------------------------------*/

static void
vwp_poll_pass(void *priv, struct sess *sp)
{
	struct vwp *vwp;

	CAST_OBJ_NOTNULL(vwp, priv, VWP_MAGIC);

	WAIT_Write_Session(sp, vwp->pipes[1]);
}

/*--------------------------------------------------------------------*/

static void *
vwp_poll_init(void)
{
	struct vwp *vwp;

	ALLOC_OBJ(vwp, VWP_MAGIC);
	AN(vwp);
	VTAILQ_INIT(&vwp->sesshead);
	AZ(pipe(vwp->pipes));

	AZ(VFIL_nonblocking(vwp->pipes[1]));

	vwp_pollspace(vwp, 256);
	AZ(pthread_create(&vwp->poll_thread, NULL, vwp_main, vwp));
	return (vwp);
}

/*--------------------------------------------------------------------*/

const struct waiter waiter_poll = {
	.name =		"poll",
	.init =		vwp_poll_init,
	.pass =		vwp_poll_pass,
};
