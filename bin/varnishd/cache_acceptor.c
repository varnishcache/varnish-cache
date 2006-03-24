/*
 * $Id$
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <netdb.h>

#include <sbuf.h>
#include <event.h>

#include "libvarnish.h"
#include "heritage.h"
#include "shmlog.h"
#include "cache.h"

static struct event_base *evb;

static struct event accept_e[2 * HERITAGE_NSOCKS];

struct sessmem {
	struct sess	s;
	struct event	e;
};

static void
http_read_f(int fd, short event, void *arg)
{
	struct sess *sp = arg;
	const char *p;
	int i;

	printf("%s(%d, %d, ...)\n", __func__, fd, event);
	assert(VCA_RXBUFSIZE - sp->rcv_len > 0);
	i = read(fd, sp->rcv + sp->rcv_len, VCA_RXBUFSIZE - sp->rcv_len);
	if (i <= 0) {
		VSL(SLT_SessionClose, sp->fd, "remote %d", sp->rcv_len);
		event_del(sp->rd_e);
		close(sp->fd);
		free(sp->mem);
		return;
	}

	sp->rcv_len += i;
	sp->rcv[sp->rcv_len] = '\0';

	p = sp->rcv;
	while (1) {
		/* XXX: we could save location of all linebreaks for later */
		p = strchr(p, '\n');
		if (p == NULL)
			return;
		p++;
		if (*p == '\r')
			p++;
		if (*p != '\n')
			continue;
		break;
	}
	sp->hdr_e = p;
	event_del(sp->rd_e);
	HttpdAnalyze(sp);
}

static void
accept_f(int fd, short event, void *arg __unused)
{
	socklen_t l;
	struct sessmem *sm;
	struct sockaddr addr;
	struct sess *sp;
	char port[10];

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
	AZ(getnameinfo(&addr, l,
	    sp->addr, VCA_ADDRBUFSIZE,
	    port, sizeof port, NI_NUMERICHOST | NI_NUMERICSERV));
	strlcat(sp->addr, ":", VCA_ADDRBUFSIZE);
	strlcat(sp->addr, port, VCA_ADDRBUFSIZE);
	VSL(SLT_SessionOpen, sp->fd, "%s", sp->addr);
	event_set(sp->rd_e, sp->fd, EV_READ | EV_PERSIST,
	    http_read_f, sp);
	event_base_set(evb, sp->rd_e);
	event_add(sp->rd_e, NULL);	/* XXX: timeout */
}

void *
vca_main(void *arg)
{
	unsigned u;
	struct event *ep;

	evb = event_init();

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
