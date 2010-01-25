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
 *
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>

#include "shmlog.h"
#include "cache.h"
#include "cache_waiter.h"

#define NEEV	128

static pthread_t vca_poll_thread;
static struct pollfd *pollfd;
static unsigned npoll, hpoll;

static VTAILQ_HEAD(,sess) sesshead = VTAILQ_HEAD_INITIALIZER(sesshead);

/*--------------------------------------------------------------------*/

static void
vca_pollspace(unsigned fd)
{
	struct pollfd *newpollfd = pollfd;
	unsigned newnpoll;

	if (fd < npoll)
		return;
	newnpoll = npoll;
	if (newnpoll == 0)
		newnpoll = 1;
	while (fd >= newnpoll)
		newnpoll = newnpoll * 2;
	VSL(SLT_Debug, 0, "Acceptor poll space increased to %u", newnpoll);
	newpollfd = realloc(newpollfd, newnpoll * sizeof *newpollfd);
	XXXAN(newpollfd);
	memset(newpollfd + npoll, 0, (newnpoll - npoll) * sizeof *newpollfd);
	pollfd = newpollfd;
	while (npoll < newnpoll)
		pollfd[npoll++].fd = -1;
	assert(fd < npoll);
}

/*--------------------------------------------------------------------*/

static void
vca_poll(int fd)
{

	assert(fd >= 0);
	vca_pollspace((unsigned)fd);
	assert(fd < npoll);
	if (hpoll < fd)
		hpoll = fd;
	pollfd[fd].fd = fd;
	pollfd[fd].events = POLLIN;
}

static void
vca_unpoll(int fd)
{

	assert(fd < npoll);
	assert(fd >= 0);
	vca_pollspace((unsigned)fd);
	pollfd[fd].fd = -1;
	pollfd[fd].events = 0;
}

/*--------------------------------------------------------------------*/

static void *
vca_main(void *arg)
{
	int v;
	struct sess *ss[NEEV], *sp, *sp2;
	double deadline;
	int i, j, fd;

	THR_SetName("cache-poll");
	(void)arg;

	vca_poll(vca_pipes[0]);

	while (1) {
		assert(hpoll < npoll);
		while (hpoll > 0 && pollfd[hpoll].fd == -1)
			hpoll--;
		assert(vca_pipes[0] <= hpoll);
		assert(pollfd[vca_pipes[0]].fd = vca_pipes[0]);
		assert(pollfd[vca_pipes[1]].fd = -1);
		v = poll(pollfd, hpoll + 1, 100);
		assert(v >= 0);
		if (v && pollfd[vca_pipes[0]].revents) {
			
			if (pollfd[vca_pipes[0]].revents != POLLIN)
				VSL(SLT_Debug, 0, "pipe.revents= 0x%x",
				    pollfd[vca_pipes[0]].revents);
			assert(pollfd[vca_pipes[0]].revents == POLLIN);
			v--;
			i = read(vca_pipes[0], ss, sizeof ss);
			assert(i >= 0);
			assert(((unsigned)i % sizeof ss[0]) == 0);
			for (j = 0; j * sizeof ss[0] < i; j++) {
				CHECK_OBJ_NOTNULL(ss[j], SESS_MAGIC);
				assert(ss[j]->fd >= 0);
				VTAILQ_INSERT_TAIL(&sesshead, ss[j], list);
				vca_poll(ss[j]->fd);
			}
		}
		deadline = TIM_real() - params->sess_timeout;
		VTAILQ_FOREACH_SAFE(sp, &sesshead, list, sp2) {
			if (v == 0)
				break;
			CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
			fd = sp->fd;
			assert(pollfd[fd].fd == fd);
			if (pollfd[fd].revents) {
				v--;
				i = HTC_Rx(sp->htc);
				VTAILQ_REMOVE(&sesshead, sp, list);
				if (i == 0) {
					/* Mov to front of list for speed */
					VTAILQ_INSERT_HEAD(&sesshead, sp, list);
				} else {
					vca_unpoll(fd);
					vca_handover(sp, i);
				}
			} else if (sp->t_open <= deadline) {
				VTAILQ_REMOVE(&sesshead, sp, list);
				vca_unpoll(fd);
				TCP_linger(sp->fd, 0);
				vca_close_session(sp, "timeout");
				SES_Delete(sp);
			}
		}
	}
	NEEDLESS_RETURN(NULL);
}

/*--------------------------------------------------------------------*/

static void
vca_poll_init(void)
{

	vca_pollspace(256);
	AZ(pthread_create(&vca_poll_thread, NULL, vca_main, NULL));
}

struct waiter waiter_poll = {
	.name =		"poll",
	.init =		vca_poll_init,
};
