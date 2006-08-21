/*
 * $Id: cache_acceptor.c 860 2006-08-21 09:49:43Z phk $
 *
 * XXX: We need to pass sessions back into the event engine when they are
 * reused.  Not sure what the most efficient way is for that.  For now
 * write the session pointer to a pipe which the event engine monitors.
 */

#if defined(HAVE_KQUEUE)

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/uio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/event.h>

#ifndef HAVE_SRANDOMDEV
#include "compat/srandomdev.h"
#endif

#include "heritage.h"
#include "shmlog.h"
#include "cache.h"
#include "cache_acceptor.h"

static pthread_t vca_kqueue_thread1;
static pthread_t vca_kqueue_thread2;
static int kq = -1;

#define NKEV	100

static void
vca_kq_sess(struct sess *sp, int arm)
{
	struct kevent ke[2];
	int i, j, arm2;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	memset(ke, 0, sizeof ke);
	if (arm == EV_ADD || arm == EV_ENABLE) {
		assert(sp->kqa == 0);
		sp->kqa = 1;
		arm2 = EV_ADD;
	} else  {
		assert(sp->kqa == 1);
		sp->kqa = 0;
		arm2 = EV_DELETE;
	}
	j = 0;
	EV_SET(&ke[j++], sp->id, EVFILT_TIMER, arm2,
	    0, params->sess_timeout * 1000, sp);
	if (sp->fd >= 0)
		EV_SET(&ke[j++], sp->fd, EVFILT_READ, arm, 0, 0, sp);

	i = kevent(kq, ke, j, NULL, 0, NULL);
	assert(i == 0);
}

static struct sess *
vca_kev(struct kevent *kp)
{
	int i;
	struct sess *sp;

	if (kp->udata == NULL) {
		VSL(SLT_Debug, 0,
		    "KQ RACE %s flags %x fflags %x data %x",
		    kp->filter == EVFILT_READ ? "R" : "T",
		    kp->flags, kp->fflags, kp->data);
		return (NULL);
	}
	CAST_OBJ_NOTNULL(sp, kp->udata, SESS_MAGIC);
	if (sp->kqa == 0) {
		VSL(SLT_Debug, sp->id,
		    "KQ %s flags %x fflags %x data %x",
		    kp->filter == EVFILT_READ ? "R" : "T",
		    kp->flags, kp->fflags, kp->data);
		return (NULL);
	}
	if (kp->filter == EVFILT_READ) {
		if (kp->data > 0) {
			i = http_RecvSome(sp->fd, sp->http);
			switch (i) {
			case -1:
				return (NULL);
			case 0:
				vca_kq_sess(sp, EV_DISABLE);
				vca_handover(sp, i);
				return (NULL);	 /* ?? */
			case 1:
				vca_close_session(sp, "overflow");
				break;
			case 2:
				vca_close_session(sp, "no request");
				break;
			default:
				INCOMPL();
			}
			return (sp);
		}
		if (kp->flags == EV_EOF) {
			vca_close_session(sp, "EOF");
			return (sp);
		}
		INCOMPL();
	}
	if (kp->filter == EVFILT_TIMER) {
		vca_close_session(sp, "timeout");
		return (sp);
	}
	INCOMPL();
}


static void *
vca_kqueue_main(void *arg)
{
	struct kevent ke[NKEV], *kp;
	int i, j, n;
	struct sess *sp;

	(void)arg;

	kq = kqueue();
	assert(kq >= 0);

	while (1) {
		n = kevent(kq, NULL, 0, ke, NKEV, NULL);
		assert(n >= 1 && n <= NKEV);
		for (kp = ke, j = 0; j < n; j++, kp++) {
			sp = vca_kev(kp);
			if (sp != NULL) {
				vca_kq_sess(sp, EV_DELETE);
				SES_Delete(sp);
				for (i = j; i < n; i++)
					if (ke[i].udata == sp)
						ke[i].udata = NULL;
			}
		}
	}
	INCOMPL();
}

static void *
vca_kqueue_acct(void *arg)
{
	struct sess *sp;

	(void)arg;
	while (1) {
		sp = vca_accept_sess(heritage.socket);
		if (sp == NULL)
			continue;
		clock_gettime(CLOCK_MONOTONIC, &sp->t_idle);
		http_RecvPrep(sp->http);
		vca_kq_sess(sp, EV_ADD);
	}
}

/*--------------------------------------------------------------------*/

static void
vca_kqueue_recycle(struct sess *sp)
{

	if (sp->fd < 0) {
		SES_Delete(sp);
		return;
	}
	(void)clock_gettime(CLOCK_REALTIME, &sp->t_open);
	VSL(SLT_SessionReuse, sp->fd, "%s %s", sp->addr, sp->port);
	if (http_RecvPrepAgain(sp->http))
		vca_handover(sp, 0);
	else 
		vca_kq_sess(sp, EV_ENABLE);
}

static void
vca_kqueue_init(void)
{
	AZ(pthread_create(&vca_kqueue_thread1, NULL, vca_kqueue_main, NULL));
	AZ(pthread_create(&vca_kqueue_thread2, NULL, vca_kqueue_acct, NULL));
}

struct acceptor acceptor_kqueue = {
	.name =		"kqueue",
	.init =		vca_kqueue_init,
	.recycle =	vca_kqueue_recycle,
};

#endif /* defined(HAVE_KQUEUE) */
