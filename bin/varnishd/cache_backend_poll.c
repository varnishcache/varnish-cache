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
 * $Id: cache_backend_cfg.c 2905 2008-07-08 10:09:03Z phk $
 *
 * Poll backends for collection of health statistics
 *
 * We co-opt threads from the worker pool for probing the backends,
 * but we want to avoid a potentially messy cleanup operation when we
 * retire the backend, so the thread owns the health information, which
 * the backend references, rather than the other way around.
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

#include <sys/socket.h>

#include "shmlog.h"
#include "cache.h"
#include "vrt.h"
#include "cache_backend.h"

struct vbp_target {
	unsigned			magic;
#define VBP_TARGET_MAGIC		0x6b7cb656

	struct backend			*backend;
	struct workreq			wrq;
	int				stop;
};

static void
vbp_wrk_poll_backend(struct worker *w, void *priv)
{
	struct vbp_target *vt;

	(void)w;
	CAST_OBJ_NOTNULL(vt, priv, VBP_TARGET_MAGIC);
	THR_Name("backend poll");

	while (!vt->stop) {
		printf("Poke backend %s\n", vt->backend->vcl_name);
		sleep(1);
	}
	vt->backend->probe = NULL;
	FREE_OBJ(vt);
	THR_Name("cache-worker");
}

void
VBP_Start(struct backend *b, struct vrt_backend_probe const *p)
{
	struct vbp_target *vt;

	ASSERT_CLI();

	/* Is probing even configured ? */
	if (p->request == NULL)
		return;

	ALLOC_OBJ(vt, VBP_TARGET_MAGIC);
	AN(vt);
	vt->backend = b;
	b->probe = vt;

	vt->wrq.func = vbp_wrk_poll_backend;
	vt->wrq.priv = vt;
	if (WRK_Queue(&vt->wrq) == 0)
		return;
	assert(0 == __LINE__);
	b->probe = NULL;
	FREE_OBJ(vt);
}

void
VBP_Stop(struct backend *b)
{
	if (b->probe == NULL)
		return;
	b->probe->stop = 1;
}
