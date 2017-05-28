/*-
 * Copyright (c) 2010-2011 Varnish Software AS
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
 * VSM stuff common to manager and child.
 *
 * Please see comments in <vsm_priv.h> for details of protocols and
 * data consistency.
 *
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "vdef.h"
#include "vas.h"
#include "vsb.h"
#include "miniobj.h"
#include "vsmw.h"
#include "vqueue.h"

#include "vfil.h"
#include "vrnd.h"

#ifndef MAP_HASSEMAPHORE
#  define MAP_HASSEMAPHORE 0 /* XXX Linux */
#endif

#ifndef MAP_NOSYNC
#  define MAP_NOSYNC 0 /* XXX Linux */
#endif

/*--------------------------------------------------------------------*/

struct vsmwseg {
	unsigned			magic;
#define VSMWSEG_MAGIC			0x7e4ccaea
	VTAILQ_ENTRY(vsmwseg)		list;
	char				*fn;

	char				*class;
	size_t				len;
	char				*id;
	void				*ptr;
};

struct vsmw {
	unsigned			magic;
#define VSMW_MAGIC			0xc2ca2cd9
	int				vdirfd;
	int				mode;
	char				*idx;
	VTAILQ_HEAD(, vsmwseg)		segs;
	struct vsb			*vsb;
};

/*--------------------------------------------------------------------*/

static void
vsmw_write_index(const struct vsmw *vsmw, int fd, const struct vsmwseg *seg)
{
	ssize_t s;

	CHECK_OBJ_NOTNULL(vsmw, VSMW_MAGIC);
	CHECK_OBJ_NOTNULL(seg, VSMWSEG_MAGIC);
	VSB_clear(vsmw->vsb);
	VSB_printf(vsmw->vsb, "%s %zu %s %s\n",
	    seg->fn,
	    seg->len,
	    seg->class,
	    seg->id);
	AZ(VSB_finish(vsmw->vsb));
	s = write(fd, VSB_data(vsmw->vsb), VSB_len(vsmw->vsb));
	assert(s == VSB_len(vsmw->vsb));
}

/*--------------------------------------------------------------------*/

static void
vsmw_mkent(const struct vsmw *vsmw, const char *pfx)
{
	int fd;

	while (1) {
		VSB_clear(vsmw->vsb);
		VSB_printf(vsmw->vsb, "%s.%lx", pfx, VRND_RandomTestable());
		AZ(VSB_finish(vsmw->vsb));
		fd = openat(vsmw->vdirfd, VSB_data(vsmw->vsb), O_RDONLY);
		if (fd < 0 && errno == ENOENT)
			return;
		if (fd >= 0)
			AZ(close(fd));
	}
}

/*--------------------------------------------------------------------*/

void *
VSMW_Allocv(struct vsmw *vsmw, const char *class, size_t len,
    const char *fmt, va_list va)
{
	struct vsmwseg *seg;
	int fd;
	size_t ps;

	CHECK_OBJ_NOTNULL(vsmw, VSMW_MAGIC);

	ALLOC_OBJ(seg, VSMWSEG_MAGIC);
	AN(seg);
	REPLACE(seg->class, class);
	seg->len = len;

	VSB_clear(vsmw->vsb);
	VSB_vprintf(vsmw->vsb, fmt, va);
	AZ(VSB_finish(vsmw->vsb));
	REPLACE(seg->id, VSB_data(vsmw->vsb));

	vsmw_mkent(vsmw, class);
	REPLACE(seg->fn, VSB_data(vsmw->vsb));

	ps = getpagesize();
	len = RUP2(len, ps);

	fd = openat(vsmw->vdirfd, seg->fn,
	    O_RDWR | O_CREAT | O_EXCL, vsmw->mode);
	assert(fd >= 0);

	AZ(VFIL_allocate(fd, (off_t)len, 1));

	seg->ptr = (void *)mmap(NULL, len,
	    PROT_READ|PROT_WRITE,
	    MAP_HASSEMAPHORE | MAP_NOSYNC | MAP_SHARED,
	    fd, 0);

	AZ(close(fd));
	assert(seg->ptr != MAP_FAILED);

	VTAILQ_INSERT_TAIL(&vsmw->segs, seg, list);
	fd = openat(vsmw->vdirfd, vsmw->idx, O_APPEND | O_WRONLY);
	assert(fd >= 0);
	vsmw_write_index(vsmw, fd, seg);
	AZ(close(fd));

	return (seg->ptr);
}

void *
VSMW_Allocf(struct vsmw *vsmw, const char *class, size_t len,
    const char *fmt, ...)
{
	va_list ap;
	void *p;

	va_start(ap, fmt);
	p = VSMW_Allocv(vsmw, class, len, fmt, ap);
	va_end(ap);
	return (p);
}

/*--------------------------------------------------------------------*/
static void
vsmw_delseg(struct vsmw *vsmw, struct vsmwseg *seg, int fixidx)
{
	char *t = NULL;
	int fd;
	size_t len;

	CHECK_OBJ_NOTNULL(vsmw, VSMW_MAGIC);
	CHECK_OBJ_NOTNULL(seg, VSMWSEG_MAGIC);

	len = getpagesize();
	len = RUP2(seg->len, len);
	AZ(munmap(seg->ptr, len));

	VTAILQ_REMOVE(&vsmw->segs, seg, list);
	AZ(unlinkat(vsmw->vdirfd, seg->fn, 0));
	REPLACE(seg->fn, NULL);
	REPLACE(seg->id, NULL);
	FREE_OBJ(seg);

	if (fixidx) {
		vsmw_mkent(vsmw, vsmw->idx);
		REPLACE(t, VSB_data(vsmw->vsb));
		AN(t);
		fd = open(t, O_WRONLY|O_CREAT|O_EXCL, vsmw->mode);
		assert(fd >= 0);
		VTAILQ_FOREACH(seg, &vsmw->segs, list)
			vsmw_write_index(vsmw, fd, seg);
		AZ(close(fd));
		AZ(rename(t, vsmw->idx));
		REPLACE(t, NULL);
	}
}

/*--------------------------------------------------------------------*/

void
VSMW_Free(struct vsmw *vsmw, void **pp)
{
	struct vsmwseg *seg;
	void *p;

	CHECK_OBJ_NOTNULL(vsmw, VSMW_MAGIC);
	AN(pp);
	p = *pp;
	AN(p);
	*pp = NULL;
	VTAILQ_FOREACH(seg, &vsmw->segs, list)
		if (seg->ptr == p)
			break;
	AN(seg);
	vsmw_delseg(vsmw, seg, 1);
}

/*--------------------------------------------------------------------*/

struct vsmw *
VSMW_New(int vdirfd, int mode, const char *idxname)
{
	struct vsmw *vsmw;
	int fd;

	assert(vdirfd > 0);
	assert(mode > 0);
	AN(idxname);
	ALLOC_OBJ(vsmw, VSMW_MAGIC);
	AN(vsmw);
	VTAILQ_INIT(&vsmw->segs);
	vsmw->vsb = VSB_new_auto();
	AN(vsmw->vsb);
	REPLACE(vsmw->idx, idxname);
	vsmw->mode = mode;
	vsmw->vdirfd = vdirfd;

	(void)unlinkat(vdirfd, vsmw->idx, 0);
	fd = openat(vdirfd,
	    vsmw->idx, O_APPEND | O_WRONLY | O_CREAT, vsmw->mode);
	assert(fd >= 0);
	AZ(close(fd));

	return (vsmw);
}

void
VSMW_Destroy(struct vsmw **pp)
{
	struct vsmw *vsmw;
	struct vsmwseg *seg, *s2;

	AN(pp);
	AN(*pp);
	vsmw = *pp;
	*pp = NULL;
	CHECK_OBJ_NOTNULL(vsmw, VSMW_MAGIC);
	AZ(unlinkat(vsmw->vdirfd, vsmw->idx, 0));
	REPLACE(vsmw->idx, NULL);
	VTAILQ_FOREACH_SAFE(seg, &vsmw->segs, list, s2)
		vsmw_delseg(vsmw, seg, 0);
	VSB_destroy(&vsmw->vsb);
	AZ(close(vsmw->vdirfd));
	FREE_OBJ(vsmw);
}
