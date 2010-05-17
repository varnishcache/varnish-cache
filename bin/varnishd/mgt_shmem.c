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

static int vsl_fd = -1;
static unsigned vsl_size;

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
	vsl_size = slh.size + slh.start;
	return (1);
}

static void
vsl_buildnew(const char *fn, unsigned size, int fill)
{
	struct shmloghead slh;
	int i;
	unsigned u;
	char buf[64*1024];

	(void)unlink(fn);
	vsl_fd = open(fn, O_RDWR | O_CREAT | O_EXCL, 0644);
	if (vsl_fd < 0) {
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
	i = write(vsl_fd, &slh, sizeof slh);
	xxxassert(i == sizeof slh);
	vsl_size = slh.start + size;

	if (fill) {
		memset(buf, 0, sizeof buf);
		for (u = sizeof slh; u < size; ) {
			i = write(vsl_fd, buf, sizeof buf);
			if (i <= 0) {
				fprintf(stderr, "Write error %s: %s\n",
				    fn, strerror(errno));
				exit (1);
			}
			u += i;
		}
	}

	AZ(ftruncate(vsl_fd, (off_t)vsl_size));
}

void
mgt_SHM_Init(const char *fn, const char *l_arg)
{
	int i, fill;
	struct params *pp;
	const char *q;
	uintmax_t size, s1, s2;
	char **av, **ap;

	if (l_arg == NULL)
		l_arg = "";

	av = ParseArgv(l_arg, ARGV_COMMA);
	AN(av);
	if (av[0] != NULL) 
		ARGV_ERR("\t-l ...: %s", av[0]);

	printf("<%s> <%s> <%s>\n", av[1], av[2], av[3]);

	ap = av + 1;

	/* Size of SHMLOG */
	if (*ap != NULL && **ap != '\0') {
		q = str2bytes(*ap, &s1, 0);
		if (q != NULL)
			ARGV_ERR("\t-l[1] ...:  %s\n", q);
	} else {
		s1 = 80 * 1024 * 1024;
	}
	if (*ap != NULL)
		ap++;

	/* Size of space for other stuff */
	if (*ap != NULL && **ap != '\0') {
		q = str2bytes(*ap, &s2, 0);
		if (q != NULL)
			ARGV_ERR("\t-l[2] ...:  %s\n", q);
	} else {
		s2 = 1024 * 1024;
	}
	if (*ap != NULL)
		ap++;

	/* Fill or not ? */
	if (*ap != NULL) {
		if (*ap == '\0')
			fill = 1;
		else if (!strcmp(*ap, "-"))
			fill = 0;
		else if (!strcmp(*ap, "+"))
			fill = 1;
		else
			ARGV_ERR("\t-l[3] ...:  Must be \"-\" or \"+\"\n");
		ap++;
	} else {
		fill = 1;
	}

	FreeArgv(av);

	size = s1 + s2;

	if (av[2] == NULL)
		q = str2bytes(av[2], &size, 0);

	i = open(fn, O_RDWR, 0644);
	if (i >= 0 && vsl_goodold(i)) {
		fprintf(stderr, "Using old SHMFILE\n");
		vsl_fd = i;
	} else {
		fprintf(stderr, "Creating new SHMFILE\n");
		(void)close(i);
		vsl_buildnew(fn, size, fill);
	}

	loghead = (void *)mmap(NULL, vsl_size,
	    PROT_READ|PROT_WRITE,
	    MAP_HASSEMAPHORE | MAP_NOSYNC | MAP_SHARED,
	    vsl_fd, 0);
	loghead->master_pid = getpid();
	xxxassert(loghead != MAP_FAILED);
	(void)mlock((void*)loghead, vsl_size);
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
