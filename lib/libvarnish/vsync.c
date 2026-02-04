/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2019 Varnish Software AS
 * All rights reserved.
 *
 * Author: Stephane Cance <stephane.cance@varnish-software.com>
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
 * Varnish Synchronisation devices.
 */
#include "config.h"

#include <stdlib.h>
#include <time.h>
#include <errno.h>

#include "vdef.h"
#include "vas.h"
#include "vtim.h"
#define VSYNC_KEEP_ASSERTS
#include "vsync.h"

vsync_mtx_event_f *VSYNC_mtx_event_func = NULL;
vsync_cond_event_f *VSYNC_cond_event_func = NULL;

void
VSYNC_cond_clock_init(pthread_cond_t *cond, clockid_t *idp, const char *func,
    const char *file, int line)
{
	pthread_condattr_t attr;

	VSYNC_assert(cond != NULL);
	VSYNC_assert(idp != NULL);

	VSYNC_PTOK(pthread_condattr_init(&attr));
#ifdef HAVE_PTHREAD_CONDATTR_SETCLOCK
	VSYNC_PTOK(pthread_condattr_setclock(&attr, *idp));
#else
	*idp = CLOCK_REALTIME;
#endif /* HAVE_PTHREAD_CONDATTR_SETCLOCK */
	VSYNC_PTOK(pthread_cond_init(cond, &attr));
	VSYNC_PTOK(pthread_condattr_destroy(&attr));
}
