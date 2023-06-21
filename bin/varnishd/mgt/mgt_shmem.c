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
 */

#include "config.h"

#include <sys/stat.h>

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mgt/mgt.h"

#include "vsm_priv.h"

#include "common/heritage.h"
#include "common/vsmw.h"

static struct vsmw	*mgt_vsmw;
static void 		*mgt_traffic;

/*--------------------------------------------------------------------
 */

void
mgt_SHM_static_alloc(const void *ptr, ssize_t size,
    const char *category, const char *ident)
{
	void *p;

	p = VSMW_Allocf(mgt_vsmw, NULL, category, size, "%s", ident);
	AN(p);
	memcpy(p, ptr, size);
}

/*--------------------------------------------------------------------
 * Exit handler that clears the owning pid from the SHMLOG
 */

static void
mgt_shm_atexit(void)
{

	/* Do not let VCC kill our VSM */
	if (getpid() != heritage.mgt_pid)
		return;
	VJ_master(JAIL_MASTER_FILE);
	VSMW_Destroy(&mgt_vsmw);
	if (!MGT_DO_DEBUG(DBG_VTC_MODE)) {
		VJ_master(JAIL_MASTER_SYSTEM);
		AZ(system("rm -rf " VSM_MGT_DIRNAME));
		AZ(system("rm -rf " VSM_CHILD_DIRNAME));
	}
	VJ_master(JAIL_MASTER_LOW);
}

/*--------------------------------------------------------------------
 * Initialize VSM subsystem
 */

void
mgt_SHM_Init(void)
{
	int fd;

	VJ_master(JAIL_MASTER_SYSTEM);
	AZ(system("rm -rf " VSM_MGT_DIRNAME));
	VJ_master(JAIL_MASTER_FILE);
	AZ(mkdir(VSM_MGT_DIRNAME, 0755));
	fd = open(VSM_MGT_DIRNAME, O_RDONLY);
	VJ_fix_fd(fd, JAIL_FIXFD_VSMMGT);
	VJ_master(JAIL_MASTER_LOW);
	mgt_vsmw = VSMW_New(fd, 0640, "_.index");
	AN(mgt_vsmw);

	heritage.proc_vsmw = mgt_vsmw;

	/* Setup atexit handler */
	AZ(atexit(mgt_shm_atexit));
}

void
mgt_SHM_ChildNew(void)
{
	struct listen_sock *ls;
	uint64_t *nonce;
	unsigned n;

	VJ_master(JAIL_MASTER_SYSTEM);
	AZ(system("rm -rf " VSM_CHILD_DIRNAME));
	VJ_master(JAIL_MASTER_FILE);
	AZ(mkdir(VSM_CHILD_DIRNAME, 0750));

	heritage.vsm_fd = open(VSM_CHILD_DIRNAME, O_RDONLY);
	assert(heritage.vsm_fd >= 0);
	VJ_fix_fd(heritage.vsm_fd, JAIL_FIXFD_VSMWRK);
	VJ_master(JAIL_MASTER_LOW);

	MCH_Fd_Inherit(heritage.vsm_fd, "VSMW");

	heritage.param = VSMW_Allocf(mgt_vsmw, NULL, VSM_CLASS_PARAM,
	    sizeof *heritage.param, "");
	AN(heritage.param);
	*heritage.param = mgt_param;

	heritage.panic_str_len = 64 * 1024;
	heritage.panic_str = VSMW_Allocf(mgt_vsmw, NULL, "Panic",
	    heritage.panic_str_len, "");
	AN(heritage.panic_str);

	n = 0;
	VTAILQ_FOREACH(ls, &heritage.socks, list)
		n++;
	mgt_traffic = VSMW_Allocf(mgt_vsmw, NULL, "Traffic",
	    n * sizeof *nonce, "");
	AN(mgt_traffic);

	nonce = mgt_traffic;
	VTAILQ_FOREACH(ls, &heritage.socks, list)
		ls->nonce = nonce++;
}

void
mgt_SHM_ChildDestroy(void)
{

	closefd(&heritage.vsm_fd);
	if (!MGT_DO_DEBUG(DBG_VTC_MODE)) {
		VJ_master(JAIL_MASTER_SYSTEM);
		AZ(system("rm -rf " VSM_CHILD_DIRNAME));
		VJ_master(JAIL_MASTER_LOW);
	}
	VSMW_Free(mgt_vsmw, (void**)&mgt_traffic);
	VSMW_Free(mgt_vsmw, (void**)&heritage.panic_str);
	VSMW_Free(mgt_vsmw, (void**)&heritage.param);
}
