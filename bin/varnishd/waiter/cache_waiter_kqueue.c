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
#include "vtim.h"
#include "vfil.h"

#define NKEV	100

struct vwk {
	unsigned		magic;
#define VWK_MAGIC		0x1cc2acc2
	pthread_t		thread;
	int			pipes[2];
	int			kq;
	struct kevent		ki[NKEV];
	unsigned		nki;
	VTAILQ_HEAD(,sess)	sesshead;
};

/*--------------------------------------------------------------------*/

static void
vwk_kq_flush(struct vwk *vwk)
{
	int i;

	if (vwk->nki == 0)
		return;
	i = kevent(vwk->kq, vwk->ki, vwk->nki, NULL, 0, NULL);
	assert(i == 0);
	vwk->nki = 0;
}

static void
vwk_kq_sess(struct vwk *vwk, struct sess *sp, short arm)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	assert(sp->fd >= 0);
	DSL(DBG_WAITER, sp->vxid, "KQ: EV_SET sp %p arm %x", sp, arm);
	EV_SET(&vwk->ki[vwk->nki], sp->fd, EVFILT_READ, arm, 0, 0, sp);
	if (++vwk->nki == NKEV)
		vwk_kq_flush(vwk);
}

/*--------------------------------------------------------------------*/

static void
vwk_pipe_ev(struct vwk *vwk, const struct kevent *kp)
{
	int i, j;
	struct sess *ss[NKEV];

	AN(kp->udata);
	assert(kp->udata == vwk->pipes);
	j = 0;
	i = read(vwk->pipes[0], ss, sizeof ss);
	if (i == -1 && errno == EAGAIN)
		return;
	while (i >= sizeof ss[0]) {
		CHECK_OBJ_NOTNULL(ss[j], SESS_MAGIC);
		assert(ss[j]->fd >= 0);
		VTAILQ_INSERT_TAIL(&vwk->sesshead, ss[j], list);
		vwk_kq_sess(vwk, ss[j], EV_ADD | EV_ONESHOT);
		j++;
		i -= sizeof ss[0];
	}
	assert(i == 0);
}

/*--------------------------------------------------------------------*/

static void
vwk_sess_ev(struct vwk *vwk, const struct kevent *kp, double now)
{
	struct sess *sp;

	AN(kp->udata);
	assert(kp->udata != vwk->pipes);
	CAST_OBJ_NOTNULL(sp, kp->udata, SESS_MAGIC);
	DSL(DBG_WAITER, sp->vxid, "KQ: sp %p kev data %lu flags 0x%x%s",
	    sp, (unsigned long)kp->data, kp->flags,
	    (kp->flags & EV_EOF) ? " EOF" : "");

	if (kp->data > 0) {
		VTAILQ_REMOVE(&vwk->sesshead, sp, list);
		SES_Handle(sp, now);
		return;
	} else if (kp->flags & EV_EOF) {
		VTAILQ_REMOVE(&vwk->sesshead, sp, list);
		SES_Delete(sp, SC_REM_CLOSE, now);
		return;
	} else {
		VSL(SLT_Debug, sp->vxid,
		    "KQ: sp %p kev data %lu flags 0x%x%s",
		    sp, (unsigned long)kp->data, kp->flags,
		    (kp->flags & EV_EOF) ? " EOF" : "");
	}
}

/*--------------------------------------------------------------------*/

static void *
vwk_thread(void *priv)
{
	struct vwk *vwk;
	struct kevent ke[NKEV], *kp;
	int j, n, dotimer;
	double now, deadline;
	struct sess *sp;

	CAST_OBJ_NOTNULL(vwk, priv, VWK_MAGIC);
	THR_SetName("cache-kqueue");

	vwk->kq = kqueue();
	assert(vwk->kq >= 0);

	j = 0;
	EV_SET(&ke[j], 0, EVFILT_TIMER, EV_ADD, 0, 100, NULL);
	j++;
	EV_SET(&ke[j], vwk->pipes[0], EVFILT_READ, EV_ADD, 0, 0, vwk->pipes);
	j++;
	AZ(kevent(vwk->kq, ke, j, NULL, 0, NULL));

	vwk->nki = 0;
	while (1) {
		dotimer = 0;
		n = kevent(vwk->kq, vwk->ki, vwk->nki, ke, NKEV, NULL);
		now = VTIM_real();
		assert(n <= NKEV);
		if (n == 0) {
			/* This happens on OSX in m00011.vtc */
			dotimer = 1;
			(void)usleep(10000);
		}
		vwk->nki = 0;
		for (kp = ke, j = 0; j < n; j++, kp++) {
			if (kp->filter == EVFILT_TIMER) {
				dotimer = 1;
			} else if (kp->filter == EVFILT_READ &&
			    kp->udata == vwk->pipes) {
				vwk_pipe_ev(vwk, kp);
			} else {
				assert(kp->filter == EVFILT_READ);
				vwk_sess_ev(vwk, kp, now);
			}
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
		vwk_kq_flush(vwk);
		deadline = now - cache_param->timeout_idle;
		for (;;) {
			sp = VTAILQ_FIRST(&vwk->sesshead);
			if (sp == NULL)
				break;
			if (sp->t_idle > deadline)
				break;
			VTAILQ_REMOVE(&vwk->sesshead, sp, list);
			// XXX: not yet (void)VTCP_linger(sp->fd, 0);
			SES_Delete(sp, SC_RX_TIMEOUT, now);
		}
	}
	NEEDLESS_RETURN(NULL);
}

/*--------------------------------------------------------------------*/

static void
vwk_pass(void *priv, struct sess *sp)
{
	struct vwk *vwk;

	CAST_OBJ_NOTNULL(vwk, priv, VWK_MAGIC);

	WAIT_Write_Session(sp, vwk->pipes[1]);
}

/*--------------------------------------------------------------------*/

static void *
vwk_init(void)
{
	struct vwk *vwk;

	ALLOC_OBJ(vwk, VWK_MAGIC);
	AN(vwk);

	VTAILQ_INIT(&vwk->sesshead);
	AZ(pipe(vwk->pipes));

	AZ(VFIL_nonblocking(vwk->pipes[0]));
	AZ(VFIL_nonblocking(vwk->pipes[1]));

	AZ(pthread_create(&vwk->thread, NULL, vwk_thread, vwk));
	return (vwk);
}

/*--------------------------------------------------------------------*/

const struct waiter waiter_kqueue = {
	.name =		"kqueue",
	.init =		vwk_init,
	.pass =		vwk_pass,
};

#endif /* defined(HAVE_KQUEUE) */
