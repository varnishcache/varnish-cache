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
#include <sys/stat.h>

#include "shmlog.h"
#include "mgt.h"
#include "heritage.h"
#include "vmb.h"

#ifndef MAP_HASSEMAPHORE
#define MAP_HASSEMAPHORE 0 /* XXX Linux */
#endif

#ifndef MAP_NOSYNC
#define MAP_NOSYNC 0 /* XXX Linux */
#endif

struct varnish_stats	*VSL_stats;
struct shmloghead	*loghead;
uint8_t			*vsl_log_start;
uint8_t			*vsl_log_end;
uint8_t			*vsl_log_nxt;

static int vsl_fd = -1;

/*--------------------------------------------------------------------*/

void *
mgt_SHM_Alloc(unsigned size, const char *class, const char *type, const char *ident)
{
	struct shmalloc *sha, *sha2;
	unsigned seq;

	ASSERT_MGT();
	AN(loghead);
	/* Round up to pointersize */
	size += sizeof(sha) - 1;
	size &= ~(sizeof(sha) - 1);

	size += sizeof *sha;
	sha = &loghead->head;
	while (1) {
		CHECK_OBJ_NOTNULL(sha, SHMALLOC_MAGIC);

		if (strcmp(sha->class, "Free")) {
			sha = SHA_NEXT(sha);
			continue;
		}
		assert(size <= sha->len);

		sha2 = (void*)((uintptr_t)sha + size);

		seq = loghead->alloc_seq;
		loghead->alloc_seq = 0;
		VWMB();

		memset(sha2, 0, sizeof *sha2);
		sha2->magic = SHMALLOC_MAGIC;
		sha2->len = sha->len - size;
		bprintf(sha2->class, "%s", "Free");

		sha->len = size;
		bprintf(sha->class, "%s", class);
		bprintf(sha->type, "%s", type);
		bprintf(sha->ident, "%s", ident);

		VWMB();
		if (seq != 0) 
			do
				loghead->alloc_seq = seq++;
			while (loghead->alloc_seq == 0);

		return (SHA_PTR(sha));
	}
	return (NULL);
}

/*--------------------------------------------------------------------
 * Try to reuse an existing shmem file, but try to not disturb another
 * varnishd using the file.
 */

static int
vsl_goodold(int fd, unsigned size)
{
	struct shmloghead slh;
	int i;
	struct stat st;

	AZ(fstat(fd, &st));
	if (!S_ISREG(st.st_mode))
		ARGV_ERR("\tshmlog: Not a file\n");

	memset(&slh, 0, sizeof slh);	/* XXX: for flexelint */
	i = read(fd, &slh, sizeof slh);
	if (i != sizeof slh)
		return (0);
	if (slh.magic != SHMLOGHEAD_MAGIC)
		return (0);
	if (slh.hdrsize != sizeof slh)
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

	/* Sanity checks */

	if (slh.shm_size != size)
		return (0);

	if (st.st_size != size)
		return (0);

	return (1);
}

/*--------------------------------------------------------------------
 * Build a new shmlog file
 */

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
		fprintf(stderr, "Could not create %s: %s\n",
		    fn, strerror(errno));
		exit (1);
	}

	memset(&slh, 0, sizeof slh);
	slh.magic = SHMLOGHEAD_MAGIC;
	slh.hdrsize = sizeof slh;
	slh.shm_size = size;
	i = write(vsl_fd, &slh, sizeof slh);
	xxxassert(i == sizeof slh);

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

	AZ(ftruncate(vsl_fd, (off_t)size));
}

void
mgt_SHM_Init(const char *fn, const char *l_arg)
{
	int i, fill;
	struct params *pp;
	const char *q;
	uintmax_t size, s1, s2, ps;
	char **av, **ap;

	if (l_arg == NULL)
		l_arg = "";

	av = ParseArgv(l_arg, ARGV_COMMA);
	AN(av);
	if (av[0] != NULL) 
		ARGV_ERR("\t-l ...: %s", av[0]);

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
		if (**ap == '\0')
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

	if (*ap != NULL) 
		ARGV_ERR("\t-l ...:  Too many sub-args\n");

	FreeArgv(av);

	size = s1 + s2;
	ps = getpagesize();
	size += ps - 1;
	size &= ~(ps - 1);

	i = open(fn, O_RDWR, 0644);
	if (i >= 0 && vsl_goodold(i, size)) {
		fprintf(stderr, "Using old SHMFILE\n");
		vsl_fd = i;
	} else {
		fprintf(stderr, "Creating new SHMFILE\n");
		(void)close(i);
		vsl_buildnew(fn, size, fill);
	}

	loghead = (void *)mmap(NULL, size,
	    PROT_READ|PROT_WRITE,
	    MAP_HASSEMAPHORE | MAP_NOSYNC | MAP_SHARED,
	    vsl_fd, 0);
	loghead->master_pid = getpid();
	xxxassert(loghead != MAP_FAILED);
	(void)mlock((void*)loghead, size);

	/* Initialize pool */
	loghead->alloc_seq = 0;			/* Zero means "inconsistent" */
	VWMB();

	memset(&loghead->head, 0, sizeof loghead->head);
	loghead->head.magic = SHMALLOC_MAGIC;
	loghead->head.len =
	    (uint8_t*)(loghead) + size - (uint8_t*)&loghead->head;
	bprintf(loghead->head.class, "%s", "Free");
	VWMB();

	VSL_stats = mgt_SHM_Alloc(sizeof *VSL_stats,
	    VSL_CLASS_STAT, VSL_TYPE_STAT, "");
	AN(VSL_stats);

	pp = mgt_SHM_Alloc(sizeof *pp, "Params", "", "");
	AN(pp);
	*pp = *params;
	params = pp;

	vsl_log_start = mgt_SHM_Alloc(s1, VSL_CLASS_LOG, "", "");
	AN(vsl_log_start);
	vsl_log_end = vsl_log_start + s1;
	vsl_log_nxt = vsl_log_start + 1;
	*vsl_log_nxt = SLT_ENDMARKER;
	VWMB();

	do
		*vsl_log_start = random();
	while (*vsl_log_start == 0);

	do
		loghead->alloc_seq = random();
	while (loghead->alloc_seq == 0);

}

void
mgt_SHM_Pid(void)
{

	loghead->master_pid = getpid();
}
