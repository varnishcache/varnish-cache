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
 * Waiters are herders of connections:  They monitor a large number of
 * connections and react if data arrives, the connection is closed or
 * if nothing happens for a specified timeout period.
 *
 * The "poll" waiter should be portable to just about anything, but it
 * is not very efficient because it has to setup state on each call to
 * poll(2).  Almost all kernels have made better facilities for that
 * reason, needless to say, each with its own NIH-controlled API:
 *
 * - kqueue on FreeBSD
 * - epoll on Linux
 * - ports on Solaris
 */

struct waited;
struct waiter;

enum wait_event {
	WAITER_REMCLOSE,
	WAITER_TIMEOUT,
	WAITER_ACTION
};

typedef void waiter_handle_f(struct waited *, enum wait_event, double now);
typedef void* waiter_init_f(waiter_handle_f *, int *, volatile double *);
typedef int waiter_pass_f(void *priv, struct waited *);

#define WAITER_DEFAULT		"platform dependent"

struct waiter_impl {
	const char		*name;
	waiter_init_f		*init;
	waiter_pass_f		*pass;
};

/* cache_waiter.c */
int WAIT_Enter(const struct waiter *, struct waited *);
struct waiter *WAIT_Init(waiter_handle_f *, volatile double *timeout);
const char *WAIT_GetName(void);

/* mgt_waiter.c */
extern struct waiter_impl const * waiter;
int WAIT_tweak_waiter(struct vsb *vsb, const char *arg);

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
