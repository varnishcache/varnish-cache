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
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "vdef.h"
#include "vas.h"
#include "vsb.h"
#include "miniobj.h"
#include "vqueue.h"

#include "vfil.h"
#include "vrnd.h"

#include "heritage.h"
#include "vsmw.h"

#ifndef MAP_HASSEMAPHORE
#  define MAP_HASSEMAPHORE 0 /* XXX Linux */
#endif

#ifndef MAP_NOSYNC
#  define MAP_NOSYNC 0 /* XXX Linux */
#endif

static void v_matchproto_(vsm_lock_f)
vsmw_dummy_lock(void)
{
}

vsm_lock_f *vsmw_lock = vsmw_dummy_lock;
vsm_lock_f *vsmw_unlock = vsmw_dummy_lock;

/*--------------------------------------------------------------------*/

struct vsmw_cluster {
	unsigned			magic;
#define VSMW_CLUSTER_MAGIC		0x28b74c00

	VTAILQ_ENTRY(vsmw_cluster)	list;
	struct vsmwseg			*cseg;
	char				*fn;
	size_t				len;
	void				*ptr;
	size_t				next;
	int				refs;
};

struct vsmwseg {
	unsigned			magic;
#define VSMWSEG_MAGIC			0x7e4ccaea
	VTAILQ_ENTRY(vsmwseg)		list;
	struct vsmw_cluster		*cluster;

	char				*class;
	size_t				off;
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
	VTAILQ_HEAD(, vsmw_cluster)	clusters;
	VTAILQ_HEAD(, vsmwseg)		segs;
	struct vsb			*vsb;
	pid_t				pid;
	time_t				birth;
};

/*--------------------------------------------------------------------*/

static void
vsmw_idx_head(const struct vsmw *vsmw, int fd)
{
	char buf[64];

	bprintf(buf, "# %jd %jd\n", (intmax_t)vsmw->pid, (intmax_t)vsmw->birth);
	assert(write(fd, buf, strlen(buf)) == strlen(buf));
}

static void
vsmw_fmt_index(const struct vsmw *vsmw, const struct vsmwseg *seg)
{

	CHECK_OBJ_NOTNULL(vsmw, VSMW_MAGIC);
	CHECK_OBJ_NOTNULL(seg, VSMWSEG_MAGIC);
	VSB_printf(vsmw->vsb, "%s %zu %zu %s %s\n",
	    seg->cluster->fn,
	    seg->off,
	    seg->len,
	    seg->class,
	    seg->id);
}

/*--------------------------------------------------------------------*/

static void
vsmw_mkent(const struct vsmw *vsmw, const char *pfx)
{
	int fd;
	uint64_t rn;

	AN(pfx);
	while (1) {
		VSB_clear(vsmw->vsb);
		VSB_printf(vsmw->vsb, "_.%s", pfx);
		AZ(VRND_RandomCrypto(&rn, sizeof rn));
		VSB_printf(vsmw->vsb, ".%016jx", (uintmax_t)rn);
		AZ(VSB_finish(vsmw->vsb));
		fd = openat(vsmw->vdirfd, VSB_data(vsmw->vsb), O_RDONLY);
		if (fd < 0 && errno == ENOENT)
			return;
		if (fd >= 0)
			AZ(close(fd));
	}
}

/*--------------------------------------------------------------------*/

static void
vsmw_addseg(struct vsmw *vsmw, struct vsmwseg *seg)
{
	int fd;
	ssize_t s;

	VTAILQ_INSERT_TAIL(&vsmw->segs, seg, list);
	fd = openat(vsmw->vdirfd, vsmw->idx, O_APPEND | O_WRONLY);
	assert(fd >= 0);
	VSB_clear(vsmw->vsb);
	vsmw_fmt_index(vsmw, seg);
	AZ(VSB_finish(vsmw->vsb));
	s = write(fd, VSB_data(vsmw->vsb), VSB_len(vsmw->vsb));
	assert(s == VSB_len(vsmw->vsb));
	AZ(close(fd));
}

/*--------------------------------------------------------------------*/

static void
vsmw_delseg(struct vsmw *vsmw, struct vsmwseg *seg, int fixidx)
{
	char *t = NULL;
	ssize_t s;
	int fd;

	CHECK_OBJ_NOTNULL(vsmw, VSMW_MAGIC);
	CHECK_OBJ_NOTNULL(seg, VSMWSEG_MAGIC);

	VTAILQ_REMOVE(&vsmw->segs, seg, list);
	REPLACE(seg->class, NULL);
	REPLACE(seg->id, NULL);
	FREE_OBJ(seg);

	if (fixidx) {
		vsmw_mkent(vsmw, vsmw->idx);
		REPLACE(t, VSB_data(vsmw->vsb));
		AN(t);
		fd = openat(vsmw->vdirfd,
		    t, O_WRONLY|O_CREAT|O_EXCL, vsmw->mode);
		assert(fd >= 0);
		vsmw_idx_head(vsmw, fd);
		VSB_clear(vsmw->vsb);
		VTAILQ_FOREACH(seg, &vsmw->segs, list)
			vsmw_fmt_index(vsmw, seg);
		AZ(VSB_finish(vsmw->vsb));
		s = write(fd, VSB_data(vsmw->vsb), VSB_len(vsmw->vsb));
		assert(s == VSB_len(vsmw->vsb));
		AZ(close(fd));
		AZ(renameat(vsmw->vdirfd, t, vsmw->vdirfd, vsmw->idx));
		REPLACE(t, NULL);
	}
}

/*--------------------------------------------------------------------*/

static struct vsmw_cluster *
vsmw_newcluster(struct vsmw *vsmw, size_t len, const char *pfx)
{
	struct vsmw_cluster *vc;
	int fd;
	size_t ps;

	ALLOC_OBJ(vc, VSMW_CLUSTER_MAGIC);
	AN(vc);

	vsmw_mkent(vsmw, pfx);
	REPLACE(vc->fn, VSB_data(vsmw->vsb));

	VTAILQ_INSERT_TAIL(&vsmw->clusters, vc, list);

	ps = getpagesize();
	len = RUP2(len, ps);
	vc->len = len;

	fd = openat(vsmw->vdirfd, vc->fn,
	    O_RDWR | O_CREAT | O_EXCL, vsmw->mode);
	assert(fd >= 0);

	AZ(VFIL_allocate(fd, (off_t)len, 1));

	vc->ptr = (void *)mmap(NULL, len,
	    PROT_READ|PROT_WRITE,
	    MAP_HASSEMAPHORE | MAP_NOSYNC | MAP_SHARED,
	    fd, 0);

	AZ(close(fd));
	assert(vc->ptr != MAP_FAILED);
	(void)mlock(vc->ptr, len);

	return (vc);
}

struct vsmw_cluster *
VSMW_NewCluster(struct vsmw *vsmw, size_t len, const char *pfx)
{
	struct vsmw_cluster *vc;
	struct vsmwseg *seg;

	vsmw_lock();
	vc = vsmw_newcluster(vsmw, len, pfx);

	ALLOC_OBJ(seg, VSMWSEG_MAGIC);
	AN(seg);
	vc->cseg = seg;
	seg->len = len;
	seg->cluster = vc;
	REPLACE(seg->class, "");
	REPLACE(seg->id, "");
	vsmw_addseg(vsmw, seg);

	vsmw_unlock();
	return (vc);
}

void
VSMW_DestroyCluster(struct vsmw *vsmw, struct vsmw_cluster **vsmcp)
{
	struct vsmw_cluster *vc;

	vsmw_lock();
	CHECK_OBJ_NOTNULL(vsmw, VSMW_MAGIC);
	AN(vsmcp);
	vc = *vsmcp;
	*vsmcp = NULL;
	CHECK_OBJ_NOTNULL(vc, VSMW_CLUSTER_MAGIC);

	if (vc->cseg != NULL) {
		/*
		 * Backends go on the cool list, so the VGC cluster is
		 * destroyed before they are.  Solve this by turning the
		 * cluster into an anonymous cluster which dies with the
		 * refcount on it.
		 */
		vsmw_delseg(vsmw, vc->cseg, 1);
		vc->cseg = NULL;
		if (vc->refs > 0) {
			vsmw_unlock();
			return;
		}
	}
	AZ(munmap(vc->ptr, vc->len));

	AZ(vc->refs);
	VTAILQ_REMOVE(&vsmw->clusters, vc, list);
	if (unlinkat(vsmw->vdirfd, vc->fn, 0))
		assert (errno == ENOENT);
	REPLACE(vc->fn, NULL);
	FREE_OBJ(vc);
	vsmw_unlock();
}

/*--------------------------------------------------------------------*/

void *
VSMW_Allocv(struct vsmw *vsmw, struct vsmw_cluster *vc,
    const char *class, size_t payload,
    const char *fmt, va_list va)
{
	struct vsmwseg *seg;

	vsmw_lock();
	CHECK_OBJ_NOTNULL(vsmw, VSMW_MAGIC);
	(void)vc;

	ALLOC_OBJ(seg, VSMWSEG_MAGIC);
	AN(seg);
	REPLACE(seg->class, class);
	seg->len = PRNDUP(payload);

	VSB_clear(vsmw->vsb);
	VSB_vprintf(vsmw->vsb, fmt, va);
	AZ(VSB_finish(vsmw->vsb));
	REPLACE(seg->id, VSB_data(vsmw->vsb));

	if (vc == NULL)
		vc = vsmw_newcluster(vsmw, seg->len, class);
	AN(vc);
	vc->refs++;

	seg->cluster = vc;
	seg->off = vc->next;
	vc->next += seg->len;
	assert(vc->next <= vc->len);
	seg->ptr = seg->off + (char*)vc->ptr;

	vsmw_addseg(vsmw, seg);

	vsmw_unlock();
	return (seg->ptr);
}

void *
VSMW_Allocf(struct vsmw *vsmw, struct vsmw_cluster *vc,
    const char *class, size_t len, const char *fmt, ...)
{
	va_list ap;
	void *p;

	va_start(ap, fmt);
	p = VSMW_Allocv(vsmw, vc, class, len, fmt, ap);
	va_end(ap);
	return (p);
}

/*--------------------------------------------------------------------*/

void
VSMW_Free(struct vsmw *vsmw, void **pp)
{
	struct vsmwseg *seg;
	void *p;

	vsmw_lock();
	CHECK_OBJ_NOTNULL(vsmw, VSMW_MAGIC);
	AN(pp);
	p = *pp;
	AN(p);
	*pp = NULL;
	VTAILQ_FOREACH(seg, &vsmw->segs, list)
		if (seg->ptr == p)
			break;
	AN(seg);

	if (!--seg->cluster->refs && seg->cluster->cseg == NULL) {
		vsmw_unlock();
		VSMW_DestroyCluster(vsmw, &seg->cluster);
		vsmw_lock();
	}

	vsmw_delseg(vsmw, seg, 1);
	vsmw_unlock();
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

	vsmw_lock();
	ALLOC_OBJ(vsmw, VSMW_MAGIC);
	AN(vsmw);

	VTAILQ_INIT(&vsmw->segs);
	VTAILQ_INIT(&vsmw->clusters);
	vsmw->vsb = VSB_new_auto();
	AN(vsmw->vsb);
	REPLACE(vsmw->idx, idxname);
	vsmw->mode = mode;
	vsmw->vdirfd = vdirfd;
	vsmw->pid = getpid();
	vsmw->birth = time(NULL);

	if (unlinkat(vdirfd, vsmw->idx, 0))
		assert (errno == ENOENT);
	fd = openat(vdirfd,
	    vsmw->idx, O_APPEND | O_WRONLY | O_CREAT, vsmw->mode);
	assert(fd >= 0);
	vsmw_idx_head(vsmw, fd);
	AZ(close(fd));

	vsmw_unlock();
	return (vsmw);
}

void
VSMW_Destroy(struct vsmw **pp)
{
	struct vsmw *vsmw;
	struct vsmwseg *seg, *s2;

	vsmw_lock();
	TAKE_OBJ_NOTNULL(vsmw, pp, VSMW_MAGIC);
	VTAILQ_FOREACH_SAFE(seg, &vsmw->segs, list, s2)
		vsmw_delseg(vsmw, seg, 0);
	if (unlinkat(vsmw->vdirfd, vsmw->idx, 0))
		assert (errno == ENOENT);
	REPLACE(vsmw->idx, NULL);
	VSB_destroy(&vsmw->vsb);
	AZ(close(vsmw->vdirfd));
	FREE_OBJ(vsmw);
	vsmw_unlock();
}
