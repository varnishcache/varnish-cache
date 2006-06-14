/*
 * $Id$
 *
 * XXX: We need to pass sessions back into the event engine when they are
 * reused.  Not sure what the most efficient way is for that.  For now
 * write the session pointer to a pipe which the event engine monitors.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include <sys/uio.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <netdb.h>

#include <sbuf.h>
#include <event.h>

#include "config.h"
#include "compat.h"
#include "libvarnish.h"
#include "vcl_lang.h"
#include "heritage.h"
#include "shmlog.h"
#include "cache.h"

static struct event_base *evb;
static struct event pipe_e;
static int pipes[2];

static pthread_t vca_thread;

#define SESS_IOVS	5

static struct event accept_e[2 * HERITAGE_NSOCKS];

struct sessmem {
	struct sess	s;
	struct event	e;
	struct iovec	iov[SESS_IOVS];
	int		niov;
	size_t		liov;
};

/*--------------------------------------------------------------------
 * Write data to client
 * We try to use writev() if possible in order to minimize number of
 * syscalls made and packets sent.  It also just might allow the worker
 * thread to complete the request without holding stuff locked.
 */

void
vca_flush(struct sess *sp)
{
	int i;

	if (sp->fd < 0 || sp->mem->niov == 0)
		return;
	i = writev(sp->fd, sp->mem->iov, sp->mem->niov);
	if (i != sp->mem->liov) {
		VSL(SLT_SessionClose, sp->fd, "Premature %d of %d",
		    i,  sp->mem->liov);
		close(sp->fd);
		sp->fd = -1;
	}
	sp->mem->liov = 0;
	sp->mem->niov = 0;
}

void
vca_write(struct sess *sp, void *ptr, size_t len)
{

	if (sp->fd < 0 || len == 0)
		return;
	if (sp->mem->niov == SESS_IOVS)
		vca_flush(sp);
	if (sp->fd < 0)
		return;
	sp->mem->iov[sp->mem->niov].iov_base = ptr;
	sp->mem->iov[sp->mem->niov++].iov_len = len;
	sp->mem->liov += len;
}

/*--------------------------------------------------------------------*/

static void
pipe_f(int fd, short event, void *arg)
{
	struct sess *sp;
	int i;

	i = read(fd, &sp, sizeof sp);
	assert(i == sizeof sp);
	http_RecvHead(sp->http, sp->fd, evb, DealWithSession, sp);
}

static void
accept_f(int fd, short event, void *arg)
{
	socklen_t l;
	struct sessmem *sm;
	struct sockaddr addr;
	struct sess *sp;
	char port[10];
	int i;

	(void)arg;
	sm = calloc(sizeof *sm, 1);
	assert(sm != NULL);	/*
				 * XXX: this is probably one we should handle
				 * XXX: accept, emit error NNN and close
				 */

	sp = &sm->s;
	sp->rd_e = &sm->e;
	sp->mem = sm;

	l = sizeof addr;
	sp->fd = accept(fd, &addr, &l);
	if (sp->fd < 0) {
		free(sp);
		return;
	}
	i = 1;
	AZ(setsockopt(sp->fd, SOL_SOCKET, SO_NOSIGPIPE, &i, sizeof i));
	AZ(getnameinfo(&addr, l,
	    sp->addr, VCA_ADDRBUFSIZE,
	    port, sizeof port, NI_NUMERICHOST | NI_NUMERICSERV));
	strlcat(sp->addr, ":", VCA_ADDRBUFSIZE);
	strlcat(sp->addr, port, VCA_ADDRBUFSIZE);
	VSL(SLT_SessionOpen, sp->fd, "%s", sp->addr);
	sp->http = http_New();
	http_RecvHead(sp->http, sp->fd, evb, DealWithSession, sp);
}

static void *
vca_main(void *arg)
{
	unsigned u;
	struct event *ep;

	AZ(pipe(pipes));
	evb = event_init();

	event_set(&pipe_e, pipes[0], EV_READ | EV_PERSIST, pipe_f, NULL);
	event_base_set(evb, &pipe_e);
	event_add(&pipe_e, NULL);

	ep = accept_e;
	for (u = 0; u < HERITAGE_NSOCKS; u++) {
		if (heritage.sock_local[u] >= 0) {
			event_set(ep, heritage.sock_local[u],
			    EV_READ | EV_PERSIST,
			    accept_f, NULL);
			event_base_set(evb, ep);
			event_add(ep, NULL);
			ep++;
		}
		if (heritage.sock_remote[u] >= 0) {
			event_set(ep, heritage.sock_remote[u],
			    EV_READ | EV_PERSIST,
			    accept_f, NULL);
			event_base_set(evb, ep);
			event_add(ep, NULL);
			ep++;
		}
	}

	event_base_loop(evb, 0);

	return ("FOOBAR");
}

void
vca_recycle_session(struct sess *sp)
{
	VSL(SLT_SessionReuse, sp->fd, "%s", sp->addr);
	write(pipes[1], &sp, sizeof sp);
}

void
vca_retire_session(struct sess *sp)
{

	if (sp->http != NULL)
		http_Delete(sp->http);
	if (sp->fd >= 0) {
		VSL(SLT_SessionClose, sp->fd, "%s", sp->addr);
		close(sp->fd);
	}
	free(sp);
}

/*--------------------------------------------------------------------*/

void
VCA_Init(void)
{

	AZ(pthread_create(&vca_thread, NULL, vca_main, NULL));
}
