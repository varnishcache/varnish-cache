/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2007 Linpro AS
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
 * $Id$
 *
 */

#if defined(HAVE_POLL)

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>

#include "heritage.h"
#include "shmlog.h"
#include "cache.h"
#include "cache_acceptor.h"

static pthread_t vca_poll_thread;
static struct pollfd *pollfd;
static unsigned npoll;

static VTAILQ_HEAD(,sess) sesshead = VTAILQ_HEAD_INITIALIZER(sesshead);

/*--------------------------------------------------------------------*/

static void
vca_pollspace(unsigned fd)
{
	struct pollfd *p;
	unsigned u, v;

	if (fd < npoll)
		return;
	if (npoll == 0)
		npoll = 16;
	for (u = npoll; fd >= u; )
		u += u;
	VSL(SLT_Debug, 0, "Acceptor Pollspace %u", u);
	p = realloc(pollfd, u * sizeof *p);
	XXXAN(p);	/* close offending fd */
	memset(p + npoll, 0, (u - npoll) * sizeof *p);
	for (v = npoll ; v <= u; v++)
		p->fd = -1;
	pollfd = p;
	npoll = u;
}

/*--------------------------------------------------------------------*/

static void
vca_poll(int fd)
{

	assert(fd >= 0);
	vca_pollspace((unsigned)fd);
	pollfd[fd].fd = fd;
	pollfd[fd].events = POLLIN;
}

static void
vca_unpoll(int fd)
{

	assert(fd >= 0);
	vca_pollspace((unsigned)fd);
	pollfd[fd].fd = -1;
	pollfd[fd].events = 0;
}

/*--------------------------------------------------------------------*/

static void *
vca_main(void *arg)
{
	unsigned v;
	struct sess *sp, *sp2;
	double deadline;
	int i, fd;

	(void)arg;

	vca_poll(vca_pipes[0]);

	while (1) {
		v = poll(pollfd, npoll, 100);
		if (v && pollfd[vca_pipes[0]].revents) {
			v--;
			i = read(vca_pipes[0], &sp, sizeof sp);
			assert(i == sizeof sp);
			CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
			VTAILQ_INSERT_TAIL(&sesshead, sp, list);
			vca_poll(sp->fd);
		}
		deadline = TIM_real() - params->sess_timeout;
		VTAILQ_FOREACH_SAFE(sp, &sesshead, list, sp2) {
			if (v == 0)
				break;
			CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
			fd = sp->fd;
			if (pollfd[fd].revents) {
				v--;
				i = HTC_Rx(sp->htc);
				VTAILQ_REMOVE(&sesshead, sp, list);
				if (i == 0) {
					VTAILQ_INSERT_HEAD(&sesshead, sp, list);
					continue;
				}
				vca_unpoll(fd);
				vca_handover(sp, i);
				continue;
			}
			if (sp->t_open > deadline)
				continue;
			VTAILQ_REMOVE(&sesshead, sp, list);
			vca_unpoll(fd);
			vca_close_session(sp, "timeout");
			SES_Delete(sp);
		}
	}
}

/*--------------------------------------------------------------------*/

static void
vca_poll_init(void)
{

	AZ(pthread_create(&vca_poll_thread, NULL, vca_main, NULL));
}

struct acceptor acceptor_poll = {
	.name =		"poll",
	.init =		vca_poll_init,
};

#endif /* defined(HAVE_POLL) */
