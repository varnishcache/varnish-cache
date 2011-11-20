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
#include <sys/stat.h>

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mgt/mgt.h"
#include "common/heritage.h"
#include "common/params.h"

#include "flopen.h"
#include "vapi/vsc_int.h"
#include "vapi/vsm_int.h"
#include "vmb.h"

#ifndef MAP_HASSEMAPHORE
#define MAP_HASSEMAPHORE 0 /* XXX Linux */
#endif

#ifndef MAP_NOSYNC
#define MAP_NOSYNC 0 /* XXX Linux */
#endif

static int vsm_fd = -1;

/*--------------------------------------------------------------------
 * Check that we are not started with the same -n argument as an already
 * running varnishd.
 *
 * Non-zero return means we should exit and not trample the file.
 *
 */

static int
vsm_n_check(void)
{
	int fd, i;
	struct stat st;
	pid_t pid;
	struct VSM_head vsmh;
	int retval = 2;

	fd = open(VSM_FILENAME, O_RDWR, 0644);
	if (fd < 0)
		return (0);

	AZ(fstat(fd, &st));
	if (!S_ISREG(st.st_mode)) {
		fprintf(stderr,
		    "VSM (%s) not a regular file.\n", VSM_FILENAME);
	} else {
		i = fltest(fd, &pid);
		if (i < 0) {
			fprintf(stderr,
			    "Cannot determine locking status of VSM (%s)\n.",
			    VSM_FILENAME);
		} else if (i == 0) {
			/*
			 * File is unlocked, mark it as dead, to help any
			 * consumers still stuck on it.
			 */
			if (pread(fd, &vsmh, sizeof vsmh, 0) == sizeof vsmh) {
				vsmh.alloc_seq = 0;
				(void)pwrite(fd, &vsmh, sizeof vsmh, 0);
			}
			retval = 0;
		} else {
			/* The VSM is locked, we won't touch it. */
			fprintf(stderr,
			    "VSM locked by running varnishd master (pid=%jd)\n"
			    "(Use unique -n arguments if you want"
			    "  multiple instances)\n", (intmax_t)pid);
		}
	}
	(void)close(fd);
	return (retval);
}

/*--------------------------------------------------------------------
 * Build a zeroed file
 */

static int
vsm_zerofile(const char *fn, ssize_t size)
{
	int fd;
	ssize_t i, u;
	char buf[64*1024];
	int flags;

	fd = flopen(fn, O_RDWR | O_CREAT | O_EXCL | O_NONBLOCK, 0644);
	if (fd < 0) {
		fprintf(stderr, "Could not create %s: %s\n",
		    fn, strerror(errno));
		return (-1);
	}
	flags = fcntl(fd, F_GETFL);
	assert(flags != -1);
	flags &= ~O_NONBLOCK;
	AZ(fcntl(fd, F_SETFL, flags));

	memset(buf, 0, sizeof buf);
	for (u = 0; u < size; ) {
		i = write(fd, buf, sizeof buf);
		if (i <= 0) {
			fprintf(stderr, "Write error %s: %s\n",
			    fn, strerror(errno));
			return (-1);
		}
		u += i;
	}
	AZ(ftruncate(fd, (off_t)size));
	return (fd);
}

/*--------------------------------------------------------------------
 * Exit handler that clears the owning pid from the SHMLOG
 */

static
void
mgt_shm_atexit(void)
{

	if (heritage.vsm != NULL)
		VSM_common_delete(&heritage.vsm);
}

void
mgt_SHM_Init(void)
{
	int i;
	uintmax_t size, ps;
	void *p;
	char fnbuf[64];

	size = mgt_param.vsl_space + mgt_param.vsm_space;
	ps = getpagesize();
	size += ps - 1;
	size &= ~(ps - 1);

	/* Collision check with already running varnishd */
	i = vsm_n_check();
	if (i)
		exit(i);

	bprintf(fnbuf, "%s.%jd", VSM_FILENAME, (intmax_t)getpid());

	vsm_fd = vsm_zerofile(fnbuf, size);
	if (vsm_fd < 0)
		exit(1);

	p = (void *)mmap(NULL, size,
	    PROT_READ|PROT_WRITE,
	    MAP_HASSEMAPHORE | MAP_NOSYNC | MAP_SHARED,
	    vsm_fd, 0);

	if (p == MAP_FAILED) {
		fprintf(stderr, "Mmap error %s: %s\n", fnbuf, strerror(errno));
		exit (-1);
	}

	/* This may or may not work */
	(void)mlock(p, size);

	heritage.vsm = VSM_common_new(p, size);

	if (rename(fnbuf, VSM_FILENAME)) {
		fprintf(stderr, "Rename failed %s -> %s: %s\n",
		    fnbuf, VSM_FILENAME, strerror(errno));
		(void)unlink(fnbuf);
		exit (-1);
	}

	AZ(atexit(mgt_shm_atexit));

	heritage.param =
	    VSM_Alloc(sizeof *heritage.param, VSM_CLASS_PARAM, "", "");
	AN(heritage.param);
	*heritage.param = mgt_param;

	PAN_panicstr_len = 64 * 1024;
	PAN_panicstr = VSM_Alloc(PAN_panicstr_len, PAN_CLASS, "", "");
	AN(PAN_panicstr);
}
