/*
 * $Id$
 */

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
#include <stdlib.h>

#include "mgt_event.h"
#include "miniobj.h"
#include "binary_heap.h"

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

struct evbase *
ev_new_base(void)
{
	struct evbase *evb;

	evb = calloc(sizeof *evb, 1);
	if (!evb)
		return (evb);
	evb->magic = EVBASE_MAGIC;
	TAILQ_INIT(&evb->events);
	evb->npfd = 1;
	evb->pfd = calloc(sizeof *evb->pfd, evb->npfd);
	if (evb->pfd == NULL) {
		free(evb);
		return (NULL);
	}
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

static int
ev_get_pfd(struct evbase *evb)
{
	unsigned u;
	void *p;

	if (evb->lpfd <= evb->npfd) 
		return (0);

	if (evb->npfd > 256)
		u = evb->npfd + 256;
	else
		u = evb->npfd * 2;
	p = realloc(evb->pfd, sizeof *evb->pfd * u);
	if (p == NULL)
		return (1);
	evb->npfd = u;
	evb->pfd = p;
	return (0);
}

/*--------------------------------------------------------------------*/

int
ev_add(struct evbase *evb, struct ev *e)
{

	CHECK_OBJ_NOTNULL(evb, EVBASE_MAGIC);
	assert(e->magic != EV_MAGIC);
	assert(e->callback != NULL);

	if (e->timeout < 0.0)
		return (EINVAL);

	if (e->fd >= 0) {
		if (ev_get_pfd(evb))
			return (ENOMEM);
		evb->pfd[evb->lpfd].fd = e->fd;
		evb->pfd[evb->lpfd].events =
		    e->flags & (EV_RD|EV_WR|EV_ERR|EV_HUP);
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

	return (0);
}

/*--------------------------------------------------------------------*/

void
ev_del(struct evbase *evb, struct ev *e)
{
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

	i = e->callback(e, 0);
	if (i) {
		ev_del(evb, e);
		free(e);
	} else {
		e->__when += t + e->timeout;
		binheap_delete(evb->binheap, 0);
		binheap_insert(evb->binheap, e);
	}
}

int
ev_schedule_one(struct evbase *evb)
{
	double t;
	struct ev *e, *e2;
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
	i = poll(evb->pfd, evb->lpfd, tmo);
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
		assert(pfd->events == e->flags);
		if (!pfd->revents)
			continue;
printf("Call %p %s (%u)\n", e, e->name, pfd->revents);
		j = e->callback(e, pfd->revents);
printf("Back from %p %s (%u)\n", e, e->name, pfd->revents);
		i--;
		if (evb->disturbed) {
			TAILQ_FOREACH(e2, &evb->events, __list)
				if (e2 == e)
					break;
			assert(e2 == e);
			e2 = TAILQ_NEXT(e, __list);
			evb->disturbed = 0;
		}
		if (j) {
			ev_del(evb, e);
			free(e);
		}
	}
	return (1);
}
