/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 * Private interfaces
 */

struct waited;

struct waiter {
	unsigned			magic;
	#define WAITER_MAGIC		0x17c399db
	const struct waiter_impl	*impl;
	VTAILQ_ENTRY(waiter)		list;

	waiter_handle_f *		func;

	int				pipes[2];
	struct waited			*pipe_w;

	volatile double			*tmo;
	VTAILQ_HEAD(,waited)		waithead;

	void				*priv;
};

typedef void waiter_init_f(struct waiter *);
typedef int waiter_pass_f(void *priv, struct waited *);
typedef void waiter_inject_f(const struct waiter *, struct waited *);
typedef void waiter_evict_f(const struct waiter *, struct waited *);

struct waiter_impl {
	const char		*name;
	waiter_init_f		*init;
	waiter_pass_f		*pass;
	waiter_inject_f		*inject;
	waiter_evict_f		*evict;
	size_t			size;
};

/* cache_waiter.c */
void Wait_Handle(struct waiter *, struct waited *, enum wait_event, double now);
void Wait_UsePipe(struct waiter *w);

/* mgt_waiter.c */
extern struct waiter_impl const * waiter;

#if defined(HAVE_EPOLL_CTL)
extern const struct waiter_impl waiter_epoll;
#endif

#if defined(HAVE_KQUEUE)
extern const struct waiter_impl waiter_kqueue;
#endif

#if defined(HAVE_PORT_CREATE)
extern const struct waiter_impl waiter_ports;
#endif

extern const struct waiter_impl waiter_poll;
