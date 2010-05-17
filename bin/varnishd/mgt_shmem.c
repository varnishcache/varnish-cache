/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Redpill Linpro AS
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
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>

#include "shmlog.h"
#include "mgt.h"
#include "heritage.h"

#ifndef MAP_HASSEMAPHORE
#define MAP_HASSEMAPHORE 0 /* XXX Linux */
#endif

#ifndef MAP_NOSYNC
#define MAP_NOSYNC 0 /* XXX Linux */
#endif

struct varnish_stats *VSL_stats;
struct shmloghead *loghead;
unsigned char *logstart;


/*--------------------------------------------------------------------*/

static int
vsl_goodold(int fd)
{
	struct shmloghead slh;
	int i;

	memset(&slh, 0, sizeof slh);	/* XXX: for flexelint */
	i = read(fd, &slh, sizeof slh);
	if (i != sizeof slh)
		return (0);
	if (slh.magic != SHMLOGHEAD_MAGIC)
		return (0);
	if (slh.hdrsize != sizeof slh)
		return (0);
	if (slh.start != sizeof slh + sizeof *params)
		return (0);

	if (slh.master_pid != 0 && !kill(slh.master_pid, 0)) {
		fprintf(stderr,
		    "SHMFILE owned by running varnishd master (pid=%jd)\n",
		    (intmax_t)slh.master_pid);
		fprintf(stderr,
		    "(Use unique -n arguments if you want multiple "
		    "instances.)\n");
		exit(2);
	}

	if (slh.child_pid != 0 && !kill(slh.child_pid, 0)) {
		fprintf(stderr,
		    "SHMFILE used by orphan varnishd child process (pid=%jd)\n",
		    (intmax_t)slh.child_pid);
		fprintf(stderr, "(We assume that process is busy dying.)\n");
		return (0);
	}

	/* XXX more checks */
	heritage.vsl_size = slh.size + slh.start;
	return (1);
}

static void
vsl_buildnew(const char *fn, unsigned size)
{
	struct shmloghead slh;
	int i;

	(void)unlink(fn);
	heritage.vsl_fd = open(fn, O_RDWR | O_CREAT | O_EXCL, 0644);
	if (heritage.vsl_fd < 0) {
		fprintf(stderr, "Could not open %s: %s\n",
		    fn, strerror(errno));
		exit (1);
	}

	memset(&slh, 0, sizeof slh);
	slh.magic = SHMLOGHEAD_MAGIC;
	slh.hdrsize = sizeof slh;
	slh.size = size;
	slh.ptr = 0;
	slh.start = sizeof slh + sizeof *params;
	i = write(heritage.vsl_fd, &slh, sizeof slh);
	xxxassert(i == sizeof slh);
	heritage.vsl_size = slh.start + size;
	AZ(ftruncate(heritage.vsl_fd, (off_t)heritage.vsl_size));
}

void
mgt_SHM_Init(const char *fn, const char *l_arg)
{
	int i;
	struct params *pp;
	const char *arg_default = "80m";
	const char *q;
	uintmax_t size;

	if (l_arg == NULL)
		l_arg = arg_default;

	q = str2bytes(l_arg, &size, 0);
	if (q != NULL) {
		fprintf(stderr,  "Parameter error:\n");
		fprintf(stderr, "\t-l ...:  %s\n", q);
		exit (1);
	}

	i = open(fn, O_RDWR, 0644);
	if (i >= 0 && vsl_goodold(i)) {
		fprintf(stderr, "Using old SHMFILE\n");
		heritage.vsl_fd = i;
	} else {
		fprintf(stderr, "Creating new SHMFILE\n");
		(void)close(i);
		vsl_buildnew(fn, size);
	}

	loghead = (void *)mmap(NULL, heritage.vsl_size,
	    PROT_READ|PROT_WRITE,
	    MAP_HASSEMAPHORE | MAP_NOSYNC | MAP_SHARED,
	    heritage.vsl_fd, 0);
	loghead->master_pid = getpid();
	xxxassert(loghead != MAP_FAILED);
	(void)mlock((void*)loghead, heritage.vsl_size);
	VSL_stats = &loghead->stats;
	pp = (void *)(loghead + 1);
	*pp = *params;
	params = pp;
}

void
mgt_SHM_Pid(void)
{

	loghead->master_pid = getpid();
}
