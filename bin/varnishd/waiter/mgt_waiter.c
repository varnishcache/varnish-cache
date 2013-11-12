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
 */

#include "config.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "common/common.h"

#include "waiter/waiter.h"

static const struct waiter *const vca_waiters[] = {
    #if defined(HAVE_KQUEUE)
	&waiter_kqueue,
    #endif
    #if defined(HAVE_EPOLL_CTL)
	&waiter_epoll,
    #endif
    #if defined(HAVE_PORT_CREATE)
	&waiter_ports,
    #endif
	&waiter_poll,
	NULL,
};

struct waiter const *waiter;

int
WAIT_tweak_waiter(struct vsb *vsb, const char *arg)
{
	int i;

	ASSERT_MGT();

	if (arg == NULL) {
		if (waiter == NULL)
			VSB_printf(vsb, "default");
		else
			VSB_printf(vsb, "%s", waiter->name);

		VSB_printf(vsb, " (possible values: ");
		for (i = 0; vca_waiters[i] != NULL; i++)
			VSB_printf(vsb, "%s%s", i == 0 ? "" : ", ",
			    vca_waiters[i]->name);
		VSB_printf(vsb, ")");
		return(0);
	}
	if (!strcmp(arg, WAITER_DEFAULT)) {
		waiter = vca_waiters[0];
		return(0);
	}
	for (i = 0; vca_waiters[i]; i++) {
		if (!strcmp(arg, vca_waiters[i]->name)) {
			waiter = vca_waiters[i];
			return(0);
		}
	}
	VSB_printf(vsb, "Unknown waiter");
	return (-1);
}
