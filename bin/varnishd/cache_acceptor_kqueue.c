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
 * XXX: We need to pass sessions back into the event engine when they are
 * reused.  Not sure what the most efficient way is for that.  For now
 * write the session pointer to a pipe which the event engine monitors.
 */

#if defined(HAVE_KQUEUE)

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/event.h>

#include "heritage.h"
#include "shmlog.h"
#include "cache.h"
#include "cache_acceptor.h"

/**********************************************************************
 * Generic bitmap functions, may be generalized at some point.
 */

#define VBITMAP_TYPE	unsigned	/* Our preferred wordsize */
#define VBITMAP_LUMP	(32*1024)	/* How many bits we alloc at a time */
#define VBITMAP_WORD	(sizeof(VBITMAP_TYPE) * 8)
#define VBITMAP_IDX(n)	(n / VBITMAP_WORD)
#define VBITMAP_BIT(n)	(1U << (n % VBITMAP_WORD))

struct vbitmap {
	VBITMAP_TYPE	*bits;
	unsigned	nbits;
};

static void
vbit_expand(struct vbitmap *vb, unsigned bit)
{
	unsigned char *p;

	bit += VBITMAP_LUMP - 1;
	bit -= (bit % VBITMAP_LUMP);
	VSL(SLT_Debug, 0, "Expanding KQ VBIT to %u", bit);
	p = realloc(vb->bits, bit / 8);
	AN(p);
	memset(p + vb->nbits / 8, 0, (bit - vb->nbits) / 8);
	vb->bits = (void*)p;
	vb->nbits = bit;
}

static struct vbitmap *
vbit_init(unsigned initial)
{
	struct vbitmap *vb;

	vb = calloc(sizeof *vb, 1);
	AN(vb);
	if (initial == 0) {
#ifdef HAVE_GETDTABLESIZE
		initial = getdtablesize();
#else
		initial = VBITMAP_LUMP;
#endif
	}
	vbit_expand(vb, initial);
	return (vb);
}

static void
vbit_set(struct vbitmap *vb, unsigned bit)
{

	if (bit >= vb->nbits)
		vbit_expand(vb, bit);
	vb->bits[VBITMAP_IDX(bit)] |= VBITMAP_BIT(bit);
}

static void
vbit_clr(struct vbitmap *vb, unsigned bit)
{

	if (bit >= vb->nbits)
		vbit_expand(vb, bit);
	vb->bits[VBITMAP_IDX(bit)] &= ~VBITMAP_BIT(bit);
}

static int
vbit_test(struct vbitmap *vb, unsigned bit)
{

	if (bit >= vb->nbits)
		vbit_expand(vb, bit);
	return (vb->bits[VBITMAP_IDX(bit)] & VBITMAP_BIT(bit));
}

/**********************************************************************/


static pthread_t vca_kqueue_thread;
static int kq = -1;

static struct vbitmap *vca_kqueue_bits;

static VTAILQ_HEAD(,sess) sesshead = VTAILQ_HEAD_INITIALIZER(sesshead);

#define NKEV	100

static struct kevent ki[NKEV];
static unsigned nki;

static void
vca_kq_sess(struct sess *sp, short arm)
{
	int i;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	if (sp->fd < 0)
		return;
	EV_SET(&ki[nki], sp->fd, EVFILT_READ, arm, 0, 0, sp);
	if (++nki == NKEV) {
		i = kevent(kq, ki, nki, NULL, 0, NULL);
		assert(i <= 0);
		if (i < 0) {
			/* 
			 * We do not push kevents into the kernel before 
			 * passing the session off to a worker thread, so
			 * by the time we get around to delete the event
			 * the fd may be closed and we get an ENOENT back
			 * once we do flush.
			 * We can get EBADF the same way if the client closes
			 * on us.  In that case, we get no kevent on that
			 * socket, but the TAILQ still has it, and it will
			 * be GC'ed there after the timeout.
			 */
			assert(errno == ENOENT || errno == EBADF);
		}
		nki = 0;
	}
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
			AZ(vbit_test(vca_kqueue_bits, ss[j]->fd));
			vbit_set(vca_kqueue_bits, ss[j]->fd);
			VTAILQ_INSERT_TAIL(&sesshead, ss[j], list);
			vca_kq_sess(ss[j], EV_ADD);
			j++;
			i -= sizeof ss[0];
		}
		assert(i == 0);
		return;
	}
	if (!vbit_test(vca_kqueue_bits, kp->ident)) {
		VSL(SLT_Debug, kp->ident,
		    "KQ: not my fd %d, sp %p kev data %lu flags 0x%x%s",
		    kp->ident, kp->udata, (unsigned long)kp->data, kp->flags,
		    (kp->flags & EV_EOF) ? " EOF" : "");
		return;
	}
	CAST_OBJ_NOTNULL(sp, kp->udata, SESS_MAGIC);
#ifdef DIAGNOSTICS
	VSL(SLT_Debug, sp->id, "sp %p kev data %lu flags 0x%x%s",
	    sp, (unsigned long)kp->data, kp->flags,
	    (kp->flags & EV_EOF) ? " EOF" : "");
#endif
	assert(sp->fd == kp->ident);
	if (kp->data > 0) {
		i = HTC_Rx(sp->htc);
		if (i == 0)
			return;	/* more needed */
		vbit_clr(vca_kqueue_bits, sp->fd);
		VTAILQ_REMOVE(&sesshead, sp, list);
		vca_kq_sess(sp, EV_DELETE);
		vca_handover(sp, i);
		return;
	} else if (kp->flags == EV_EOF) {
		vbit_clr(vca_kqueue_bits, sp->fd);
		VTAILQ_REMOVE(&sesshead, sp, list);
		vca_close_session(sp, "EOF");
		SES_Delete(sp);
		return;
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

	THR_Name("cache-kqueue");
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
			if (kp->flags & EV_ERROR) {
				/* See comment in vca_kq_sess() */
				continue;
			}
			if (kp->filter == EVFILT_TIMER) {
				dotimer = 1;
				continue;
			}
			assert(kp->filter == EVFILT_READ);
			vca_kev(kp);
		}
		if (!dotimer)
			continue;
		deadline = TIM_real() - params->sess_timeout;
		for (;;) {
			sp = VTAILQ_FIRST(&sesshead);
			if (sp == NULL)
				break;
			if (sp->t_open > deadline)
				break;
			vbit_clr(vca_kqueue_bits, sp->fd);
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
	i |= O_NONBLOCK;
	i = fcntl(vca_pipes[0], F_SETFL, i);

	vca_kqueue_bits = vbit_init(0);
	AZ(pthread_create(&vca_kqueue_thread, NULL, vca_kqueue_main, NULL));
}

struct acceptor acceptor_kqueue = {
	.name =		"kqueue",
	.init =		vca_kqueue_init,
};

#endif /* defined(HAVE_KQUEUE) */
