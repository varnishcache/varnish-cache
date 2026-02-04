/*-
 * Copyright (c) 2024 Varnish Software AS
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
 */

#include "config.h"

#include <stdlib.h>
#include <errno.h>

#include "cache_varnishd.h"

#include "vtim.h"
#include "vsync.h"

#include "VSC_lck.h"
#include "VSC_cond.h"

static void
mtx_sync_evt(const char *func, const char *file,
    int line, enum vsync_mtx_event evt, struct VSC_lck *vsc)
{
	AN(vsc);

	(void)func;
	(void)line;
	(void)file;


	switch (evt) {
	case VSYNC_MTX_INIT:
		__sync_fetch_and_add(&vsc->creat, 1);
		break;
	case VSYNC_MTX_FINI:
		__sync_fetch_and_add(&vsc->destroy, 1);
		break;
	case VSYNC_MTX_LOCK:
		__sync_fetch_and_add(&vsc->locks, 1);
		break;
	case VSYNC_MTX_UNLOCK:
		break;
	}
}

static void
cond_sync_evt(const char *func, const char *file, int line,
	enum vsync_cond_event evt, struct VSC_cond *vsc_cond,
	struct VSC_lck *vsc_mtx, double *start_atp)
{
	vtim_mono now;

	AN(vsc_cond);

	(void)func;
	(void)line;
	(void)file;
	(void)vsc_mtx;

	switch (evt) {
	case VSYNC_COND_INIT:
		__sync_fetch_and_add(&vsc_cond->init, 1);
		AZ(start_atp);
		break;
	case VSYNC_COND_FINI:
		__sync_fetch_and_add(&vsc_cond->fini, 1);
		AZ(start_atp);
		break;
	case VSYNC_COND_SIGNAL:
		__sync_fetch_and_add(&vsc_cond->signal, 1);
		AZ(start_atp);
		break;
	case VSYNC_COND_BROADCAST:
		__sync_fetch_and_add(&vsc_cond->broadcast, 1);
		AZ(start_atp);
		break;
	case VSYNC_COND_WAIT_START:
		__sync_fetch_and_add(&vsc_cond->waiting, 1);
		AN(start_atp);
		*start_atp = VTIM_mono();
		break;
	case VSYNC_COND_WAIT_END:
		AN(start_atp);
		now = VTIM_mono();
		assert(now > *start_atp);

		__sync_fetch_and_sub(&vsc_cond->waiting, 1);
		__sync_fetch_and_add(&vsc_cond->waits, 1);
		__sync_fetch_and_add(&vsc_cond->wait_duration_ns,
		    (uint64_t)((now - *start_atp) * 1e9));
		break;
	}
}

void
SYNC_Init(void)
{
	VSYNC_mtx_event_func = mtx_sync_evt;
	VSYNC_cond_event_func = cond_sync_evt;
}
