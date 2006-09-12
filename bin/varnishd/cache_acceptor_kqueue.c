/*
 * $Id$
 *
 * XXX: We need to pass sessions back into the event engine when they are
 * reused.  Not sure what the most efficient way is for that.  For now
 * write the session pointer to a pipe which the event engine monitors.
 */

#if defined(HAVE_KQUEUE)

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/event.h>

#include "heritage.h"
#include "shmlog.h"
#include "cache.h"
#include "cache_acceptor.h"

static pthread_t vca_kqueue_thread;
static int kq = -1;

static TAILQ_HEAD(,sess) sesshead = TAILQ_HEAD_INITIALIZER(sesshead);
static int pipes[2];

#define NKEV	100

static struct kevent ki[NKEV];
static unsigned nki;

static void
vca_kq_sess(struct sess *sp, int arm)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	if (sp->fd < 0)
		return;
	EV_SET(&ki[nki], sp->fd, EVFILT_READ, arm, 0, 0, sp);
	if (++nki == NKEV) {
		(void)kevent(kq, ki, nki, NULL, 0, NULL);
		/* XXX: we could check the error returns here */
		nki = 0;
	}
}

static void
vca_kev(struct kevent *kp)
{
	int i, j;
	struct sess *sp;
	struct sess *ss[NKEV];

	AN(kp->udata);
	if (kp->udata == pipes) {
		j = 0;
		i = read(pipes[0], ss, sizeof ss);
		if (i == -1 && errno == EAGAIN)
			return;
		while (i >= sizeof ss[0]) {
			CHECK_OBJ_NOTNULL(ss[j], SESS_MAGIC);
			assert(ss[j]->fd >= 0);
			TAILQ_INSERT_TAIL(&sesshead, ss[j], list);
			vca_kq_sess(ss[j], EV_ADD);
			j++;
			i -= sizeof ss[0];
		}
		assert(i == 0);
		return;
	}
	CAST_OBJ_NOTNULL(sp, kp->udata, SESS_MAGIC);
	if (kp->data > 0) {
		i = vca_pollsession(sp);
		if (i == -1)
			return;
		TAILQ_REMOVE(&sesshead, sp, list);
		if (i == 0) {
			vca_kq_sess(sp, EV_DELETE);
			vca_handover(sp, i);
		} else {
			SES_Delete(sp);
		}
		return;
	}
	if (kp->flags == EV_EOF) {
		TAILQ_REMOVE(&sesshead, sp, list);
		vca_close_session(sp, "EOF");
		SES_Delete(sp);
		return;
	}
	INCOMPL();
}

/*--------------------------------------------------------------------*/

static void *
vca_kqueue_main(void *arg)
{
	struct kevent ke[NKEV], *kp;
	int j, n, dotimer;
	struct timespec ts;
	struct sess *sp;

	(void)arg;

	kq = kqueue();
	assert(kq >= 0);

	j = 0;
	EV_SET(&ke[j++], 0, EVFILT_TIMER, EV_ADD, 0, 100, NULL);
	EV_SET(&ke[j++], pipes[0], EVFILT_READ, EV_ADD, 0, 0, pipes);
	AZ(kevent(kq, ke, j, NULL, 0, NULL));

	nki = 0;
	while (1) {
		dotimer = 0;
		n = kevent(kq, ki, nki, ke, NKEV, NULL);
		assert(n >= 1 && n <= NKEV);
		nki = 0;
		for (kp = ke, j = 0; j < n; j++, kp++) {
			if (kp->flags & EV_ERROR)
				continue;
			if (kp->filter == EVFILT_TIMER) {
				dotimer = 1;
				continue; 
			}
			assert(kp->filter == EVFILT_READ);
			vca_kev(kp);
		}
		if (!dotimer)
			continue;
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec -= params->sess_timeout;
		for (;;) {
			sp = TAILQ_FIRST(&sesshead);
			if (sp == NULL)
				break;
			if (sp->t_open.tv_sec > ts.tv_sec) 
				break;
			if (sp->t_open.tv_sec == ts.tv_sec &&
			    sp->t_open.tv_nsec > ts.tv_nsec)
				break;
			TAILQ_REMOVE(&sesshead, sp, list);
			vca_close_session(sp, "timeout");
			SES_Delete(sp);
		}
	}
}

/*--------------------------------------------------------------------*/

static void
vca_kqueue_recycle(struct sess *sp)
{

	if (sp->fd < 0)
		SES_Delete(sp);
	else
		assert(write(pipes[1], &sp, sizeof sp) == sizeof sp);
}

static void
vca_kqueue_init(void)
{
	int i;

	AZ(pipe(pipes));
	i = fcntl(pipes[0], F_GETFL);
	i |= O_NONBLOCK;
	i = fcntl(pipes[0], F_SETFL, i);
	
	AZ(pthread_create(&vca_kqueue_thread, NULL, vca_kqueue_main, NULL));
}

struct acceptor acceptor_kqueue = {
	.name =		"kqueue",
	.init =		vca_kqueue_init,
	.recycle =	vca_kqueue_recycle,
};

#endif /* defined(HAVE_KQUEUE) */
