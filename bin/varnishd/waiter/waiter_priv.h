/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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
struct vbh;
struct VSC_waiter;

struct waiter {
	unsigned			magic;
#define WAITER_MAGIC			0x17c399db
	const struct waiter_impl	*impl;
	VTAILQ_ENTRY(waiter)		list;
	VTAILQ_HEAD(,waited)		waithead;

	void				*priv;
	struct vbh			*heap;
	struct VSC_waiter		*vsc;
};

typedef void waiter_init_f(struct waiter *);
typedef void waiter_fini_f(struct waiter *);
typedef int waiter_enter_f(void *priv, struct waited *);
typedef void waiter_inject_f(const struct waiter *, struct waited *);
typedef void waiter_evict_f(const struct waiter *, struct waited *);

struct waiter_impl {
	const char			*name;
	waiter_init_f			*init;
	waiter_fini_f			*fini;
	waiter_enter_f			*enter;
	waiter_inject_f			*inject;
	size_t				size;
};

static inline double
Wait_When(const struct waited *wp)
{
	CHECK_OBJ_NOTNULL(wp, WAITED_MAGIC);
	return (wp->idle + wp->tmo);
}

void Wait_Call(const struct waiter *, struct waited *,
    enum wait_event ev, double now);
void Wait_HeapInsert(const struct waiter *, struct waited *);
int Wait_HeapDelete(const struct waiter *, const struct waited *);
double Wait_HeapDue(const struct waiter *, struct waited **);
