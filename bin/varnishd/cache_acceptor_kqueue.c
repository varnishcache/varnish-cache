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

static void
vca_kq_sess(struct sess *sp, int arm)
{
	struct kevent ke;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	if (sp->fd < 0)
		return;
	EV_SET(&ke, sp->fd, EVFILT_READ, arm, 0, 0, sp);
	AZ(kevent(kq, &ke, 1, NULL, 0, NULL));
}

static void
vca_kev(struct kevent *kp)
{
	int i;
	struct sess *sp;

	AN(kp->udata);
	if (kp->udata == pipes) {
		while (kp->data > 0) {
			i = read(pipes[0], &sp, sizeof sp);
			assert(i == sizeof sp);
			kp->data -= i;
			CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
			TAILQ_INSERT_TAIL(&sesshead, sp, list);
			vca_kq_sess(sp, EV_ADD);
		}
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
	int j, n;
	struct timespec ts;
	struct sess *sp;

	(void)arg;

	kq = kqueue();
	assert(kq >= 0);

	j = 0;
	EV_SET(&ke[j++], 0, EVFILT_TIMER, EV_ADD, 0, 100, NULL);
	EV_SET(&ke[j++], pipes[0], EVFILT_READ, EV_ADD, 0, 0, pipes);
	AZ(kevent(kq, ke, j, NULL, 0, NULL));

	while (1) {
		n = kevent(kq, NULL, 0, ke, NKEV, NULL);
		assert(n >= 1 && n <= NKEV);
		for (kp = ke, j = 0; j < n; j++, kp++) {
			if (kp->filter == EVFILT_TIMER)
				continue; 
			assert(kp->filter == EVFILT_READ);
			vca_kev(kp);
		}
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

	AZ(pipe(pipes));
	AZ(pthread_create(&vca_kqueue_thread, NULL, vca_kqueue_main, NULL));
}

struct acceptor acceptor_kqueue = {
	.name =		"kqueue",
	.init =		vca_kqueue_init,
	.recycle =	vca_kqueue_recycle,
};

#endif /* defined(HAVE_KQUEUE) */
