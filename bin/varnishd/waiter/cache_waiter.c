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
#include <unistd.h>
#include <string.h>
#include <math.h>

#include "cache/cache.h"

#include "waiter/waiter.h"

#include "vtcp.h"

static void *waiter_priv;

const char *
WAIT_GetName(void)
{

	if (waiter != NULL)
		return (waiter->name);
	else
		return ("no_waiter");
}

void
WAIT_Init(void)
{

	AN(waiter);
	AN(waiter->name);
	AN(waiter->init);
	AN(waiter->pass);
	waiter_priv = waiter->init();
}

void
WAIT_Enter(struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	assert(sp->fd >= 0);

	/*
	* Set nonblocking in the worker-thread, before passing to the
	* acceptor thread, to reduce syscall density of the latter.
	*/
	if (VTCP_nonblocking(sp->fd))
		SES_Close(sp, SC_REM_CLOSE);
	waiter->pass(waiter_priv, sp);
}

void
WAIT_Write_Session(struct sess *sp, int fd)
{
	ssize_t written;
	written = write(fd, &sp, sizeof sp);
	if (written != sizeof sp && (errno == EAGAIN || errno == EWOULDBLOCK)) {
		VSC_C_main->sess_pipe_overflow++;
		SES_Delete(sp, SC_SESS_PIPE_OVERFLOW, NAN);
		return;
	}
	assert (written == sizeof sp);
}
