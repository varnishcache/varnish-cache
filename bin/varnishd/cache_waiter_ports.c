/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006 Linpro AS
 * Copyright (c) 2007 OmniTI Computer Consulting, Inc.
 * Copyright (c) 2007 Theo Schlossnagle
 * Copyright (c) 2010 UPLEX, Nils Goroll
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")
#if defined(HAVE_PORT_CREATE)

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#include <port.h>
#include <sys/time.h>

#include "cache.h"
#include "cache_waiter.h"

#define MAX_EVENTS 256
static pthread_t vca_ports_thread;
int solaris_dport = -1;

static VTAILQ_HEAD(,sess) sesshead = VTAILQ_HEAD_INITIALIZER(sesshead);

static inline void
vca_add(int fd, void *data)
{
	/*
	 * POLLIN should be all we need here
	 *
	 */
	AZ(port_associate(solaris_dport, PORT_SOURCE_FD, fd, POLLIN, data));
}

static inline void
vca_del(int fd)
{
	port_dissociate(solaris_dport, PORT_SOURCE_FD, fd);
}

static inline void
vca_port_ev(port_event_t *ev) {
	struct sess *sp;
	if(ev->portev_source == PORT_SOURCE_USER) {
		CAST_OBJ_NOTNULL(sp, ev->portev_user, SESS_MAGIC);
		assert(sp->fd >= 0);
		AZ(sp->obj);
		VTAILQ_INSERT_TAIL(&sesshead, sp, list);
		vca_add(sp->fd, sp);
	} else {
		int i;
		assert(ev->portev_source == PORT_SOURCE_FD);
		CAST_OBJ_NOTNULL(sp, ev->portev_user, SESS_MAGIC);
		assert(sp->fd >= 0);
		if(ev->portev_events & POLLERR) {
			vca_del(sp->fd);
			VTAILQ_REMOVE(&sesshead, sp, list);			
			vca_close_session(sp, "EOF");
			SES_Delete(sp);
			return;
		}
		i = HTC_Rx(sp->htc);

		if (i == 0) {
			/* incomplete header, wait for more data */
			vca_add(sp->fd, sp);
			return;
		}

		/* 
		 * note: the original man page for port_associate(3C) states:
		 *
		 *    When an event for a PORT_SOURCE_FD object is retrieved,
		 *    the object no longer has an association with the port.
		 *
		 * This can be read along the lines of sparing the
		 * port_dissociate after port_getn(), but in fact,
		 * port_dissociate should be used
		 *
		 * Ref: http://opensolaris.org/jive/thread.jspa?threadID=129476&tstart=0
		 */
		vca_del(sp->fd);
		VTAILQ_REMOVE(&sesshead, sp, list);

		/* vca_handover will also handle errors */
		vca_handover(sp, i);
	}
	return;
}

static void *
vca_main(void *arg)
{
	struct sess *sp;

	/*
	 * timeouts:
	 *
	 * min_ts : Minimum timeout for port_getn
	 * min_t  : ^ equivalent in floating point representation
	 *
	 * max_ts : Maximum timeout for port_getn
	 * max_t  : ^ equivalent in floating point representation
	 *
	 * with (nevents == 1), we should always choose the correct port_getn
	 * timeout to check session timeouts, so max is just a safety measure
	 * (if this implementation is correct, it could be set to an "infinte"
	 *  value)
	 *
	 * with (nevents > 1), min and max define the acceptable range for
	 * - additional latency of keep-alive connections and
	 * - additional tolerance for handling session timeouts
	 *
	 */
	static struct timespec min_ts = {0L,    100L /*ms*/  * 1000L /*us*/  * 1000L /*ns*/};
	static double          min_t  = 0.1; /* 100    ms*/
	static struct timespec max_ts = {1L, 0L}; 		/* 1 second */
	static double	       max_t  = 1.0;			/* 1 second */

	struct timespec ts;
	struct timespec *timeout;

	(void)arg;

	solaris_dport = port_create();
	assert(solaris_dport >= 0);

	timeout = &max_ts;

	while (1) {
		port_event_t ev[MAX_EVENTS];
		int nevents, ei, ret;
		double now, deadline;

		/*
		 * XXX Do we want to scale this up dynamically to increase
		 *     efficiency in high throughput situations? - would need to
		 *     start with one to keep latency low at any rate
		 *
		 *     Note: when increasing nevents, we must lower min_ts
		 *	     and max_ts
		 */
		nevents = 1;

		/*
		 * see disucssion in
		 * - https://issues.apache.org/bugzilla/show_bug.cgi?id=47645
		 * - http://mail.opensolaris.org/pipermail/networking-discuss/2009-August/011979.html
		 *
		 * comment from apr/poll/unix/port.c :
		 *
		 * This confusing API can return an event at the same time
		 * that it reports EINTR or ETIME.
		 *
		 */

		ret = port_getn(solaris_dport, ev, MAX_EVENTS, &nevents, timeout);

		if (ret < 0)
			assert((errno == EINTR) || (errno == ETIME));

		for (ei=0; ei<nevents; ei++) {
			vca_port_ev(ev + ei);
		}

		/* check for timeouts */
		now = TIM_real();
		deadline = now - params->sess_timeout;

		/*
		 * This loop assumes that the oldest sessions are always at the
		 * beginning of the list (which is the case if we guarantee to
		 * enqueue at the tail only
		 *
		 */

		for (;;) {
			sp = VTAILQ_FIRST(&sesshead);
			if (sp == NULL)
				break;
			if (sp->t_open > deadline) {
				break;
			}
			VTAILQ_REMOVE(&sesshead, sp, list);
			if(sp->fd != -1) {
				vca_del(sp->fd);
			}
			vca_close_session(sp, "timeout");
			SES_Delete(sp);
		}

		/*
		 * Calculate the timeout for the next get_portn
		 */

		if (sp) {
			double tmo = (sp->t_open + params->sess_timeout) - now;

			/* we should have removed all sps whose timeout has passed */
			assert(tmo > 0.0);

			if (tmo < min_t) {
				timeout = &min_ts;
			} else if (tmo > max_t) {
				timeout = &max_ts;
			} else {
				/* TIM_t2ts() ? see #630 */
				ts.tv_sec = (int)floor(tmo);
				ts.tv_nsec = 1e9 * (tmo - ts.tv_sec);
				timeout = &ts;
			}
		} else {
			timeout = &max_ts;
		}
	}
}

static void
vca_ports_pass(struct sess *sp)
{
	int r;
	while((r = port_send(solaris_dport, 0, sp)) == -1 &&
		errno == EAGAIN);
	AZ(r);
}

/*--------------------------------------------------------------------*/

static void
vca_ports_init(void)
{

	AZ(pthread_create(&vca_ports_thread, NULL, vca_main, NULL));
}

struct waiter waiter_ports = {
	.name =		"ports",
	.init =		vca_ports_init,
	.pass =		vca_ports_pass
};

#endif /* defined(HAVE_PORT_CREATE) */
