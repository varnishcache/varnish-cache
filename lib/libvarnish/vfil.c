/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Dag-Erling Smørgrav <des@des.no>
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
 */

#include "config.h"

#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#ifdef HAVE_SYS_MOUNT_H
#  include <sys/param.h>
#  include <sys/mount.h>
#endif
#ifdef HAVE_SYS_STATVFS_H
#  include <sys/statvfs.h>
#endif
#ifdef HAVE_SYS_VFS_H
#  include <sys/vfs.h>
#endif
#ifdef HAVE_FALLOCATE
#  include <linux/magic.h>
#endif

#include "vdef.h"

#include "miniobj.h"
#include "vas.h"
#include "vsb.h"
#include "vfil.h"
#include "vqueue.h"

void
VFIL_null_fd(int target)
{
	int fd;

	assert(target >= 0);
	fd = open("/dev/null", O_RDWR);
	assert(fd >= 0);
	assert(dup2(fd, target) == target);
	closefd(&fd);
}

static char *
vfil_readfd(int fd, ssize_t *sz)
{
	struct stat st;
	char *f;
	int i;

	AZ(fstat(fd, &st));
	if (!S_ISREG(st.st_mode))
		return (NULL);
	f = malloc(st.st_size + 1);
	assert(f != NULL);
	i = read(fd, f, st.st_size + 1);
	if (i != st.st_size) {
		free(f);
		return (NULL);
	}
	f[i] = '\0';
	if (sz != NULL)
		*sz = st.st_size;
	return (f);
}

static int
vfil_writefd(int fd, const char *buf, size_t sz)
{
	ssize_t len;

	while (sz > 0) {
		len = write(fd, buf, sz);
		if (len < 0)
			return (len);
		if (len == 0)
			break;
		buf += len;
		sz -= len;
	}

	return (sz == 0 ? 0 : -1);
}

static int
vfil_openfile(const char *pfx, const char *fn, int flags, int mode)
{
	char fnb[PATH_MAX + 1];

	if (fn[0] != '/' && pfx != NULL) {
		/* XXX: graceful length check */
		bprintf(fnb, "/%s/%s", pfx, fn);
		fn = fnb;
	}

	if (flags & O_CREAT)
		return (open(fn, flags, mode));
	else
		return (open(fn, flags));
}

char *
VFIL_readfile(const char *pfx, const char *fn, ssize_t *sz)
{
	int fd, err;
	char *r;

	fd = vfil_openfile(pfx, fn, O_RDONLY, 0);
	if (fd < 0)
		return (NULL);
	r = vfil_readfd(fd, sz);
	err = errno;
	closefd(&fd);
	errno = err;
	return (r);
}

int
VFIL_writefile(const char *pfx, const char *fn, const char *buf, size_t sz)
{
	int fd, err;
	int r;

	fd = vfil_openfile(pfx, fn, O_WRONLY|O_CREAT|O_TRUNC, 0660);
	if (fd < 0)
		return (fd);
	r = vfil_writefd(fd, buf, sz);
	err = errno;
	closefd(&fd);
	errno = err;
	return (r);
}

int
VFIL_nonblocking(int fd)
{
	int i;

	i = fcntl(fd, F_GETFL);
	assert(i != -1);
	i |= O_NONBLOCK;
	i = fcntl(fd, F_SETFL, i);
	assert(i != -1);
	return (i);
}

/*
 * Get file system information from an fd
 * Returns block size, total size and space available in the passed pointers
 * Returns 0 on success, or -1 on failure with errno set
 */
int
VFIL_fsinfo(int fd, unsigned *pbs, uintmax_t *psize, uintmax_t *pspace)
{
	unsigned bs;
	uintmax_t size, space;
#if defined(HAVE_SYS_STATVFS_H)
	struct statvfs fsst;

	if (fstatvfs(fd, &fsst))
		return (-1);
	bs = fsst.f_frsize;
	size = fsst.f_blocks * fsst.f_frsize;
	space = fsst.f_bavail * fsst.f_frsize;
#elif defined(HAVE_SYS_MOUNT_H) || defined(HAVE_SYS_VFS_H)
	struct statfs fsst;

	if (fstatfs(fd, &fsst))
		return (-1);
	bs = fsst.f_bsize;
	size = fsst.f_blocks * fsst.f_bsize;
	space = fsst.f_bavail * fsst.f_bsize;
#else
#error no struct statfs / struct statvfs
#endif

	if (pbs)
		*pbs = bs;
	if (psize)
		*psize = size;
	if (pspace)
		*pspace = space;
	return (0);
}

/*
 * Make sure that the file system can accommodate the file of the given
 * size. Will use fallocate if available. If fallocate is not available
 * and insist is true, it will write size zero bytes.
 *
 * Returns 0 on success, -1 on failure with errno set.
 */

int
VFIL_allocate(int fd, uintmax_t size, int insist)
{
	struct stat st;
	uintmax_t fsspace;
	size_t l;
	ssize_t l2, l3;
	char *buf;
	ssize_t bufsiz;
	int retval = 0;

	if (ftruncate(fd, size))
		return (-1);
	if (fstat(fd, &st))
		return (-1);
	if (VFIL_fsinfo(fd, NULL, NULL, &fsspace))
		return (-1);
	if ((st.st_blocks * 512) + fsspace < size) {
		/* Sum of currently allocated blocks and available space
		   is less than requested size */
		errno = ENOSPC;
		return (-1);
	}
#if defined(__linux__) && defined(HAVE_FALLOCATE)
	{
		/* fallocate will for some filesystems (e.g. xfs) not take
		   the already allocated blocks of the file into
		   account. This will cause fallocate to report ENOSPC
		   when called on an existing fully allocated file unless
		   the filesystem has enough free space to accommodate the
		   complete new file size. Because of this we enable
		   fallocate only on filesystems that are known to work as
		   we expect. */
		struct statfs stfs;
		if (!fstatfs(fd, &stfs) && stfs.f_type == EXT4_SUPER_MAGIC) {
			if (!fallocate(fd, 0, 0, size))
				return (0);
			if (errno == ENOSPC)
				return (-1);
		}
	}
#endif
	if (!insist)
		return (0);

	/* Write size zero bytes to make sure the entire file is allocated
	   in the file system */
	if (size > 65536)
		bufsiz = 64 * 1024;
	else
		bufsiz = size;
	buf = calloc(1, bufsiz);
	AN(buf);
	assert(lseek(fd, 0, SEEK_SET) == 0);
	for (l = 0; l < size; l += l2) {
		l2 = bufsiz;
		if (l + l2 > size)
			l2 = size - l;
		l3 = write(fd, buf, l2);
		if (l3 != l2) {
			retval = -1;
			break;
		}
	}
	assert(lseek(fd, 0, SEEK_SET) == 0);
	free(buf);
	return (retval);
}

struct vfil_path {
	unsigned		magic;
#define VFIL_PATH_MAGIC		0x92dbcc31
	char			*str;
	const char		*dirs[0];
};

/*
 * Path searching functions
 */

void
VFIL_destroypath(struct vfil_path **pp)
{
	struct vfil_path *vp;

	TAKE_OBJ_NOTNULL(vp, pp, VFIL_PATH_MAGIC);
	free(vp->str);
	FREE_OBJ(vp);
}

void
VFIL_setpath(struct vfil_path **pp, const char *path)
{
	struct vfil_path *vp;
	const char *c, **d;
	char *p, *q;
	unsigned n;

	AN(pp);
	AN(path);

	if (*pp != NULL)
		VFIL_destroypath(pp);

	for (n = 1, c = strchr(path, ':'); c != NULL; n++)
		c = strchr(c + 1, ':');

	ALLOC_OBJ_ARRAY(vp, n + 1, dirs, VFIL_PATH_MAGIC);
	AN(vp);
	REPLACE(vp->str, path);
	*pp = vp;

	for (d = vp->dirs, p = vp->str; p != NULL; p = q) {
		q = strchr(p, ':');
		if (q != NULL)
			*q++ = '\0';
		if (*p != '\0') {
			*d = p;
			d++;
		}
	}
	AZ(*d);
}

static int
vfil_path_openfile(void *priv, const char *fn)
{
	char *p, **pp;

	AN(priv);
	AN(fn);
	p = VFIL_readfile(NULL, fn, NULL);
	if (p == NULL)
		return (1);

	pp = priv;
	*pp = p;
	return (0);
}

int
VFIL_searchpath(const struct vfil_path *vp, vfil_path_func_f *func, void *priv,
    const char *fni, char **fno)
{
	struct vsb *vsb;
	const char * const *dir;
	int i, e;

	CHECK_OBJ_NOTNULL(vp, VFIL_PATH_MAGIC);
	AN(fni);
	AN(fno);
	*fno = NULL;

	if (func == NULL) {
		func = vfil_path_openfile;
		AN(priv);
	}

	if (*fni == '/') {
		i = func(priv, fni);
		if (i <= 0)
			REPLACE(*fno, fni);
		return (i);
	}
	vsb = VSB_new_auto();
	AN(vsb);
	for (dir = vp->dirs; *dir != NULL; dir++) {
		VSB_clear(vsb);
		VSB_printf(vsb, "%s/%s", *dir, fni);
		AZ(VSB_finish(vsb));
		if (access(VSB_data(vsb), F_OK))
			continue;
		i = func(priv, VSB_data(vsb));
		if (i <= 0) {
			e = errno;
			*fno = strdup(VSB_data(vsb));
			AN(*fno);
			VSB_destroy(&vsb);
			errno = e;
			return (i);
		}
	}
	VSB_destroy(&vsb);
	return (-1);
}
