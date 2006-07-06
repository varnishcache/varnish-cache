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
#include <queue.h>

#include <netdb.h>

#include <sbuf.h>
#include <event.h>

#include "config.h"
#include "compat.h"
#include "libvarnish.h"
#include "heritage.h"
#include "shmlog.h"
#include "cache.h"

static struct event_base *evb;
static struct event pipe_e;
static int pipes[2];

static pthread_t vca_thread;

#define SESS_IOVS	10

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
	if (i != sp->mem->liov)
		vca_close_session(sp, "remote closed");
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

void
vca_write_obj(struct worker *w, struct sess *sp)
{
	struct storage *st;
	unsigned u = 0;
	char *r;
	

	VSL(SLT_Response, sp->fd, "%u", sp->obj->response);
	VSL(SLT_Length, sp->fd, "%u", sp->obj->len);

	vca_write(sp, sp->obj->header, strlen(sp->obj->header));

	sbuf_clear(w->sb);
	sbuf_printf(w->sb, "Age: %u\r\n",
		sp->obj->age + sp->t_req - sp->obj->entered);
	sbuf_printf(w->sb, "Via: 1.1 varnish\r\n");
	sbuf_printf(w->sb, "X-Varnish: xid %u\r\n", sp->obj->xid);
	if (http_GetProto(sp->http, &r) && strcmp(r, "HTTP/1.1")) 
		sbuf_printf(w->sb, "Connection: close\r\n");
	sbuf_printf(w->sb, "\r\n");
	sbuf_finish(w->sb);
	vca_write(sp, sbuf_data(w->sb), sbuf_len(w->sb));
	assert(http_GetReq(sp->http, &r));
	if (!strcmp(r, "GET")) {
		TAILQ_FOREACH(st, &sp->obj->store, list) {
			u += st->len;
			if (st->stevedore->send == NULL) {
				vca_write(sp, st->ptr, st->len);
				continue;
			}
			st->stevedore->send(st, sp,
			    sp->mem->iov, sp->mem->niov, sp->mem->liov);
			sp->mem->niov = 0;
			sp->mem->liov = 0;
		}
		assert(u == sp->obj->len);
	}
	vca_flush(sp);
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
	struct sockaddr addr[2];
	struct sess *sp;
	char port[NI_MAXSERV];
	int i;

	VSL_stats->client_conn++;

	(void)arg;
	sm = calloc(sizeof *sm, 1);
	assert(sm != NULL);	/*
				 * XXX: this is probably one we should handle
				 * XXX: accept, emit error NNN and close
				 */
	VSL_stats->n_sess++;

	sp = &sm->s;
	sp->rd_e = &sm->e;
	sp->mem = sm;

	l = sizeof addr;
	sp->fd = accept(fd, addr, &l);
	if (sp->fd < 0) {
		free(sp);
		return;
	}
#ifdef SO_NOSIGPIPE /* XXX Linux */
	i = 1;
	AZ(setsockopt(sp->fd, SOL_SOCKET, SO_NOSIGPIPE, &i, sizeof i));
#endif
	i = getnameinfo(addr, l,
	    sp->addr, VCA_ADDRBUFSIZE,
	    port, sizeof port, NI_NUMERICHOST | NI_NUMERICSERV);
	if (i) {
		printf("getnameinfo = %d %s\n", i,
		    gai_strerror(i));
	}
	strlcat(sp->addr, " ", VCA_ADDRBUFSIZE);
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

/*--------------------------------------------------------------------*/

void
vca_close_session(struct sess *sp, const char *why)
{

	VSL(SLT_SessionClose, sp->fd, why);
	close(sp->fd);
	sp->fd = -1;
}

/*--------------------------------------------------------------------*/

void
vca_return_session(struct sess *sp)
{

	if (sp->fd >= 0) {
		VSL(SLT_SessionReuse, sp->fd, "%s", sp->addr);
		write(pipes[1], &sp, sizeof sp);
	} else {
		if (sp->http != NULL)
			http_Delete(sp->http);
		VSL_stats->n_sess--;
		free(sp->mem);
	}
}

/*--------------------------------------------------------------------*/

void
VCA_Init(void)
{

	AZ(pthread_create(&vca_thread, NULL, vca_main, NULL));
}
