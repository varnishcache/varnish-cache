/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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

#include <sys/mman.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vdef.h"
#include "vas.h"
#include "miniobj.h"

#include "vav.h"
#include "vin.h"
#include "vlu.h"
#include "vsb.h"
#include "vsm_priv.h"
#include "vqueue.h"
#include "vtim.h"

#include "vapi/vsig.h"
#include "vapi/vsm.h"

#ifndef MAP_HASSEMAPHORE
#  define MAP_HASSEMAPHORE 0 /* XXX Linux */
#endif

#ifndef MAP_NOSYNC
#  define MAP_NOSYNC 0 /* XXX Linux */
#endif

const struct vsm_valid VSM_invalid[1] = {{"invalid"}};
const struct vsm_valid VSM_valid[1] = {{"valid"}};

static vlu_f vsm_vlu_func;

#define VSM_PRIV_SHIFT							\
	(sizeof (uint64_t) * 4)
#define VSM_PRIV_MASK							\
	((1UL << VSM_PRIV_SHIFT) - 1)
#define VSM_PRIV_LOW(u)							\
	((uint64_t)(u) & VSM_PRIV_MASK)
#define VSM_PRIV_HIGH(u)						\
	(((uint64_t)(u) >> VSM_PRIV_SHIFT) & VSM_PRIV_MASK)
#define VSM_PRIV_MERGE(low, high)					\
	(VSM_PRIV_LOW(low) | (VSM_PRIV_LOW(high) << VSM_PRIV_SHIFT))

/*--------------------------------------------------------------------*/

struct vsm_set;

struct vsm_seg {
	unsigned		magic;
#define VSM_SEG_MAGIC		0xeb6c6dfd
	unsigned		flags;
#define VSM_FLAG_MARKSCAN	(1U<<1)
#define VSM_FLAG_STALE		(1U<<2)
#define VSM_FLAG_CLUSTER	(1U<<3)
	VTAILQ_ENTRY(vsm_seg)	list;
	VTAILQ_ENTRY(vsm_seg)	clist;
	struct vsm_set		*set;
	struct vsm_seg		*cluster;
	char			**av;
	int			refs;
	void			*s;
	size_t			sz;
	void			*b;
	void			*e;
	uint64_t		serial;
};

struct vsm_set {
	unsigned		magic;
#define VSM_SET_MAGIC		0xdee401b8
	const char		*dname;
	struct vsm		*vsm;
	VTAILQ_HEAD(,vsm_seg)	segs;
	VTAILQ_HEAD(,vsm_seg)	stale;
	VTAILQ_HEAD(,vsm_seg)	clusters;

	int			dfd;
	struct stat		dst;

	int			fd;
	struct stat		fst;

	uintmax_t		id1, id2;

	// _.index reading state
	struct vlu		*vlu;
	unsigned		retval;
	struct vsm_seg		*vg;

	unsigned		flag_running;
	unsigned		flag_changed;
	unsigned		flag_restarted;
};

struct vsm {
	unsigned		magic;
#define VSM_MAGIC		0x6e3bd69b

	struct vsb		*diag;
	uint64_t		serial;

	int			wdfd;
	struct stat		wdst;
	char			*wdname;

	struct vsm_set		*mgt;
	struct vsm_set		*child;

	int			attached;
	double			patience;
};

/*--------------------------------------------------------------------*/

static int
vsm_diag(struct vsm *vd, const char *fmt, ...)
{
	va_list ap;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	AN(fmt);

	if (vd->diag == NULL)
		vd->diag = VSB_new_auto();
	AN(vd->diag);
	VSB_clear(vd->diag);
	va_start(ap, fmt);
	VSB_vprintf(vd->diag, fmt, ap);
	va_end(ap);
	AZ(VSB_finish(vd->diag));
	return (-1);
}

/*--------------------------------------------------------------------*/

static int
vsm_mapseg(struct vsm *vd, struct vsm_seg *vg)
{
	size_t of, off, sz, ps, len;
	struct vsb *vsb;
	int fd;

	CHECK_OBJ_NOTNULL(vg, VSM_SEG_MAGIC);

	if (vg->s != NULL)
		return (0);

	ps = getpagesize();

	of = strtoul(vg->av[2], NULL, 10);
	off = RDN2(of, ps);

	if (vg->flags & VSM_FLAG_CLUSTER)
		assert(of == 0);
	assert(vg->cluster == NULL);

	sz = strtoul(vg->av[3], NULL, 10);
	assert(sz > 0);
	assert(of >= off);
	len = RUP2((of - off) + sz, ps);

	vsb = VSB_new_auto();
	AN(vsb);
	VSB_printf(vsb, "%s/%s/%s", vd->wdname, vg->set->dname, vg->av[1]);
	AZ(VSB_finish(vsb));

	fd = open(VSB_data(vsb), O_RDONLY);	// XXX: openat
	if (fd < 0) {
		VSB_destroy(&vsb);
		return (vsm_diag(vd, "Could not open segment"));
	}

	vg->s = (void*)mmap(NULL, len,
	    PROT_READ,
	    MAP_HASSEMAPHORE | MAP_NOSYNC | MAP_SHARED,
	    fd, (off_t)off);

	VSB_destroy(&vsb);

	closefd(&fd);
	if (vg->s == MAP_FAILED)
		return (vsm_diag(vd, "Could not mmap segment"));

	vg->b = (char*)(vg->s) + of - off;
	vg->e = (char *)vg->b + sz;
	vg->sz = len;

	return (0);
}

static void
vsm_unmapseg(struct vsm_seg *vg)
{

	CHECK_OBJ_NOTNULL(vg, VSM_SEG_MAGIC);

	AN(vg->b);
	AN(vg->e);
	AZ(munmap(vg->s, vg->sz));
	vg->s = vg->b = vg->e = NULL;
	vg->sz = 0;
}

/*--------------------------------------------------------------------*/

static void
vsm_delseg(struct vsm_seg *vg, int refsok)
{

	CHECK_OBJ_NOTNULL(vg, VSM_SEG_MAGIC);

	if (vg->set->vg == vg) {
		AZ(vg->flags & VSM_FLAG_STALE);
		vg->set->vg = VTAILQ_NEXT(vg, list);
	}

	if (refsok && vg->refs) {
		AZ(vg->flags & VSM_FLAG_STALE);
		vg->flags |= VSM_FLAG_STALE;
		VTAILQ_REMOVE(&vg->set->segs, vg, list);
		VTAILQ_INSERT_TAIL(&vg->set->stale, vg, list);
		return;
	}

	if (vg->s != NULL)
		vsm_unmapseg(vg);

	if (vg->flags & VSM_FLAG_CLUSTER) {
		vg->flags &= ~VSM_FLAG_CLUSTER;
		VTAILQ_REMOVE(&vg->set->clusters, vg, clist);
	}

	if (vg->flags & VSM_FLAG_STALE)
		VTAILQ_REMOVE(&vg->set->stale, vg, list);
	else
		VTAILQ_REMOVE(&vg->set->segs, vg, list);
	VAV_Free(vg->av);
	FREE_OBJ(vg);
}

/*--------------------------------------------------------------------*/

static struct vsm_set *
vsm_newset(const char *dirname)
{
	struct vsm_set *vs;

	ALLOC_OBJ(vs, VSM_SET_MAGIC);
	AN(vs);
	VTAILQ_INIT(&vs->segs);
	VTAILQ_INIT(&vs->stale);
	VTAILQ_INIT(&vs->clusters);
	vs->dname = dirname;
	vs->dfd = vs->fd = -1;
	vs->vlu = VLU_New(vsm_vlu_func, vs, 0);
	AN(vs->vlu);
	return (vs);
}

static void
vsm_delset(struct vsm_set **p)
{
	struct vsm_set *vs;
	struct vsm_seg *vg;

	TAKE_OBJ_NOTNULL(vs, p, VSM_SET_MAGIC);

	if (vs->fd >= 0)
		closefd(&vs->fd);
	if (vs->dfd >= 0)
		closefd(&vs->dfd);
	while ((vg = VTAILQ_FIRST(&vs->stale)) != NULL) {
		AN(vg->flags & VSM_FLAG_STALE);
		vsm_delseg(vg, 0);
	}
	while ((vg = VTAILQ_FIRST(&vs->segs)) != NULL) {
		AZ(vg->flags & VSM_FLAG_STALE);
		vsm_delseg(vg, 0);
	}
	assert(VTAILQ_EMPTY(&vs->clusters));
	VLU_Destroy(&vs->vlu);
	FREE_OBJ(vs);
}

static void
vsm_wash_set(const struct vsm_set *vs, int all)
{
	struct vsm_seg *vg, *vg2;

	VTAILQ_FOREACH_SAFE(vg, &vs->segs, list, vg2) {
		if (all || (vg->flags & VSM_FLAG_MARKSCAN) == 0)
			vsm_delseg(vg, 1);
	}
}

/*--------------------------------------------------------------------*/

struct vsm *
VSM_New(void)
{
	struct vsm *vd;

	ALLOC_OBJ(vd, VSM_MAGIC);
	AN(vd);

	vd->mgt = vsm_newset(VSM_MGT_DIRNAME);
	vd->mgt->flag_running = VSM_MGT_RUNNING;
	vd->mgt->flag_changed = VSM_MGT_CHANGED;
	vd->mgt->flag_restarted = VSM_MGT_RESTARTED;

	vd->child = vsm_newset(VSM_CHILD_DIRNAME);
	vd->child->flag_running = VSM_WRK_RUNNING;
	vd->child->flag_changed = VSM_WRK_CHANGED;
	vd->child->flag_restarted = VSM_WRK_RESTARTED;

	vd->mgt->vsm = vd;
	vd->child->vsm = vd;
	vd->wdfd = -1;
	vd->patience = 5;
	return (vd);
}

/*--------------------------------------------------------------------*/

int
VSM_Arg(struct vsm *vd, char flag, const char *arg)
{
	char *p = NULL;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);

	if (arg == NULL)
		return (1);
	switch (flag) {
	case 't':
		if (!strcasecmp(arg, "off")) {
			vd->patience = -1;
		} else {
			vd->patience = strtod(arg, &p);
			if ((p != NULL && *p != '\0') ||
			    !isfinite(vd->patience) || vd->patience < 0)
				return (vsm_diag(vd,
				    "-t: Invalid argument: %s", arg));
		}
		break;
	case 'n':
		if (vd->wdname != NULL)
			free(vd->wdname);
		vd->wdname = VIN_n_Arg(arg);
		if (vd->wdname == NULL)
			return (vsm_diag(vd, "Invalid instance name: %s",
			    strerror(errno)));
		break;
	default:
		return (vsm_diag(vd, "Unknown VSM_Arg('%c')", flag));
	}
	return (1);
}

/*--------------------------------------------------------------------*/

void
VSM_Destroy(struct vsm **vdp)
{
	struct vsm *vd;

	TAKE_OBJ_NOTNULL(vd, vdp, VSM_MAGIC);

	VSM_ResetError(vd);
	REPLACE(vd->wdname, NULL);
	if (vd->diag != NULL)
		VSB_destroy(&vd->diag);
	if (vd->wdfd >= 0)
		closefd(&vd->wdfd);
	vsm_delset(&vd->mgt);
	vsm_delset(&vd->child);
	FREE_OBJ(vd);
}

/*--------------------------------------------------------------------*/

const char *
VSM_Error(const struct vsm *vd)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);

	if (vd->diag == NULL)
		return ("No VSM error");
	else
		return (VSB_data(vd->diag));
}

/*--------------------------------------------------------------------*/

void
VSM_ResetError(struct vsm *vd)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);

	if (vd->diag == NULL)
		return;
	VSB_destroy(&vd->diag);
}

/*--------------------------------------------------------------------
 */

static int
vsm_cmp_av(char * const *a1, char * const *a2)
{

	while (1) {
		if (*a1 == NULL && *a2 == NULL)
			return (0);
		if (*a1 == NULL || *a2 == NULL)
			return (1);
		if (strcmp(*a1, *a2))
			return (1);
		a1++;
		a2++;
	}
}

static struct vsm_seg *
vsm_findcluster(const struct vsm_set *vs, const char *cnam)
{
	struct vsm_seg *vg;
	AN(vs);
	AN(cnam);
	VTAILQ_FOREACH(vg, &vs->clusters, clist) {
		AN(vg->av[1]);
		if (!strcmp(cnam, vg->av[1]))
			return (vg);
	}
	return (NULL);
}

static int
vsm_vlu_hash(struct vsm *vd, struct vsm_set *vs, const char *line)
{
	int i;
	uintmax_t id1, id2;

	(void)vd;

	i = sscanf(line, "# %ju %ju", &id1, &id2);
	if (i != 2) {
		vs->retval |= VSM_MGT_RESTARTED | VSM_MGT_CHANGED;
		return (0);
	}
	vs->retval |= VSM_MGT_RUNNING;
	if (id1 != vs->id1 || id2 != vs->id2) {
		vs->retval |= VSM_MGT_RESTARTED | VSM_MGT_CHANGED;
		vs->id1 = id1;
		vs->id2 = id2;
	}
	return (0);
}

static int
vsm_vlu_plus(struct vsm *vd, struct vsm_set *vs, const char *line)
{
	char **av;
	int ac;
	struct vsm_seg *vg;

	av = VAV_Parse(line + 1, &ac, 0);

	if (av[0] != NULL || ac < 4 || ac > 6) {
		(void)(vsm_diag(vd, "vsm_vlu_plus: bad index (%d/%s)",
		    ac, av[0]));
		VAV_Free(av);
		return (-1);
	}

	vg = vs->vg;
	CHECK_OBJ_ORNULL(vg, VSM_SEG_MAGIC);
	if (vg != NULL)
		AZ(vg->flags & VSM_FLAG_STALE);
	while (vg != NULL && vsm_cmp_av(&vg->av[1], &av[1]))
		vg = VTAILQ_NEXT(vg, list);
	if (vg != NULL) {
		/* entry compared equal, so it survives */
		CHECK_OBJ_NOTNULL(vg, VSM_SEG_MAGIC);
		VAV_Free(av);
		vg->flags |= VSM_FLAG_MARKSCAN;
		vs->vg = VTAILQ_NEXT(vg, list);
	} else {
		ALLOC_OBJ(vg, VSM_SEG_MAGIC);
		AN(vg);
		vg->av = av;
		vg->set = vs;
		vg->flags = VSM_FLAG_MARKSCAN;
		vg->serial = vd->serial;

		VTAILQ_INSERT_TAIL(&vs->segs, vg, list);
		if (ac == 4) {
			vg->flags |= VSM_FLAG_CLUSTER;
			VTAILQ_INSERT_TAIL(&vs->clusters, vg, clist);
		} else if (*vg->av[2] != '0') {
			vg->cluster = vsm_findcluster(vs, vg->av[1]);
			CHECK_OBJ_NOTNULL(vg->cluster, VSM_SEG_MAGIC);
		}
	}
	return (0);
}

static int
vsm_vlu_minus(struct vsm *vd, const struct vsm_set *vs, const char *line)
{
	char **av;
	int ac;
	struct vsm_seg *vg;

	av = VAV_Parse(line + 1, &ac, 0);

	if (av[0] != NULL || ac < 4 || ac > 6) {
		(void)(vsm_diag(vd, "vsm_vlu_minus: bad index (%d/%s)",
		    ac, av[0]));
		VAV_Free(av);
		return (-1);
	}

	/* Clustered segments cannot come before their cluster */
	if (*av[2] != '0')
		vg = vsm_findcluster(vs, av[1]);
	else
		vg = VTAILQ_FIRST(&vs->segs);

	for (;vg != NULL; vg = VTAILQ_NEXT(vg, list)) {
		if (!vsm_cmp_av(&vg->av[1], &av[1])) {
			vsm_delseg(vg, 1);
			break;
		}
	}
	AN(vg);
	VAV_Free(av);
	return (0);
}

static int v_matchproto_(vlu_f)
vsm_vlu_func(void *priv, const char *line)
{
	struct vsm *vd;
	struct vsm_set *vs;
	int i = 0;

	CAST_OBJ_NOTNULL(vs, priv, VSM_SET_MAGIC);
	vd = vs->vsm;
	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	AN(line);

	/* Up the serial counter. This wraps at UINTPTR_MAX/2
	 * because thats the highest value we can store in struct
	 * vsm_fantom. */
	vd->serial = VSM_PRIV_LOW(vd->serial + 1);

	switch (line[0]) {
	case '#':
		i = vsm_vlu_hash(vd, vs, line);
		VTAILQ_FOREACH(vs->vg, &vs->segs, list)
			vs->vg->flags &= ~VSM_FLAG_MARKSCAN;
		if (!(vs->retval & vs->flag_restarted))
			vs->vg = VTAILQ_FIRST(&vs->segs);
		break;
	case '+':
		i = vsm_vlu_plus(vd, vs, line);
		break;
	case '-':
		i = vsm_vlu_minus(vd, vs, line);
		break;
	default:
		break;
	}
	return (i);
}

static void
vsm_readlines(struct vsm_set *vs)
{
	int i;

	do {
		assert(vs->fd >= 0);
		i = VLU_Fd(vs->vlu, vs->fd);
	} while (!i);
	assert(i == -2);
}

static unsigned
vsm_refresh_set(struct vsm *vd, struct vsm_set *vs)
{
	struct stat st;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	CHECK_OBJ_NOTNULL(vs, VSM_SET_MAGIC);
	vs->retval = 0;
	if (vs->dfd >= 0 && (
	    fstatat(vd->wdfd, vs->dname, &st, AT_SYMLINK_NOFOLLOW) ||
	    st.st_ino != vs->dst.st_ino ||
	    st.st_dev != vs->dst.st_dev ||
	    st.st_mode != vs->dst.st_mode ||
	    st.st_nlink == 0)) {
		closefd(&vs->dfd);
	}

	if (vs->dfd < 0) {
		if (vs->fd >= 0)
			closefd(&vs->fd);
		vs->dfd = openat(vd->wdfd, vs->dname, O_RDONLY);
	}

	if (vs->dfd < 0) {
		vs->id1 = vs->id2 = 0;
		vsm_wash_set(vs, 1);
		return (vs->retval | vs->flag_restarted);
	}

	AZ(fstat(vs->dfd, &vs->dst));

	if (vs->fd >= 0 && (
	    fstatat(vs->dfd, "_.index", &st, AT_SYMLINK_NOFOLLOW) ||
	    st.st_ino != vs->fst.st_ino ||
	    st.st_dev != vs->fst.st_dev ||
	    st.st_mode != vs->fst.st_mode ||
	    st.st_size < vs->fst.st_size ||
	    st.st_nlink < 1)) {
		closefd(&vs->fd);
	}

	if (vs->fd >= 0) {
		vs->vg = NULL;
		vsm_readlines(vs);
	} else {
		VTAILQ_FOREACH(vs->vg, &vs->segs, list)
			vs->vg->flags &= ~VSM_FLAG_MARKSCAN;
		vs->vg = VTAILQ_FIRST(&vs->segs);
		vs->fd = openat(vs->dfd, "_.index", O_RDONLY);
		if (vs->fd < 0)
			return (vs->retval | vs->flag_restarted);
		VLU_Reset(vs->vlu);
		AZ(fstat(vs->fd, &vs->fst));
		vsm_readlines(vs);
		vsm_wash_set(vs, 0);
	}

	vs->fst.st_size = lseek(vs->fd, 0L, SEEK_CUR);

	vs->retval |= vs->flag_running;
	return (vs->retval);
}

/*--------------------------------------------------------------------*/

unsigned
VSM_Status(struct vsm *vd)
{
	unsigned retval = 0;
	struct stat st;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);

	/* See if the -n workdir changed */
	if (vd->wdfd >= 0) {
		AZ(fstat(vd->wdfd, &st));
		if (st.st_ino != vd->wdst.st_ino ||
		    st.st_dev != vd->wdst.st_dev ||
		    st.st_mode != vd->wdst.st_mode ||
		    st.st_nlink == 0) {
			closefd(&vd->wdfd);
			vsm_wash_set(vd->mgt, 1);
			vsm_wash_set(vd->child, 1);
		}
	}

	/* Open workdir */
	if (vd->wdfd < 0) {
		retval |= VSM_MGT_RESTARTED | VSM_MGT_CHANGED;
		retval |= VSM_WRK_RESTARTED | VSM_MGT_CHANGED;
		vd->wdfd = open(vd->wdname, O_RDONLY);
		if (vd->wdfd < 0)
			(void)vsm_diag(vd,
			    "VSM_Status: Cannot open workdir");
		else
			AZ(fstat(vd->wdfd, &vd->wdst));
	}

	if (vd->wdfd >= 0) {
		retval |= vsm_refresh_set(vd, vd->mgt);
		if (retval & VSM_MGT_RUNNING)
			retval |= vsm_refresh_set(vd, vd->child);
	}
	return (retval);
}

/*--------------------------------------------------------------------*/

int
VSM_Attach(struct vsm *vd, int progress)
{
	double t0;
	unsigned u;
	int i, n = 0;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);

	if (vd->patience < 0)
		t0 = DBL_MAX;
	else
		t0 = VTIM_mono() + vd->patience;

	if (vd->wdname == NULL) {
		/* Use default (hostname) */
		i = VSM_Arg(vd, 'n', "");
		if (i < 0)
			return (i);
		AN(vd->wdname);
	}

	AZ(vd->attached);
	while (!VSIG_int && !VSIG_term) {
		u = VSM_Status(vd);
		VSM_ResetError(vd);
		if (u & VSM_MGT_RUNNING) {
			if (progress >= 0 && n > 4)
				(void)write(progress, "\n", 1);
			vd->attached = 1;
			return (0);
		}
		if (t0 < VTIM_mono()) {
			if (progress >= 0 && n > 4)
				(void)write(progress, "\n", 1);
			return (vsm_diag(vd,
			    "Could not get hold of varnishd, is it running?"));
		}
		if (progress >= 0 && !(++n % 4))
			(void)write(progress, ".", 1);
		VTIM_sleep(.25);
	}
	return (vsm_diag(vd, "Attach interrupted"));
}

/*--------------------------------------------------------------------*/

static struct vsm_seg *
vsm_findseg(const struct vsm *vd, const struct vsm_fantom *vf)
{
	struct vsm_set *vs;
	struct vsm_seg *vg;
	uintptr_t x;

	x = VSM_PRIV_HIGH(vf->priv);
	if (x == vd->serial) {
		vg = (struct vsm_seg *)vf->priv2;
		if (!VALID_OBJ(vg, VSM_SEG_MAGIC) ||
		    vg->serial != VSM_PRIV_LOW(vf->priv))
			WRONG("Corrupt fantom");
		return (vg);
	}

	x = VSM_PRIV_LOW(vf->priv);
	vs = vd->mgt;
	VTAILQ_FOREACH(vg, &vs->segs, list)
		if (vg->serial == x)
			break;
	if (vg == NULL) {
		VTAILQ_FOREACH(vg, &vs->stale, list)
			if (vg->serial == x)
				break;
	}
	vs = vd->child;
	if (vg == NULL) {
		VTAILQ_FOREACH(vg, &vs->segs, list)
			if (vg->serial == x)
				break;
	}
	if (vg == NULL) {
		VTAILQ_FOREACH(vg, &vs->stale, list)
			if (vg->serial == x)
				break;
	}
	if (vg == NULL)
		return (NULL);

	/* Update the fantom with the new priv so that lookups will be
	 * fast on the next call. Note that this casts away the const. */
	((struct vsm_fantom *)TRUST_ME(vf))->priv =
	    VSM_PRIV_MERGE(vg->serial, vd->serial);
	return (vg);
}

/*--------------------------------------------------------------------*/

void
VSM__iter0(const struct vsm *vd, struct vsm_fantom *vf)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	AN(vf);

	AN(vd->attached);
	memset(vf, 0, sizeof *vf);
}

int
VSM__itern(struct vsm *vd, struct vsm_fantom *vf)
{
	struct vsm_seg *vg;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	AN(vd->attached);
	AN(vf);

	if (vf->priv == 0) {
		vg = VTAILQ_FIRST(&vd->mgt->segs);
		if (vg == NULL)
			return (0);
	} else {
		vg = vsm_findseg(vd, vf);
		if (vg == NULL)
			return (vsm_diag(vd, "VSM_FOREACH: inconsistency"));
		while (1) {
			if (vg->set == vd->mgt && VTAILQ_NEXT(vg, list) == NULL)
				vg = VTAILQ_FIRST(&vd->child->segs);
			else
				vg = VTAILQ_NEXT(vg, list);
			if (vg == NULL)
				return (0);
			if (!(vg->flags & VSM_FLAG_CLUSTER))
				break;
		}
	}
	memset(vf, 0, sizeof *vf);
	vf->priv = VSM_PRIV_MERGE(vg->serial, vd->serial);
	vf->priv2 = (uintptr_t)vg;
	vf->class = vg->av[4];
	vf->ident = vg->av[5];
	AN(vf->class);
	return (1);
}

/*--------------------------------------------------------------------*/

int
VSM_Map(struct vsm *vd, struct vsm_fantom *vf)
{
	struct vsm_seg *vg, *vgc;
	size_t of, sz;
	int r;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	AN(vd->attached);
	AN(vf);
	vg = vsm_findseg(vd, vf);
	if (vg == NULL)
		return (vsm_diag(vd, "VSM_Map: bad fantom"));

	assert(vg->serial == VSM_PRIV_LOW(vf->priv));
	assert(vg->av[4] == vf->class);
	assert(vg->av[5] == vf->ident);

	if (vg->b != NULL) {
		assert(vg->refs > 0);
		AN(vg->e);
		vf->b = vg->b;
		vf->e = vg->e;
		vg->refs++;
		return (0);
	}

	assert(vg->refs == 0);

	vgc = vg->cluster;

	if (vgc == NULL) {
		r = vsm_mapseg(vd, vg);
		if (r)
			return (r);
		vf->b = vg->b;
		vf->e = vg->e;

		vg->refs++;

		return (0);
	}

	CHECK_OBJ_NOTNULL(vgc, VSM_SEG_MAGIC);
	assert(vgc->flags & VSM_FLAG_CLUSTER);
	assert(vg->s == NULL);
	assert(vg->sz == 0);

	r = vsm_mapseg(vd, vgc);
	if (r)
		return (r);
	vgc->refs++;

	of = strtoul(vg->av[2], NULL, 10);
	sz = strtoul(vg->av[3], NULL, 10);
	assert(sz > 0);

	assert(vgc->sz >= of + sz);
	assert(vgc->s == vgc->b);
	vg->b = (char *)vgc->b + of;
	vg->e = (char *)vg->b + sz;

	vf->b = vg->b;
	vf->e = vg->e;

	vg->refs++;

	return (0);
}

/*--------------------------------------------------------------------*/

int
VSM_Unmap(struct vsm *vd, struct vsm_fantom *vf)
{
	struct vsm_seg *vg;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	AN(vd->attached);
	AN(vf);
	AN(vf->b);
	vg = vsm_findseg(vd, vf);
	if (vg == NULL)
		return (vsm_diag(vd, "VSM_Unmap: bad fantom"));
	CHECK_OBJ_NOTNULL(vg, VSM_SEG_MAGIC);
	assert(vg->refs > 0);
	vg->refs--;
	vf->b = NULL;
	vf->e = NULL;
	if (vg->refs > 0)
		return (0);

	if (vg->cluster) {
		CHECK_OBJ_NOTNULL(vg->cluster, VSM_SEG_MAGIC);
		assert(vg->s == NULL);
		assert(vg->sz == 0);
		assert(vg->cluster->refs > 0);
		if (--vg->cluster->refs == 0) {
			vsm_unmapseg(vg->cluster);
			if (vg->cluster->flags & VSM_FLAG_STALE) {
				AN(vg->flags & VSM_FLAG_STALE);
				vsm_delseg(vg->cluster, 0);
			}
		}
		vg->b = vg->e = NULL;
	} else {
		vsm_unmapseg(vg);
	}
	if (vg->flags & VSM_FLAG_STALE)
		vsm_delseg(vg, 0);
	return (0);
}

/*--------------------------------------------------------------------*/

const struct vsm_valid *
VSM_StillValid(const struct vsm *vd, const struct vsm_fantom *vf)
{
	struct vsm_seg *vg;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	AN(vf);
	vg = vsm_findseg(vd, vf);
	if (vg == NULL || vg->flags & VSM_FLAG_STALE)
		return (VSM_invalid);
	return (VSM_valid);
}

/*--------------------------------------------------------------------*/

int
VSM_Get(struct vsm *vd, struct vsm_fantom *vf,
    const char *class, const char *ident)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	AN(vd->attached);
	VSM_FOREACH(vf, vd) {
		if (strcmp(vf->class, class))
			continue;
		if (ident != NULL && strcmp(vf->ident, ident))
			continue;
		return (1);
	}
	memset(vf, 0, sizeof *vf);
	return (0);
}

/*--------------------------------------------------------------------*/

char *
VSM_Dup(struct vsm *vd, const char *class, const char *ident)
{
	struct vsm_fantom vf;
	char *p = NULL;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	AN(vd->attached);
	VSM_FOREACH(&vf, vd) {
		if (strcmp(vf.class, class))
			continue;
		if (ident != NULL && strcmp(vf.ident, ident))
			continue;
		AZ(VSM_Map(vd, &vf));
		AN(vf.b);
		AN(vf.e);
		p = malloc((char*)vf.e - (char*)vf.b);
		AN(p);
		memcpy(p, vf.b, (char*)vf.e - (char*)vf.b);
		AZ(VSM_Unmap(vd, &vf));
		break;
	}
	return (p);
}
