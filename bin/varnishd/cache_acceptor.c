/*
 * $Id$
 *
 * XXX: We need to pass sessions back into the event engine when they are
 * reused.  Not sure what the most efficient way is for that.  For now
 * write the session pointer to a pipe which the event engine monitors.
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/uio.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <netdb.h>

#include "config.h"
#include "libvarnish.h"
#include "heritage.h"
#include "shmlog.h"
#include "cache.h"

static struct event_base *evb;
static struct event pipe_e;
static int pipes[2];

static struct event tick_e;
static struct timeval tick_rate;

static pthread_t vca_thread;
static unsigned		xids;

static struct event accept_e[2 * HERITAGE_NSOCKS];
static TAILQ_HEAD(,sess) sesshead = TAILQ_HEAD_INITIALIZER(sesshead);

static void
vca_tick(int a, short b, void *c)
{
	struct sess *sp, *sp2;
	struct timespec t;

	(void)a;
	(void)b;
	(void)c;
	AZ(evtimer_add(&tick_e, &tick_rate));
	clock_gettime(CLOCK_MONOTONIC, &t);
	TAILQ_FOREACH_SAFE(sp, &sesshead, list, sp2) {
		CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
		if (sp->t_idle.tv_sec + 30 < t.tv_sec) {
			TAILQ_REMOVE(&sesshead, sp, list);
			vca_close_session(sp, "timeout");
			vca_return_session(sp);
		}
	}
}

static void
vca_callback(void *arg, int bad)
{
	struct sess *sp;

	CAST_OBJ_NOTNULL(sp, arg, SESS_MAGIC);
	TAILQ_REMOVE(&sesshead, sp, list);
	if (bad) {
		if (bad == 1)
			vca_close_session(sp, "overflow");
		else
			vca_close_session(sp, "no request");
		vca_return_session(sp);
		return;
	}
	sp->step = STP_RECV;
	VSL_stats->client_req++;
	sp->xid = xids++;
	VSL(SLT_XID, sp->fd, "%u", sp->xid);
	WRK_QueueSession(sp);
}

static void
pipe_f(int fd, short event, void *arg)
{
	struct sess *sp;
	int i;

	(void)event;
	(void)arg;
	i = read(fd, &sp, sizeof sp);
	assert(i == sizeof sp);
	clock_gettime(CLOCK_MONOTONIC, &sp->t_idle);
	TAILQ_INSERT_TAIL(&sesshead, sp, list);
	http_RecvHead(sp->http, sp->fd, evb, vca_callback, sp);
}

static void
accept_f(int fd, short event, void *arg)
{
	socklen_t l;
	struct sockaddr addr[2];	/* XXX: IPv6 hack */
	struct sess *sp;
	int i;
	struct linger linger;

	(void)event;
	(void)arg;
	VSL_stats->client_conn++;


	l = sizeof addr;
	i = accept(fd, addr, &l);
	if (i < 0) {
		VSL(SLT_Debug, fd, "Accept failed errno=%d", errno);
		/* XXX: stats ? */
		return;
	}
	sp = SES_New(addr, l);
	assert(sp != NULL);	/* XXX handle */

	sp->fd = i;
	sp->id = i;

#ifdef SO_NOSIGPIPE /* XXX Linux */
	i = 1;
	AZ(setsockopt(sp->fd, SOL_SOCKET, SO_NOSIGPIPE, &i, sizeof i));
#endif
#ifdef SO_LINGER /* XXX Linux*/
	linger.l_onoff = 0;
	linger.l_linger = 0;
	AZ(setsockopt(sp->fd, SOL_SOCKET, SO_LINGER, &linger, sizeof linger));
#endif

	TCP_name(addr, l, sp->addr, sizeof sp->addr, sp->port, sizeof sp->port);
	VSL(SLT_SessionOpen, sp->fd, "%s %s", sp->addr, sp->port);
	clock_gettime(CLOCK_MONOTONIC, &sp->t_idle);
	TAILQ_INSERT_TAIL(&sesshead, sp, list);
	http_RecvHead(sp->http, sp->fd, evb, vca_callback, sp);
}

static void *
vca_main(void *arg)
{
	unsigned u;
	struct event *ep;

	(void)arg;

	AZ(pipe(pipes));
	evb = event_init();
	assert(evb != NULL);

	event_set(&pipe_e, pipes[0], EV_READ | EV_PERSIST, pipe_f, NULL);
	AZ(event_base_set(evb, &pipe_e));
	AZ(event_add(&pipe_e, NULL));

	evtimer_set(&tick_e, vca_tick, NULL);
	AZ(event_base_set(evb, &tick_e));
	
	AZ(evtimer_add(&tick_e, &tick_rate));

	ep = accept_e;
	for (u = 0; u < HERITAGE_NSOCKS; u++) {
		if (heritage.sock_local[u] >= 0) {
			event_set(ep, heritage.sock_local[u],
			    EV_READ | EV_PERSIST,
			    accept_f, NULL);
			AZ(event_base_set(evb, ep));
			AZ(event_add(ep, NULL));
			ep++;
		}
		if (heritage.sock_remote[u] >= 0) {
			event_set(ep, heritage.sock_remote[u],
			    EV_READ | EV_PERSIST,
			    accept_f, NULL);
			AZ(event_base_set(evb, ep));
			AZ(event_add(ep, NULL));
			ep++;
		}
	}

	AZ(event_base_loop(evb, 0));
	INCOMPL();
}

/*--------------------------------------------------------------------*/

void
vca_close_session(struct sess *sp, const char *why)
{

	VSL(SLT_SessionClose, sp->fd, why);
	if (sp->fd >= 0)
		AZ(close(sp->fd));
	sp->fd = -1;
}

/*--------------------------------------------------------------------*/

void
vca_return_session(struct sess *sp)
{

	if (sp->fd >= 0) {
		VSL(SLT_SessionReuse, sp->fd, "%s %s", sp->addr, sp->port);
		assert(sizeof sp == write(pipes[1], &sp, sizeof sp));
	} else {
		SES_Delete(sp);
	}
}

/*--------------------------------------------------------------------*/

void
VCA_Init(void)
{

	tick_rate.tv_sec = 1;
	tick_rate.tv_usec = 0;
	AZ(pthread_create(&vca_thread, NULL, vca_main, NULL));
	srandomdev();
	xids = random();
}
