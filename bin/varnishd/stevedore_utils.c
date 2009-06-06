/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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
 * Utility functions for stevedores and storage modules
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/stat.h>

#ifdef HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#endif

#ifdef HAVE_SYS_STATVFS_H
#include <sys/statvfs.h>
#endif

#ifdef HAVE_SYS_VFS_H
#include <sys/vfs.h>
#endif

#include "mgt.h"
#include "stevedore.h"

#ifndef O_LARGEFILE
#define O_LARGEFILE	0
#endif

/*--------------------------------------------------------------------
 * Get a storage file.
 *
 * The fn argument can be an existing file, an existing directory or
 * a nonexistent filename in an existing directory.
 *
 * If a directory is specified, the file will be anonymous (unlinked)
 *
 * Return:
 *	 0 if the file was preexisting.
 *	 1 if the file was created.
 *	 2 if the file is anonymous.
 *
 * Uses ARGV_ERR to exit in case of trouble.
 */

int
STV_GetFile(const char *fn, int *fdp, const char **fnp, const char *ctx)
{
	int fd;
	struct stat st;
	char *q;
	int retval = 1;

	AN(fn);
	AN(fnp);
	AN(fdp);
	*fnp = NULL;
	*fdp = -1;

	/* try to create a new file of this name */
	fd = open(fn, O_RDWR | O_CREAT | O_EXCL | O_LARGEFILE, 0600);
	if (fd >= 0) {
		*fdp = fd;
		*fnp = fn;
		return (retval);
	}

	if (stat(fn, &st))
		ARGV_ERR(
		    "(%s) \"%s\" does not exist and could not be created\n",
		    ctx, fn);

	if (S_ISDIR(st.st_mode)) {
		asprintf(&q, "%s/varnish.XXXXXX", fn);
		XXXAN(q);
		fd = mkstemp(q);
		if (fd < 0)
			ARGV_ERR("(%s) \"%s\" mkstemp(%s) failed (%s)\n",
			    ctx, fn, q, strerror(errno));
		*fnp = q;
		retval = 2;
	} else if (S_ISREG(st.st_mode)) {
		fd = open(fn, O_RDWR | O_LARGEFILE);
		if (fd < 0)
			ARGV_ERR("(%s) \"%s\" could not open (%s)\n",
			    ctx, fn, strerror(errno));
		*fnp = fn;
		retval = 0;
	} else
		ARGV_ERR(
		    "(%s) \"%s\" is neither file nor directory\n", ctx, fn);

	AZ(fstat(fd, &st));
	if (!S_ISREG(st.st_mode))
		ARGV_ERR("(%s) \"%s\" was not a file after opening\n",
		    ctx, fn);

	*fdp = fd;
	return (retval);
}

/*--------------------------------------------------------------------
 * Figure out how much space is in a filesystem
 */

static uintmax_t
stv_fsspace(int fd, unsigned *bs)
{
	uintmax_t bsize, bavail;
#if defined(HAVE_SYS_STATVFS_H)
	struct statvfs fsst;

	AZ(fstatvfs(fd, &fsst));
	bsize = fsst.f_frsize;
	bavail = fsst.f_bavail;
#elif defined(HAVE_SYS_MOUNT_H) || defined(HAVE_SYS_VFS_H)
	struct statfs fsst;

	AZ(fstatfs(sc->fd, &fsst));
	bsize = fsst.f_bsize;
	bavail = fsst.f_bavail;
#else
#error no struct statfs / struct statvfs
#endif

	/* We use units of the larger of filesystem blocksize and pagesize */
	if (*bs < bsize)
		*bs = bsize;
	xxxassert(*bs % bsize == 0);
	return (bsize * bavail);
}


/*--------------------------------------------------------------------
 * Decide file size.
 *
 * If the sizespecification is empty and the file exists with non-zero
 * size, use that, otherwise, interpret the specification.
 *
 * Handle off_t sizes and pointer width limitations.
 */

uintmax_t
STV_FileSize(int fd, const char *size, unsigned *granularity, const char *ctx)
{
	uintmax_t l, fssize;
	unsigned bs;
	const char *q;
	int i;
	off_t o;
	struct stat st;

	AZ(fstat(fd, &st));
	xxxassert(S_ISREG(st.st_mode));

	bs = *granularity;
	fssize = stv_fsspace(fd, &bs);
	xxxassert(bs % *granularity == 0);

	if ((size == NULL || *size == '\0') && st.st_size != 0) {
		/*
		 * We have no size specification, but an existing file,
		 * use it's existing size.
		 */
		l = st.st_size;
	} else {
		AN(size);
		q = str2bytes(size, &l, fssize);

		if (q != NULL)
			ARGV_ERR("(%s) size \"%s\": %s\n", size, ctx, q);
	}

	/*
	 * This trickery wouldn't be necessary if X/Open would
	 * just add OFF_MAX to <limits.h>...
	 */
	i = 0;
	while(1) {
		o = l;
		if (o == l && o > 0)
			break;
		l >>= 1;
		i++;
	}
	if (i)
		fprintf(stderr, "WARNING: (%s) file size reduced"
		    " to %ju due to system \"off_t\" limitations\n", ctx, l);
	else if (l - st.st_size > fssize) {
		l = fssize * 80 / 100;
		fprintf(stderr, "WARNING: (%s) file size reduced"
		    " to %ju (80%% of available disk space)\n", ctx, l);
	}

	if (sizeof(void *) == 4 && l > INT32_MAX) { /*lint !e506 !e774 */
		fprintf(stderr,
		    "NB: Storage size limited to 2GB on 32 bit architecture,\n"
		    "NB: otherwise we could run out of address space.\n"
		);
		l = INT32_MAX;
	}

	/* round down to multiple of filesystem blocksize or pagesize */
	l -= (l % bs);

	*granularity = bs;
	return(l);
}
