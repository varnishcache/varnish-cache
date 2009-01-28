/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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
 * XXX: We need to pass sessions back into the event engine when they are
 * reused.  Not sure what the most efficient way is for that.  For now
 * write the session pointer to a pipe which the event engine monitors.
 */

#include "config.h"

#if defined(HAVE_KQUEUE)

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/event.h>

#include "shmlog.h"
#include "cache.h"
#include "cache_acceptor.h"


/**********************************************************************/


static pthread_t vca_kqueue_thread;
static int kq = -1;


static VTAILQ_HEAD(,sess) sesshead = VTAILQ_HEAD_INITIALIZER(sesshead);

#define NKEV	100

static struct kevent ki[NKEV];
static unsigned nki;

static void
vca_kq_flush(void)
{
	int i;

	if (nki == 0)
		return;
	i = kevent(kq, ki, nki, NULL, 0, NULL);
	assert(i == 0);
	nki = 0;
}

static void
vca_kq_sess(struct sess *sp, short arm)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	assert(sp->fd >= 0);
	DSL(0x04, SLT_Debug, sp->fd, "KQ: EV_SET sp %p arm %x", sp, arm);
	EV_SET(&ki[nki], sp->fd, EVFILT_READ, arm, 0, 0, sp);
	if (++nki == NKEV) 
		vca_kq_flush();
}

static void
vca_kev(const struct kevent *kp)
{
	int i, j;
	struct sess *sp;
	struct sess *ss[NKEV];

	AN(kp->udata);
	if (kp->udata == vca_pipes) {
		j = 0;
		i = read(vca_pipes[0], ss, sizeof ss);
		if (i == -1 && errno == EAGAIN)
			return;
		while (i >= sizeof ss[0]) {
			CHECK_OBJ_NOTNULL(ss[j], SESS_MAGIC);
			assert(ss[j]->fd >= 0);
			AZ(ss[j]->obj);
			VTAILQ_INSERT_TAIL(&sesshead, ss[j], list);
			vca_kq_sess(ss[j], EV_ADD | EV_ONESHOT);
			j++;
			i -= sizeof ss[0];
		}
		assert(i == 0);
		return;
	}
	CAST_OBJ_NOTNULL(sp, kp->udata, SESS_MAGIC);
	DSL(0x04, SLT_Debug, sp->id, "KQ: sp %p kev data %lu flags 0x%x%s",
	    sp, (unsigned long)kp->data, kp->flags,
	    (kp->flags & EV_EOF) ? " EOF" : "");

	assert(sp->id == kp->ident);
	assert(sp->fd == sp->id);
	if (kp->data > 0) {
		i = HTC_Rx(sp->htc);
		if (i == 0) {
			vca_kq_sess(sp, EV_ADD | EV_ONESHOT);
			return;	/* more needed */
		}
		VTAILQ_REMOVE(&sesshead, sp, list);
		vca_handover(sp, i);
		return;
	} else if (kp->flags & EV_EOF) {
		VTAILQ_REMOVE(&sesshead, sp, list);
		vca_close_session(sp, "EOF");
		SES_Delete(sp);
		return;
	} else {
		VSL(SLT_Debug, sp->id, "KQ: sp %p kev data %lu flags 0x%x%s",
		    sp, (unsigned long)kp->data, kp->flags,
		    (kp->flags & EV_EOF) ? " EOF" : "");
	}
}

/*--------------------------------------------------------------------*/

static void *
vca_kqueue_main(void *arg)
{
	struct kevent ke[NKEV], *kp;
	int j, n, dotimer;
	double deadline;
	struct sess *sp;

	THR_SetName("cache-kqueue");
	(void)arg;

	kq = kqueue();
	assert(kq >= 0);

	j = 0;
	EV_SET(&ke[j++], 0, EVFILT_TIMER, EV_ADD, 0, 100, NULL);
	EV_SET(&ke[j++], vca_pipes[0], EVFILT_READ, EV_ADD, 0, 0, vca_pipes);
	AZ(kevent(kq, ke, j, NULL, 0, NULL));

	nki = 0;
	while (1) {
		dotimer = 0;
		n = kevent(kq, ki, nki, ke, NKEV, NULL);
		assert(n >= 1 && n <= NKEV);
		nki = 0;
		for (kp = ke, j = 0; j < n; j++, kp++) {
			if (kp->filter == EVFILT_TIMER) {
				dotimer = 1;
				continue;
			}
			assert(kp->filter == EVFILT_READ);
			vca_kev(kp);
		}
		if (!dotimer)
			continue;
		/*
		 * Make sure we have no pending changes for the fd's
		 * we are about to close, in case the accept(2) in the
		 * other thread creates new fd's betwen our close and
		 * the kevent(2) at the top of this loop, the kernel
		 * would not know we meant "the old fd of this number".
		 */
		vca_kq_flush();
		deadline = TIM_real() - params->sess_timeout;
		for (;;) {
			sp = VTAILQ_FIRST(&sesshead);
			if (sp == NULL)
				break;
			if (sp->t_open > deadline)
				break;
			VTAILQ_REMOVE(&sesshead, sp, list);
			vca_close_session(sp, "timeout");
			SES_Delete(sp);
		}
	}
}

/*--------------------------------------------------------------------*/

static void
vca_kqueue_init(void)
{
	int i;

	i = fcntl(vca_pipes[0], F_GETFL);
	assert(i != -1);
	i |= O_NONBLOCK;
	i = fcntl(vca_pipes[0], F_SETFL, i);
	assert(i != -1);

	AZ(pthread_create(&vca_kqueue_thread, NULL, vca_kqueue_main, NULL));
}

struct acceptor acceptor_kqueue = {
	.name =		"kqueue",
	.init =		vca_kqueue_init,
};

#endif /* defined(HAVE_KQUEUE) */
