/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
 */

#include <poll.h>
#include "vqueue.h"

struct ev;
struct evbase;

typedef int ev_cb_f(struct ev *, int what);

struct ev {
	unsigned	magic;
#define EV_MAGIC	0x15c8134b

	/* pub */
	const char	*name;
	int		fd;
	unsigned	fd_flags;
#define		EV_RD	POLLIN
#define		EV_WR	POLLOUT
#define		EV_ERR	POLLERR
#define		EV_HUP	POLLHUP
#define		EV_GONE	POLLNVAL
#define		EV_SIG	-1
	int		sig;
	unsigned	sig_flags;
	double		timeout;
	ev_cb_f		*callback;
	void		*priv;

	/* priv */
	double		__when;
	VTAILQ_ENTRY(ev)	__list;
	unsigned	__binheap_idx;
	unsigned	__privflags;
	struct evbase	*__evb;
	int		__poll_idx;
};

struct evbase;

struct evbase *ev_new_base(void);
void ev_destroy_base(struct evbase *evb);

struct ev *ev_new(void);

int ev_add(struct evbase *evb, struct ev *e);
void ev_del(struct evbase *evb, struct ev *e);

int ev_schedule_one(struct evbase *evb);
int ev_schedule(struct evbase *evb);
