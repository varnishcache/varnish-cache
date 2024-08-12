/*-
 * Copyright (c) 2010-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 *
 * VSM stuff common to manager and child.
 *
 */

#include "config.h"

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

static int vsmw_haslock;
vsm_lock_f *vsmw_lock = vsmw_dummy_lock;
vsm_lock_f *vsmw_unlock = vsmw_dummy_lock;

#define vsmw_assert_lock()	AN(vsmw_haslock)

#define vsmw_do_lock() vsmw_do_lock_(__func__, __LINE__)

#define vsmw_do_lock_(f, l)				\
	do {					\
		vsmw_lock();			\
		AZ(vsmw_haslock);		\
		vsmw_haslock = 1;		\
	} while(0)

#define vsmw_do_unlock() vsmw_do_unlock_(__func__, __LINE__)
#define vsmw_do_unlock_(f, l)				\
	do {					\
		AN(vsmw_haslock);		\
		vsmw_haslock = 0;		\
		vsmw_unlock();			\
	} while(0)

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
	int				named;
};

struct vsmwseg {
	unsigned			magic;
#define VSMWSEG_MAGIC			0x7e4ccaea
	VTAILQ_ENTRY(vsmwseg)		list;
	struct vsmw_cluster		*cluster;

	char				*category;
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
	uint64_t			nsegs;
	uint64_t			nsubs;
};

/* Allocations in clusters never start at offset zero */
#define VSM_CLUSTER_OFFSET 16

/*--------------------------------------------------------------------*/

static void
vsmw_idx_head(const struct vsmw *vsmw, int fd)
{
	char buf[64];

	bprintf(buf, "# %jd %jd\n", (intmax_t)vsmw->pid, (intmax_t)vsmw->birth);
	// XXX handle ENOSPC? #2764
	assert(write(fd, buf, strlen(buf)) == strlen(buf));
}

#define ASSERT_SEG_STR(x) do {			\
		AN(x);				\
		AZ(strchr(x, '\n'));		\
	} while (0);

static void
vsmw_fmt_index(const struct vsmw *vsmw, const struct vsmwseg *seg, char act)
{

	vsmw_assert_lock();
	CHECK_OBJ_NOTNULL(vsmw, VSMW_MAGIC);
	CHECK_OBJ_NOTNULL(seg, VSMWSEG_MAGIC);
	AN(seg->cluster);
	ASSERT_SEG_STR(seg->category);
	ASSERT_SEG_STR(seg->id);

	VSB_printf(vsmw->vsb, "%c %s %zu %zu %s %s\n",
	    act,
	    seg->cluster->fn,
	    seg->off,
	    seg->len,
	    seg->category,
	    seg->id);
}

/*--------------------------------------------------------------------*/

static void
vsmw_mkent(const struct vsmw *vsmw, const char *pfx)
{
	int fd;
	uint64_t rn;

	AN(pfx);
	vsmw_assert_lock();
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
			closefd(&fd);
	}
}

/*--------------------------------------------------------------------*/

static void
vsmw_append_record(struct vsmw *vsmw, struct vsmwseg *seg, char act)
{
	int fd;

	vsmw_assert_lock();
	CHECK_OBJ_NOTNULL(vsmw, VSMW_MAGIC);
	CHECK_OBJ_NOTNULL(seg, VSMWSEG_MAGIC);
	fd = openat(vsmw->vdirfd, vsmw->idx, O_APPEND | O_WRONLY);
	assert(fd >= 0);
	VSB_clear(vsmw->vsb);
	vsmw_fmt_index(vsmw, seg, act);
	AZ(VSB_finish(vsmw->vsb));
	XXXAZ(VSB_tofile(vsmw->vsb, fd)); // XXX handle ENOSPC? #2764
	closefd(&fd);
}

/*--------------------------------------------------------------------*/

static void
vsmw_addseg(struct vsmw *vsmw, struct vsmwseg *seg)
{

	vsmw_assert_lock();
	VTAILQ_INSERT_TAIL(&vsmw->segs, seg, list);
	vsmw_append_record(vsmw, seg, '+');
	vsmw->nsegs++;
}

/*--------------------------------------------------------------------*/

static void
vsmw_delseg(struct vsmw *vsmw, struct vsmwseg *seg)
{
	char *t = NULL;
	int fd;
	struct vsmwseg *s2;

	vsmw_assert_lock();
	CHECK_OBJ_NOTNULL(vsmw, VSMW_MAGIC);
	CHECK_OBJ_NOTNULL(seg, VSMWSEG_MAGIC);

	VTAILQ_REMOVE(&vsmw->segs, seg, list);

	vsmw->nsegs--;
	if (vsmw->nsubs < 10 || vsmw->nsubs * 2 < vsmw->nsegs) {
		vsmw_append_record(vsmw, seg, '-');
		vsmw->nsubs++;
	} else {
		vsmw_mkent(vsmw, vsmw->idx);
		REPLACE(t, VSB_data(vsmw->vsb));
		fd = openat(vsmw->vdirfd,
		    t, O_WRONLY|O_CREAT|O_EXCL, vsmw->mode);
		assert(fd >= 0);
		vsmw_idx_head(vsmw, fd);
		VSB_clear(vsmw->vsb);
		VTAILQ_FOREACH(s2, &vsmw->segs, list)
			vsmw_fmt_index(vsmw, s2, '+');
		AZ(VSB_finish(vsmw->vsb));
		XXXAZ(VSB_tofile(vsmw->vsb, fd)); // XXX handle ENOSPC? #2764
		closefd(&fd);
		AZ(renameat(vsmw->vdirfd, t, vsmw->vdirfd, vsmw->idx));
		REPLACE(t, NULL);
		vsmw->nsubs = 0;
	}
	REPLACE(seg->category, NULL);
	REPLACE(seg->id, NULL);
	FREE_OBJ(seg);
}

/*--------------------------------------------------------------------*/

static struct vsmw_cluster *
vsmw_newcluster(struct vsmw *vsmw, size_t len, const char *pfx)
{
	struct vsmw_cluster *vc;
	static int warn = 0;
	int fd;
	size_t ps;

	vsmw_assert_lock();
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

	closefd(&fd);
	assert(vc->ptr != MAP_FAILED);
	if (mlock(vc->ptr, len) && warn++ == 0)  {
		fprintf(stderr, "Warning: mlock() of VSM failed: %s (%d)\n",
		    VAS_errtxt(errno), errno);
	}

	return (vc);
}

struct vsmw_cluster *
VSMW_NewCluster(struct vsmw *vsmw, size_t len, const char *pfx)
{
	struct vsmw_cluster *vc;
	struct vsmwseg *seg;

	vsmw_do_lock();
	vc = vsmw_newcluster(vsmw, len + VSM_CLUSTER_OFFSET, pfx);
	AN(vc);
	vc->next += VSM_CLUSTER_OFFSET;

	ALLOC_OBJ(seg, VSMWSEG_MAGIC);
	AN(seg);
	vc->cseg = seg;
	seg->len = vc->len;
	seg->cluster = vc;
	REPLACE(seg->category, "");
	REPLACE(seg->id, "");
	vc->refs++;
	vc->named = 1;
	vsmw_addseg(vsmw, seg);

	vsmw_do_unlock();
	return (vc);
}

static void
vsmw_DestroyCluster_locked(struct vsmw *vsmw, struct vsmw_cluster *vc)
{

	vsmw_assert_lock();
	CHECK_OBJ_NOTNULL(vsmw, VSMW_MAGIC);
	CHECK_OBJ_NOTNULL(vc, VSMW_CLUSTER_MAGIC);

	AZ(vc->refs);

	AZ(munmap(vc->ptr, vc->len));
	if (vc->named)
		vsmw_delseg(vsmw, vc->cseg);
	vc->cseg = 0;

	VTAILQ_REMOVE(&vsmw->clusters, vc, list);
	if (unlinkat(vsmw->vdirfd, vc->fn, 0))
		assert (errno == ENOENT);
	REPLACE(vc->fn, NULL);
	FREE_OBJ(vc);
}

void
VSMW_DestroyCluster(struct vsmw *vsmw, struct vsmw_cluster **vsmcp)
{
	struct vsmw_cluster *vc;

	TAKE_OBJ_NOTNULL(vc, vsmcp, VSMW_CLUSTER_MAGIC);

	vsmw_do_lock();
	if (--vc->refs == 0)
		vsmw_DestroyCluster_locked(vsmw, vc);
	vsmw_do_unlock();
}

/*--------------------------------------------------------------------*/

void *
VSMW_Allocv(struct vsmw *vsmw, struct vsmw_cluster *vc,
    const char *category, size_t payload, const char *prefix,
    const char *fmt, va_list va)
{
	struct vsmwseg *seg;
	ssize_t l;

	vsmw_do_lock();
	CHECK_OBJ_NOTNULL(vsmw, VSMW_MAGIC);

	ALLOC_OBJ(seg, VSMWSEG_MAGIC);
	AN(seg);
	REPLACE(seg->category, category);
	seg->len = PRNDUP(payload);

	VSB_clear(vsmw->vsb);
	if (prefix != NULL) {
		assert(prefix[0] != '\0');
		VSB_cat(vsmw->vsb, prefix);
		if (fmt[0] != '\0')
			VSB_cat(vsmw->vsb, ".");
	}
	l = VSB_len(vsmw->vsb);
	assert(l >= 0);
	VSB_vprintf(vsmw->vsb, fmt, va);
	AZ(VSB_finish(vsmw->vsb));
	assert(fmt[0] == '\0' || l < VSB_len(vsmw->vsb));

	REPLACE(seg->id, VSB_data(vsmw->vsb));

	if (vc == NULL)
		vc = vsmw_newcluster(vsmw, seg->len, category);
	AN(vc);
	vc->refs++;

	seg->cluster = vc;
	seg->off = vc->next;
	vc->next += seg->len;
	assert(vc->next <= vc->len);
	seg->ptr = seg->off + (char*)vc->ptr;

	vsmw_addseg(vsmw, seg);

	vsmw_do_unlock();
	return (seg->ptr);
}

void *
VSMW_Allocf(struct vsmw *vsmw, struct vsmw_cluster *vc,
    const char *category, size_t len, const char *fmt, ...)
{
	va_list ap;
	void *p;

	va_start(ap, fmt);
	p = VSMW_Allocv(vsmw, vc, category, len, NULL, fmt, ap);
	va_end(ap);
	return (p);
}

/*--------------------------------------------------------------------*/

void
VSMW_Free(struct vsmw *vsmw, void **pp)
{
	struct vsmwseg *seg;
	struct vsmw_cluster *cp;

	vsmw_do_lock();
	CHECK_OBJ_NOTNULL(vsmw, VSMW_MAGIC);
	AN(pp);
	VTAILQ_FOREACH(seg, &vsmw->segs, list)
		if (seg->ptr == *pp)
			break;
	AN(seg);
	*pp = NULL;

	cp = seg->cluster;
	CHECK_OBJ_NOTNULL(cp, VSMW_CLUSTER_MAGIC);
	assert(cp->refs > 0);

	vsmw_delseg(vsmw, seg);

	if (!--cp->refs)
		vsmw_DestroyCluster_locked(vsmw, cp);
	vsmw_do_unlock();
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

	vsmw_do_lock();
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
	closefd(&fd);

	vsmw_do_unlock();
	return (vsmw);
}

void
VSMW_Destroy(struct vsmw **pp)
{
	struct vsmw *vsmw;
	struct vsmwseg *seg, *s2;

	vsmw_do_lock();
	TAKE_OBJ_NOTNULL(vsmw, pp, VSMW_MAGIC);
	VTAILQ_FOREACH_SAFE(seg, &vsmw->segs, list, s2)
		vsmw_delseg(vsmw, seg);
	if (unlinkat(vsmw->vdirfd, vsmw->idx, 0))
		assert (errno == ENOENT);
	REPLACE(vsmw->idx, NULL);
	VSB_destroy(&vsmw->vsb);
	closefd(&vsmw->vdirfd);
	FREE_OBJ(vsmw);
	vsmw_do_unlock();
}
