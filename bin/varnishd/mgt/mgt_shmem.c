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

#include <sys/mman.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mgt/mgt.h"
#include "common/heritage.h"
#include "common/common_vsm.h"

#include "vfl.h"
#include "vsm_priv.h"
#include "vfil.h"

#ifndef MAP_HASSEMAPHORE
#define MAP_HASSEMAPHORE 0 /* XXX Linux */
#endif

#ifndef MAP_NOSYNC
#define MAP_NOSYNC 0 /* XXX Linux */
#endif

#define PAN_CLASS "Panic"

static void *mgt_vsm_p;
static ssize_t mgt_vsm_l;

/*--------------------------------------------------------------------
 * Use a bogo-VSM to hold master-copies of the VSM chunks the master
 * publishes, such as -S & -T arguments.
 */

static struct vsm_sc *static_vsm;
static char static_vsm_buf[1024];

void
mgt_SHM_static_alloc(const void *ptr, ssize_t size,
    const char *class, const char *type, const char *ident)
{
	void *p;

	p = CVSM_alloc(static_vsm, size, class, type, ident);
	AN(p);
	memcpy(p, ptr, size);
	if (heritage.vsm != NULL) {
		p = CVSM_alloc(heritage.vsm, size, class, type, ident);
		AN(p);
		memcpy(p, ptr, size);
	}
}

/*--------------------------------------------------------------------
 * Build a zeroed file
 */

static int
vsm_zerofile(const char *fn, ssize_t size)
{
	int fd;
	int flags;

	fd = VFL_Open(fn, O_RDWR | O_CREAT | O_EXCL | O_NONBLOCK, 0640);
	if (fd < 0) {
		MGT_Complain(C_ERR, "Could not create %s: %s",
		    fn, strerror(errno));
		return (-1);
	}
	VJ_fix_vsm_file(fd);
	flags = fcntl(fd, F_GETFL);
	assert(flags != -1);
	flags &= ~O_NONBLOCK;
	AZ(fcntl(fd, F_SETFL, flags));
	if (VFIL_allocate(fd, (off_t)size, 1)) {
		MGT_Complain(C_ERR, "File allocation error %s: %s",
		    fn, strerror(errno));
		return (-1);
	}
	return (fd);
}

/*--------------------------------------------------------------------
 * Create a VSM instance
 */

static size_t
mgt_shm_size(void)
{
	size_t size, ps;

	size = mgt_param.vsl_space + mgt_param.vsm_space;
	ps = getpagesize();
	size = RUP2(size, ps);
	return (size);
}

static void
mgt_shm_cleanup(void)
{
	char fnbuf[64];

	bprintf(fnbuf, "%s.%jd", VSM_FILENAME, (intmax_t)getpid());
	VJ_master(JAIL_MASTER_FILE);
	(void)unlink(fnbuf);
	VJ_master(JAIL_MASTER_LOW);
}

void
mgt_SHM_Create(void)
{
	size_t size;
	void *p;
	char fnbuf[64];
	int vsm_fd;

	AZ(heritage.vsm);
	size = mgt_shm_size();

	bprintf(fnbuf, "%s.%jd", VSM_FILENAME, (intmax_t)getpid());

	VJ_master(JAIL_MASTER_FILE);
	vsm_fd = vsm_zerofile(fnbuf, size);
	VJ_master(JAIL_MASTER_LOW);
	if (vsm_fd < 0) {
		mgt_shm_cleanup();
		exit(1);
	}

	p = (void *)mmap(NULL, size,
	    PROT_READ|PROT_WRITE,
	    MAP_HASSEMAPHORE | MAP_NOSYNC | MAP_SHARED,
	    vsm_fd, 0);

	closefd(&vsm_fd);

	if (p == MAP_FAILED) {
		MGT_Complain(C_ERR, "Mmap error %s: %s",
		    fnbuf, strerror(errno));
		mgt_shm_cleanup();
		exit(1);
	}

	mgt_vsm_p = p;
	mgt_vsm_l = size;

	/* This may or may not work */
	(void)mlock(p, size);

	heritage.vsm = CVSM_new(p, size);

	CVSM_copy(heritage.vsm, static_vsm);

	heritage.param = CVSM_alloc(heritage.vsm,
	    sizeof *heritage.param, VSM_CLASS_PARAM, "", "");
	AN(heritage.param);
	*heritage.param = mgt_param;

	heritage.panic_str_len = 64 * 1024;
	heritage.panic_str = CVSM_alloc(heritage.vsm,
	    heritage.panic_str_len, PAN_CLASS, "", "");
	AN(heritage.panic_str);

	/* Copy management counters to shm and update pointer */
	VSC_C_mgt = VSC_mgt_New("");
	AN(VSC_C_mgt);
	*VSC_C_mgt = static_VSC_C_mgt;

#ifdef __OpenBSD__
	/* Commit changes, for OS's without coherent VM/buf */
	AZ(msync(p, getpagesize(), MS_SYNC));
#endif
	VJ_master(JAIL_MASTER_FILE);
	if (rename(fnbuf, VSM_FILENAME)) {
		MGT_Complain(C_ERR, "Rename failed %s -> %s: %s",
		    fnbuf, VSM_FILENAME, strerror(errno));
		(void)unlink(fnbuf);
		exit(1);
	}
	VJ_master(JAIL_MASTER_LOW);
}

/*--------------------------------------------------------------------
 * Destroy a VSM instance
 */

void
mgt_SHM_Destroy(int keep)
{

	/* Point mgt counters back at static version */
	VSC_C_mgt = &static_VSC_C_mgt;

	AN(heritage.vsm);
	if (keep)
		(void)rename(VSM_FILENAME, VSM_FILENAME ".keep");
	heritage.panic_str = NULL;
	heritage.panic_str_len = 0;
	heritage.param = NULL;
	CVSM_delete(&heritage.vsm);
	AZ(munmap(mgt_vsm_p, mgt_vsm_l));
	mgt_vsm_p = NULL;
	mgt_vsm_l = 0;
}

/*--------------------------------------------------------------------
 * Destroy and recreate VSM if its size should change
 */

void
mgt_SHM_Size_Adjust(void)
{

	AN(heritage.vsm);
	if (mgt_vsm_l == mgt_shm_size())
		return;
	mgt_SHM_Destroy(0);
	mgt_SHM_Create();
}

/*--------------------------------------------------------------------
 * Exit handler that clears the owning pid from the SHMLOG
 */

static void
mgt_shm_atexit(void)
{

	/* Do not let VCC kill our VSM */
	if (getpid() != mgt_pid)
		return;
	if (heritage.vsm != NULL)
		CVSM_delete(&heritage.vsm);
}

/*--------------------------------------------------------------------
 * Initialize VSM subsystem
 */

void
mgt_SHM_Init(void)
{
	/* Create our static VSM instance */
	static_vsm = CVSM_new(static_vsm_buf, sizeof static_vsm_buf);

	/* Setup atexit handler */
	AZ(atexit(mgt_shm_atexit));
}
