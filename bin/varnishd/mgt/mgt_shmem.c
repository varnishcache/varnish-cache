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
 *
 * TODO:
 *
 * There is a risk that the child process might corrupt the VSM segment
 * and we should capture that event and recover gracefully.
 *
 * A possible state diagram could be:
 *
 *	[manager start]
 *		|
 *		v
 *      Open old VSM,
 *	check pid	--------> exit/fail (-n message)
 *		|
 *		+<----------------------+
 *		|			^
 *		v			|
 *	Create new VSM			|
 *		|			|
 *		v			|
 *	Init header			|
 *	Alloc VSL			|
 *	Alloc VSC:Main			|
 *	Alloc Args etc.			|
 *		|			|
 *		+<--------------+	|
 *		|		^	|
 *		v		|	|
 *	start worker		|	|
 *		|		|	|
 *		|		|	+<---- worker crash
 *		v		|	^
 *	Reset VSL ptr.		|	|
 *	Reset VSC counters	|	|
 *		|		|	|
 *		+<------+	|	|
 *		|	^	|	|
 *		v	|	|	|
 *	alloc dynamics	|	|	|
 *	free dynamics	|	|	|
 *		|	|	|	|
 *		v	|	|	|
 *		+------>+	|	|
 *		|		|	|
 *		v		|	|
 *	stop worker		|	|
 *		|		|	|
 *		v		|	|
 *	Check consist---------- | ----->+
 *		|		|
 *		v		|
 *	Free dynamics		|
 *		|		|
 *		v		|
 *		+-------------->+
 *
 */

#include "config.h"

#include <sys/mman.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <signal.h>
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
#include "vapi/vsl_int.h"
#include "vapi/vsm_int.h"
#include "vmb.h"

#ifndef MAP_HASSEMAPHORE
#define MAP_HASSEMAPHORE 0 /* XXX Linux */
#endif

#ifndef MAP_NOSYNC
#define MAP_NOSYNC 0 /* XXX Linux */
#endif

struct VSC_C_main	*VSC_C_main;

static int vsl_fd = -1;

/*--------------------------------------------------------------------
 * Check that we are not started with the same -n argument as an already
 * running varnishd
 */

static void
vsl_n_check(int fd)
{
	struct VSM_head slh;
	int i;
	struct stat st;
	pid_t pid;

	AZ(fstat(fd, &st));
	if (!S_ISREG(st.st_mode))
		ARGV_ERR("\tshmlog: Not a file\n");

	/* Test if the SHMFILE is locked by other Varnish */
	if (fltest(fd, &pid) > 0) {
		fprintf(stderr,
			"SHMFILE locked by running varnishd master (pid=%jd)\n",
			(intmax_t)pid);
		fprintf(stderr,
			"(Use unique -n arguments if you want multiple "
			"instances)\n");
		exit(2);
	}

	/* Read owning pid from SHMFILE */
	memset(&slh, 0, sizeof slh);	/* XXX: for flexelint */
	i = read(fd, &slh, sizeof slh);
	if (i != sizeof slh)
		return;
	if (slh.magic != VSM_HEAD_MAGIC)
		return;
	if (slh.hdrsize != sizeof slh)
		return;
	if (slh.master_pid != 0 && !kill(slh.master_pid, 0)) {
		fprintf(stderr,
			"WARNING: Taking over SHMFILE marked as owned by "
			"running process (pid=%jd)\n",
			(intmax_t)slh.master_pid);
	}
}

/*--------------------------------------------------------------------
 * Build a new shmlog file
 */

static void
vsl_buildnew(const char *fn, ssize_t size)
{
	int i;
	unsigned u;
	char buf[64*1024];
	int flags;

	(void)unlink(fn);
	vsl_fd = flopen(fn, O_RDWR | O_CREAT | O_EXCL | O_NONBLOCK, 0644);
	if (vsl_fd < 0) {
		fprintf(stderr, "Could not create %s: %s\n",
		    fn, strerror(errno));
		exit (1);
	}
	flags = fcntl(vsl_fd, F_GETFL);
	assert(flags != -1);
	flags &= ~O_NONBLOCK;
	AZ(fcntl(vsl_fd, F_SETFL, flags));

	memset(buf, 0, sizeof buf);
	for (u = 0; u < size; ) {
		i = write(vsl_fd, buf, sizeof buf);
		if (i <= 0) {
			fprintf(stderr, "Write error %s: %s\n",
			    fn, strerror(errno));
			exit (1);
		}
		u += i;
	}
	AZ(ftruncate(vsl_fd, (off_t)size));
}

/*--------------------------------------------------------------------
 * Exit handler that clears the owning pid from the SHMLOG
 */

static
void
mgt_shm_atexit(void)
{
#if 0
	if (getpid() == VSM_head->master_pid)
		VSM_head->master_pid = 0;
#endif
}

void
mgt_SHM_Init(void)
{
	int i, fill;
	uintmax_t size, ps;
	void *p;
#if 0
	uint32_t *vsl_log_start;
#endif

	fill = 1;

	size = mgt_param.vsl_space + mgt_param.vsm_space;
	ps = getpagesize();
	size += ps - 1;
	size &= ~(ps - 1);

	i = open(VSM_FILENAME, O_RDWR, 0644);
	if (i >= 0) {
		vsl_n_check(i);
		(void)close(i);
	}
	vsl_buildnew(VSM_FILENAME, size);

	p = (void *)mmap(NULL, size,
	    PROT_READ|PROT_WRITE,
	    MAP_HASSEMAPHORE | MAP_NOSYNC | MAP_SHARED,
	    vsl_fd, 0);
	xxxassert(p != MAP_FAILED);

	heritage.vsm = VSM_common_new(p, size);

	(void)mlock(p, size);
	AZ(atexit(mgt_shm_atexit));

	/* XXX: We need to zero params if we dealloc/clean/wash */
	cache_param = VSM_Alloc(sizeof *cache_param, VSM_CLASS_PARAM, "", "");
	AN(cache_param);
	*cache_param = mgt_param;

	PAN_panicstr_len = 64 * 1024;
	PAN_panicstr = VSM_Alloc(PAN_panicstr_len, PAN_CLASS, "", "");
	AN(PAN_panicstr);

#if 0

	VSM_head->master_pid = getpid();

	memset(&VSM_head->head, 0, sizeof VSM_head->head);
	VSM_head->head.magic = VSM_CHUNK_MAGIC;
	VSM_head->head.len =
	    (uint8_t*)(VSM_head) + size - (uint8_t*)&VSM_head->head;
	bprintf(VSM_head->head.class, "%s", VSM_CLASS_FREE);
	VWMB();

	vsm_end = (void*)((uint8_t*)VSM_head + size);

	VSC_C_main = VSM_Alloc(sizeof *VSC_C_main,
	    VSC_CLASS, VSC_TYPE_MAIN, "");
	AN(VSC_C_main);

	do
		VSM_head->alloc_seq = random();
	while (VSM_head->alloc_seq == 0);
#endif

}

void
mgt_SHM_Pid(void)
{

#if 0
	VSM_head->master_pid = getpid();
#endif
}
