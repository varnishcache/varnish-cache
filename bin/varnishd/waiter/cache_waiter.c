/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

#include "cache/cache.h"

#include "waiter/waiter.h"

struct waiter {
	unsigned			magic;
#define WAITER_MAGIC			0x17c399db
	const struct waiter_impl	*impl;
	void				*priv;
	int				pfd;
};

const char *
WAIT_GetName(void)
{

	if (waiter != NULL)
		return (waiter->name);
	else
		return ("no_waiter");
}

struct waiter *
WAIT_Init(waiter_handle_f *func)
{
	struct waiter *w;

	ALLOC_OBJ(w, WAITER_MAGIC);
	AN(w);
	w->pfd = -1;

	AN(waiter);
	AN(waiter->name);
	AN(waiter->init);
	w->impl = waiter;
	w->priv = w->impl->init(func, &w->pfd);
	AN(waiter->pass || w->pfd >= 0);
	return (w);
}

int
WAIT_Enter(const struct waiter *w, void *ptr, int fd)
{
	struct sess *sp;

	CHECK_OBJ_NOTNULL(w, WAITER_MAGIC);
	CAST_OBJ_NOTNULL(sp, ptr, SESS_MAGIC);
	assert(fd >= 0);
	assert(sp->fd >= 0);

	if (w->impl->pass != NULL)
		return (w->impl->pass(w->priv, sp));
	assert(w->pfd >= 0);
	return (WAIT_Write_Session(sp, w->pfd));
}

/*
 * We do not make sp a const, in order to hint that we actually do take
 * control of it.
 */
int __match_proto__()
WAIT_Write_Session(struct sess *sp, int fd)
{
	ssize_t written;
	written = write(fd, &sp, sizeof sp);
	if (written != sizeof sp && (errno == EAGAIN || errno == EWOULDBLOCK))
		return (-1);
	assert (written == sizeof sp);
	return (0);
}
