/*
 * $Id$
 */

#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>

#include "mgt_event.h"
#include "miniobj.h"
#include "binary_heap.h"

struct evsig {
	struct evbase		*evb;
	struct ev		*ev;
	struct sigaction	sigact;
	unsigned char		happened;
};

static struct evsig		*ev_sigs;
static unsigned			ev_nsig;

struct evbase {
	unsigned		magic;
#define EVBASE_MAGIC		0x0cfd976f
	TAILQ_HEAD(,ev)		events;
	struct pollfd		*pfd;
	unsigned		npfd;
	unsigned		lpfd;
	struct binheap		*binheap;
	unsigned char		compact_pfd;
	unsigned char		disturbed;
	unsigned		psig;
};

/*--------------------------------------------------------------------*/

static double
ev_now(void)
{
	double t;
	struct timespec ts;

	assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
	t = ts.tv_sec + ts.tv_nsec * 1e-9;
	return (t);
}

/*--------------------------------------------------------------------*/

static void
ev_bh_update(void *priv, void *a, unsigned u)
{
	struct evbase *evb;
	struct ev *e;

	CAST_OBJ_NOTNULL(evb, priv, EVBASE_MAGIC);
	CAST_OBJ_NOTNULL(e, a, EV_MAGIC);
	e->__binheap_idx = u;
}

static int
ev_bh_cmp(void *priv, void *a, void *b)
{
	struct evbase *evb;
	struct ev *ea, *eb;

	CAST_OBJ_NOTNULL(evb, priv, EVBASE_MAGIC);
	CAST_OBJ_NOTNULL(ea, a, EV_MAGIC);
	CAST_OBJ_NOTNULL(eb, b, EV_MAGIC);
	return (ea->__when < eb->__when);
}

/*--------------------------------------------------------------------*/

static int
ev_get_pfd(struct evbase *evb)
{
	unsigned u;
	void *p;

	if (evb->lpfd < evb->npfd) 
		return (0);

	if (evb->npfd > 256)
		u = evb->npfd + 256;
	else if (evb->npfd > 8)
		u = evb->npfd * 2;
	else
		u = 8;
	p = realloc(evb->pfd, sizeof *evb->pfd * u);
	if (p == NULL)
		return (1);
	evb->npfd = u;
	evb->pfd = p;
	return (0);
}

/*--------------------------------------------------------------------*/

static int
ev_get_sig(int sig)
{
	struct evsig *os;

	if (sig < ev_nsig) 
		return (0);

	os = calloc(sizeof *os, (sig + 1));
	if (os == NULL)
		return (ENOMEM);

	memcpy(os, ev_sigs, ev_nsig * sizeof *os);

	free(ev_sigs);
	ev_sigs = os;
	ev_nsig = sig + 1;

	return (0);
}

/*--------------------------------------------------------------------*/

static void
ev_sighandler(int sig)
{
	struct evsig *es;

	assert(sig < ev_nsig);
	assert(ev_sigs != NULL);
	es = &ev_sigs[sig];
	if (!es->happened)
		es->evb->psig++;
	es->happened = 1;
printf("SIG %d happened\n", sig);
}

/*--------------------------------------------------------------------*/

struct evbase *
ev_new_base(void)
{
	struct evbase *evb;

	evb = calloc(sizeof *evb, 1);
	if (!evb)
		return (evb);
	if (ev_get_pfd(evb)) {
		free(evb);
		return (NULL);
	}
	evb->magic = EVBASE_MAGIC;
	TAILQ_INIT(&evb->events);
	evb->binheap = binheap_new(evb, ev_bh_cmp, ev_bh_update);
	return (evb);
}

/*--------------------------------------------------------------------*/

void
ev_destroy_base(struct evbase *evb)
{
	CHECK_OBJ_NOTNULL(evb, EVBASE_MAGIC);
	evb->magic = 0;
	free(evb);
}

/*--------------------------------------------------------------------*/

struct ev *
ev_new(void)
{
	struct ev *e;

	e = calloc(sizeof *e, 1);
	if (e != NULL) {
		e->fd = -1;
	}
	return (e);
}

/*--------------------------------------------------------------------*/

int
ev_add(struct evbase *evb, struct ev *e)
{
	struct evsig *es;

	CHECK_OBJ_NOTNULL(evb, EVBASE_MAGIC);
	assert(e->magic != EV_MAGIC);
	assert(e->callback != NULL);
	assert(e->sig >= 0);
	assert(e->timeout >= 0.0);
	assert(e->fd < 0 || e->fd_flags);

	if (e->sig > 0 && ev_get_sig(e->sig))
		return (ENOMEM);

	if (e->fd >= 0 && ev_get_pfd(evb))
		return (ENOMEM);

	if (e->sig > 0) {
		es = &ev_sigs[e->sig];
		if (es->ev != NULL)
			return (EBUSY);
		assert(es->happened == 0);
		es->ev = e;
		es->evb = evb;
		es->sigact.sa_flags = e->sig_flags;
		es->sigact.sa_handler = ev_sighandler;
	} else {
		es = NULL;
	}

	if (e->fd >= 0) {
		evb->pfd[evb->lpfd].fd = e->fd;
		evb->pfd[evb->lpfd].events =
		    e->fd_flags & (EV_RD|EV_WR|EV_ERR|EV_HUP);
		e->__poll_idx = evb->lpfd;
		evb->lpfd++;
	} else
		e->__poll_idx = -1;

	if (e->timeout != 0.0) {
		e->__when += ev_now() + e->timeout;
		binheap_insert(evb->binheap, e);
	} else {
		e->__when = 0.0;
	}

	e->magic = EV_MAGIC;
	e->__evb = evb;
	e->__binheap_idx = 0;
	e->__privflags = 0;
	if (e->fd < 0)
		TAILQ_INSERT_TAIL(&evb->events, e, __list);
	else
		TAILQ_INSERT_HEAD(&evb->events, e, __list);

	if (e->sig > 0) {
		assert(es != NULL);
		assert(sigaction(e->sig, &es->sigact, NULL) == 0);
	}

	return (0);
}

/*--------------------------------------------------------------------*/

void
ev_del(struct evbase *evb, struct ev *e)
{
	struct evsig *es;

	CHECK_OBJ_NOTNULL(evb, EVBASE_MAGIC);
	CHECK_OBJ_NOTNULL(e, EV_MAGIC);
	assert(evb == e->__evb);

	if (e->__binheap_idx != 0)
		binheap_delete(evb->binheap, e->__binheap_idx);
	assert(e->__binheap_idx == 0);

	if (e->fd >= 0) {
		evb->pfd[e->__poll_idx].fd = -1;
		evb->compact_pfd++;
		e->fd = -1;
	}

	if (e->sig > 0) {
		assert(e->sig < ev_nsig);
		es = &ev_sigs[e->sig];
		assert(es->ev == e);
		es->ev = NULL;
		es->evb = NULL;
		es->sigact.sa_flags = e->sig_flags;
		es->sigact.sa_handler = SIG_DFL;
		assert(sigaction(e->sig, &es->sigact, NULL) == 0);
		es->happened = 0;
	}

	TAILQ_REMOVE(&evb->events, e, __list);

	e->magic = 0;
	e->__evb = NULL;

	evb->disturbed = 1;
}

/*--------------------------------------------------------------------*/

int
ev_schedule(struct evbase *evb)
{
	int i;

	CHECK_OBJ_NOTNULL(evb, EVBASE_MAGIC);
	do 
		i = ev_schedule_one(evb);
	while (i == 1);
	return (i);
}

/*--------------------------------------------------------------------*/

static void
ev_compact_pfd(struct evbase *evb)
{
	/* XXX TBD */
	evb->compact_pfd = 0;
}

/*--------------------------------------------------------------------*/

static void
ev_sched_timeout(struct evbase *evb, struct ev *e, double t)
{
	int i;

printf("Call %p %s (TMO)\n", e, e->name);
	i = e->callback(e, 0);
printf("Back %p %s (TMO)\n", e, e->name);
	if (i) {
		ev_del(evb, e);
		free(e);
	} else {
		e->__when += t + e->timeout;
		binheap_delete(evb->binheap, 0);
		binheap_insert(evb->binheap, e);
	}
}

static void
ev_sched_signal(struct evbase *evb)
{
	int i, j;
	struct evsig *es;
	struct ev *e;

	es = ev_sigs;
	for (j = 0; j < ev_nsig; j++, es++) {
		if (!es->happened || es->evb != evb)
			continue;
		evb->psig--;
		es->happened = 0;
		e = es->ev;
		assert(e != NULL);
printf("Call %p %s (sig %d)\n", e, e->name, j);
		i = e->callback(e, EV_SIG);
printf("Back %p %s (sig %d)\n", e, e->name, j);
		if (i) {
			ev_del(evb, e);
			free(e);
		}
	}
}

int
ev_schedule_one(struct evbase *evb)
{
	double t;
	struct ev *e, *e2, *e3;
	int i, j, tmo;
	struct pollfd *pfd;

	CHECK_OBJ_NOTNULL(evb, EVBASE_MAGIC);
	e = binheap_root(evb->binheap);
	if (e != NULL) {
		CHECK_OBJ_NOTNULL(e, EV_MAGIC);
		t = ev_now();
		if (e->__when <= t) {
			ev_sched_timeout(evb, e, t);
			return (1);
		}
		tmo = (e->__when - t) * 1e3;
	} else
		tmo = INFTIM;

	if (evb->compact_pfd)
		ev_compact_pfd(evb);

	if (tmo == INFTIM && evb->lpfd == 0)
		return (0);

	if (evb->psig) {
printf("hassig\n");
		ev_sched_signal(evb);
		return (1);
	}
	i = poll(evb->pfd, evb->lpfd, tmo);
	if(i == -1 && errno == EINTR) {
printf("gotsig\n");
		ev_sched_signal(evb);
		return (1);
	}
	if (i == 0) {
		assert(e != NULL);
		t = ev_now();
		if (e->__when <= t)
			ev_sched_timeout(evb, e, t);
		return (1);
	}
	evb->disturbed = 0;
	TAILQ_FOREACH_SAFE(e, &evb->events, __list, e2) {
		if (i == 0)
			break;
		if (e->fd < 0)
			continue;
		assert(e->__poll_idx < evb->lpfd);
		pfd = &evb->pfd[e->__poll_idx];
		assert(pfd->fd == e->fd);
		assert(pfd->events == e->fd_flags);
		if (!pfd->revents)
			continue;
printf("Call %p %s (%u)\n", e, e->name, pfd->revents);
		j = e->callback(e, pfd->revents);
printf("Back %p %s (%u)\n", e, e->name, pfd->revents);
		i--;
		if (evb->disturbed) {
			TAILQ_FOREACH(e3, &evb->events, __list) {
				if (e3 == e) {
					e3 = TAILQ_NEXT(e, __list);
					break;
				} else if (e3 == e2)
					break;
			}
			e2 = e3;
			evb->disturbed = 0;
		}
		if (j) {
			ev_del(evb, e);
			free(e);
		}
	}
	return (1);
}
