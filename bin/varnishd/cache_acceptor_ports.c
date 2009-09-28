/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006 Linpro AS
 * Copyright (c) 2007 OmniTI Computer Consulting, Inc.
 * Copyright (c) 2007 Theo Schlossnagle
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
 * $Id$
 *
 * XXX: We need to pass sessions back into the event engine when they are
 * reused.  Not sure what the most efficient way is for that.  For now
 * write the session pointer to a pipe which the event engine monitors.
 */

#include "config.h"
#if defined(HAVE_PORT_CREATE)

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <port.h>

#ifndef HAVE_CLOCK_GETTIME
#include "compat/clock_gettime.h"
#endif

#include "shmlog.h"
#include "cache.h"
#include "cache_acceptor.h"

#define MAX_EVENTS 256
static pthread_t vca_ports_thread;
int solaris_dport = -1;

static VTAILQ_HEAD(,sess) sesshead = VTAILQ_HEAD_INITIALIZER(sesshead);

static void
vca_add(int fd, void *data)
{
	AZ(port_associate(solaris_dport, PORT_SOURCE_FD, fd,
	    POLLIN | POLLERR | POLLPRI, data));
}

static void
vca_del(int fd)
{
	port_dissociate(solaris_dport, PORT_SOURCE_FD, fd);
}

static void
vca_port_ev(port_event_t *ev) {
	struct sess *sp;
	if(ev->portev_source == PORT_SOURCE_USER) {
		sp = ev->portev_user;
		CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
		assert(sp->fd >= 0);
		AZ(sp->obj);
		VTAILQ_INSERT_TAIL(&sesshead, sp, list);
		vca_add(sp->fd, sp);
	} else {
		int i;
		CAST_OBJ_NOTNULL(sp, ev->portev_user, SESS_MAGIC);
		if(ev->portev_events & POLLERR) {
			VTAILQ_REMOVE(&sesshead, sp, list);
			vca_close_session(sp, "EOF");
			SES_Delete(sp);
			return;
		}
		i = HTC_Rx(sp->htc);
		if (i == 0)
			return;
		if (i > 0) {
			VTAILQ_REMOVE(&sesshead, sp, list);
			if (sp->fd != -1)
				vca_del(sp->fd);
			vca_handover(sp, i);
		}
	}
	return;
}

static void *
vca_main(void *arg)
{
	struct sess *sp;

	(void)arg;

	solaris_dport = port_create();
	assert(solaris_dport >= 0);

	while (1) {
		port_event_t ev[MAX_EVENTS];
		int nevents, ei;
		double deadline;
		struct timespec ts;
		ts.tv_sec = 0L;
		ts.tv_nsec = 50L /*ms*/  * 1000L /*us*/  * 1000L /*ns*/;
		nevents = 1;
		if (port_getn(solaris_dport, ev, MAX_EVENTS, &nevents, &ts)
		     == 0) {
			for (ei=0; ei<nevents; ei++) {
				vca_port_ev(ev + ei);
			}
		}
		/* check for timeouts */
		deadline = TIM_real() - params->sess_timeout;
		for (;;) {
			sp = VTAILQ_FIRST(&sesshead);
			if (sp == NULL)
				break;
			if (sp->t_open > deadline)
				break;
			VTAILQ_REMOVE(&sesshead, sp, list);
			if(sp->fd != -1)
				vca_del(sp->fd);
			TCP_linger(sp->fd, 0);
			vca_close_session(sp, "timeout");
			SES_Delete(sp);
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

struct acceptor acceptor_ports = {
	.name =		"ports",
	.init =		vca_ports_init,
	.pass =		vca_ports_pass
};

#endif /* defined(HAVE_PORT_CREATE) */
