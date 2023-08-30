/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * Derived from:
 * $FreeBSD: head/lib/libutil/pidfile.c 184091 2008-10-20 17:41:08Z des $
 */

#include "config.h"

#include <sys/param.h>
#include <sys/file.h>
#include <sys/stat.h>

#include <fcntl.h>	//lint !e537
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vdef.h"

#include "vas.h"	// XXX Flexelint "not used" - but req'ed for assert()
#include "vfl.h"
#include "vpf.h"

struct vpf_fh {
	int	pf_fd;
	char	*pf_path;
	dev_t	pf_dev;
	ino_t	pf_ino;
};

static int
vpf_verify(const struct vpf_fh *pfh)
{
	struct stat sb;

	if (pfh == NULL || pfh->pf_fd == -1)
		return (EINVAL);
	/*
	 * Check remembered descriptor.
	 */
	if (fstat(pfh->pf_fd, &sb) == -1)
		return (errno);
	if (sb.st_dev != pfh->pf_dev || sb.st_ino != pfh->pf_ino)
		return (EINVAL);
	return (0);
}

int
VPF_Read(const char *path, pid_t *pidptr)
{
	char buf[16], *endptr;
	int error, fd, i;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd == -1)
		return (errno);

	i = read(fd, buf, sizeof(buf) - 1);
	error = errno;
	closefd(&fd);
	if (i == -1)
		return (error);
	else if (i == 0)
		return (EAGAIN);
	if (i > 0 && buf[i - 1] == '\n')
		i--;
	buf[i] = '\0';

	*pidptr = strtol(buf, &endptr, 10);
	if (endptr != &buf[i])
		return (EINVAL);

	return (0);
}

struct vpf_fh *
VPF_Open(const char *path, mode_t mode, pid_t *pidptr)
{
	struct vpf_fh *pfh;
	struct stat sb;
	int fd;

	/*
	 * Open the PID file and obtain exclusive lock.
	 * We truncate PID file here only to remove old PID immediately,
	 * PID file will be truncated again in VPF_Write(), so
	 * VPF_Write() can be called multiple times.
	 */
	fd = VFL_Open(path,
	    O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NONBLOCK, mode);
	if (fd == -1) {
		if (errno == EWOULDBLOCK && pidptr != NULL) {
			errno = VPF_Read(path, pidptr);
			if (errno == 0)
				errno = EEXIST;
		}
		return (NULL);
	}

	/*
	 * Remember file information, so in VPF_Write() we are sure we write
	 * to the proper descriptor.
	 */
	AZ(fstat(fd, &sb));

	pfh = malloc(sizeof(*pfh));
	AN(pfh);
	pfh->pf_path = strdup(path);
	AN(pfh->pf_path);

	pfh->pf_fd = fd;
	pfh->pf_dev = sb.st_dev;
	pfh->pf_ino = sb.st_ino;

	return (pfh);
}

void
VPF_Write(const struct vpf_fh *pfh)
{
	char pidstr[16];

	/*
	 * Check remembered descriptor, so we don't overwrite some other
	 * file if pidfile was closed and descriptor reused.
	 */
	if (vpf_verify(pfh) != 0)
		return;

	/*
	 * Truncate PID file, so multiple calls of VPF_Write() are allowed.
	 */
	AZ(ftruncate(pfh->pf_fd, 0));

	bprintf(pidstr, "%jd", (intmax_t)getpid());
	assert(pwrite(pfh->pf_fd, pidstr, strlen(pidstr), 0) ==
	    (ssize_t)strlen(pidstr));
}

void
VPF_Remove(struct vpf_fh *pfh)
{
	if (vpf_verify(pfh) == 0) {
		(void)unlink(pfh->pf_path);
		closefd(&pfh->pf_fd);
	}
	free(pfh->pf_path);
	free(pfh);
}
