/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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

#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <math.h>
#include <signal.h>
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
#include "vsb.h"
#include "vsm_priv.h"
#include "vqueue.h"
#include "vtim.h"

#include "vapi/vsm.h"

#ifndef MAP_HASSEMAPHORE
#  define MAP_HASSEMAPHORE 0 /* XXX Linux */
#endif

#ifndef MAP_NOSYNC
#  define MAP_NOSYNC 0 /* XXX Linux */
#endif

const struct vsm_valid VSM_invalid[1] = {{"invalid"}};
const struct vsm_valid VSM_valid[1] = {{"valid"}};

/*--------------------------------------------------------------------*/

struct vsm_set;

struct vsm_seg {
	unsigned		magic;
#define VSM_SEG_MAGIC		0xeb6c6dfd
	VTAILQ_ENTRY(vsm_seg)	list;
	int			markscan;
	int			stale;
	struct vsm_set		*set;
	char			**av;
	int			refs;
	void			*b;
	void			*e;
	size_t			sz;
	uintptr_t		serial;
};

struct vsm_set {
	unsigned		magic;
#define VSM_SET_MAGIC		0xdee401b8
	const char		*dname;
	VTAILQ_HEAD(,vsm_seg)	segs;
	VTAILQ_HEAD(,vsm_seg)	stale;

	int			dfd;
	struct stat		dst;

	int			fd;
	struct stat		fst;

	uintmax_t		id1, id2;
};

struct vsm {
	unsigned		magic;
#define VSM_MAGIC		0x6e3bd69b

	struct vsb		*diag;
	uintptr_t		serial;

	int			dfd;
	struct stat		dst;
	char			*dname;

	struct vsm_set		*mgt;
	struct vsm_set		*child;

	int			attached;
	double			patience;

	int			couldkill;
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

static void
vsm_unmapseg(struct vsm_seg *vg)
{
	size_t sz, ps, len;

	CHECK_OBJ_NOTNULL(vg, VSM_SEG_MAGIC);

	AN(vg->b);
	AN(vg->e);
	sz = strtoul(vg->av[2], NULL, 10);
	assert(sz > 0);
	ps = getpagesize();
	len = RUP2(sz, ps);
	AZ(munmap(vg->b, len));
	vg->b = vg->e = NULL;
}

/*--------------------------------------------------------------------*/

static void
vsm_delseg(struct vsm_seg *vg)
{

	CHECK_OBJ_NOTNULL(vg, VSM_SEG_MAGIC);

	if (vg->b != NULL)
		vsm_unmapseg(vg);

	if (vg->stale)
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
	vs->dname = dirname;
	vs->dfd = vs->fd = -1;
	return (vs);
}

static void
vsm_delset(struct vsm_set **p)
{
	struct vsm_set *vs;

	AN(p);
	vs = *p;
	*p = NULL;
	if (vs->fd >= 0)
		closefd(&vs->fd);
	if (vs->dfd >= 0)
		closefd(&vs->dfd);
	while (!VTAILQ_EMPTY(&vs->stale))
		vsm_delseg(VTAILQ_FIRST(&vs->stale));
	while (!VTAILQ_EMPTY(&vs->segs))
		vsm_delseg(VTAILQ_FIRST(&vs->segs));
	FREE_OBJ(vs);
}

/*--------------------------------------------------------------------*/

struct vsm *
VSM_New(void)
{
	struct vsm *vd;

	ALLOC_OBJ(vd, VSM_MAGIC);
	AN(vd);

	vd->mgt = vsm_newset(VSM_MGT_DIRNAME);
	vd->child = vsm_newset(VSM_CHILD_DIRNAME);
	vd->dfd = -1;
	vd->patience = 5;
	if (getenv("VSM_NOPID") != NULL)
		vd->couldkill = -1;
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
		if (VIN_n_Arg(arg, &p))
			return (vsm_diag(vd, "Invalid instance name: %s",
			    strerror(errno)));
		AN(p);
		REPLACE(vd->dname, p);
		free(p);
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
	REPLACE(vd->dname, NULL);
	if (vd->diag != NULL)
		VSB_destroy(&vd->diag);
	if (vd->dfd >= 0)
		closefd(&vd->dfd);
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

#define VSM_NUKE_ALL	(1U << 16)

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

static unsigned
vsm_refresh_set2(struct vsm *vd, struct vsm_set *vs, struct vsb *vsb)
{
	unsigned retval = 0;
	struct stat st;
	char buf[BUFSIZ];
	ssize_t sz;
	int i, ac;
	char *p, *e;
	uintmax_t id1, id2;
	char **av;
	struct vsm_seg *vg, *vg2;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	CHECK_OBJ_NOTNULL(vs, VSM_SET_MAGIC);
	if (vs->dfd >= 0) {
		if (fstatat(vd->dfd, vs->dname, &st, AT_SYMLINK_NOFOLLOW)) {
			closefd(&vs->dfd);
			vs->id1 = vs->id2 = 0;
			return (VSM_MGT_RESTARTED|VSM_NUKE_ALL);
		}
		if (st.st_ino != vs->dst.st_ino ||
		    st.st_dev != vs->dst.st_dev ||
		    st.st_mode != vs->dst.st_mode) {
			closefd(&vs->dfd);
			vs->id1 = vs->id2 = 0;
		}
	}

	if (vs->dfd < 0) {
		if (vs->fd >= 0)
			closefd(&vs->fd);
		vs->dfd = openat(vd->dfd, vs->dname, O_RDONLY);
		retval |= VSM_MGT_RESTARTED;
		if (vs->dfd < 0) {
			vs->id1 = vs->id2 = 0;
			return (retval|VSM_NUKE_ALL);
		}
		AZ(fstat(vs->dfd, &vs->dst));
	}

	if (vs->fd >= 0 && (
	    fstatat(vs->dfd, "_.index", &st, AT_SYMLINK_NOFOLLOW) ||
	    st.st_ino != vs->fst.st_ino ||
	    st.st_dev != vs->fst.st_dev ||
	    st.st_mode != vs->fst.st_mode ||
	    st.st_size != vs->fst.st_size ||
	    st.st_nlink < 1 ||
	    memcmp(&st.st_mtime, &vs->fst.st_mtime, sizeof st.st_mtime))) {
		closefd(&vs->fd);
	}

	if (vs->fd >= 0) {
		if (vd->couldkill < 1 || !kill(vs->id1, 0))
			retval |= VSM_MGT_RUNNING;
		return (retval);
	}

	retval |= VSM_MGT_CHANGED;
	vs->fd = openat(vs->dfd, "_.index", O_RDONLY);
	if (vs->fd < 0)
		return (retval|VSM_MGT_RESTARTED);

	AZ(fstat(vs->fd, &vs->fst));

	VSB_clear(vsb);
	do {
		sz = read(vs->fd, buf, sizeof buf);
		if (sz > 0)
			VSB_bcat(vsb, buf, sz);
	} while (sz > 0);
	AZ(VSB_finish(vsb));

	vs->fst.st_size = VSB_len(vsb);

	if (VSB_len(vsb) == 0)
		return (retval|VSM_NUKE_ALL);

	/*
	 * Examine the ident line
	 * XXX: for now ignore that one of the ID's is a pid which could
	 * XXX: be kill(pid,0)'ed for more rapid abandonment detection.
	 */
	i = sscanf(VSB_data(vsb), "# %ju %ju\n%n", &id1, &id2, &ac);
	if (i != 2) {
		retval |= VSM_MGT_RESTARTED | VSM_MGT_CHANGED;
		return (retval);
	}
	if (vd->couldkill >= 0 && !kill(id1, 0)) {
		vd->couldkill = 1;
	} else if (vd->couldkill > 0 && errno == ESRCH) {
		retval |= VSM_MGT_RESTARTED | VSM_MGT_CHANGED;
		return (retval);
	}
	retval |= VSM_MGT_RUNNING;
	if (id1 != vs->id1 || id2 != vs->id2) {
		retval |= VSM_MGT_RESTARTED | VSM_MGT_CHANGED;
		vs->id1 = id1;
		vs->id2 = id2;
	}
	p = VSB_data(vsb) + ac;

	VTAILQ_FOREACH(vg, &vs->segs, list)
		vg->markscan = 0;

	/*
	 * Efficient comparison walking the two lists side-by-side is ok because
	 * segment inserts always happen at the tail (VSMW_Allocv()). So, as
	 * soon as vg is exhausted, we only insert.
	 *
	 * For restarts, we require a tabula rasa
	 */

	if (retval & VSM_MGT_RESTARTED)
		vg = NULL;
	else
		vg = VTAILQ_FIRST(&vs->segs);

	while (p != NULL && *p != '\0') {
		e = strchr(p, '\n');
		if (e == NULL)
			break;
		*e = '\0';
		av = VAV_Parse(p, &ac, 0);
		p = e + 1;

		if (av[0] != NULL || ac < 4 || ac > 5) {
			(void)(vsm_diag(vd,
			    "vsm_refresh_set2: bad index (%d/%s)",
			    ac, av[0]));
			VAV_Free(av);
			break;
		}

		if (vg == NULL) {
			ALLOC_OBJ(vg2, VSM_SEG_MAGIC);
			AN(vg2);
			vg2->av = av;
			vg2->set = vs;
			vg2->markscan = 1;
			vg2->serial = ++vd->serial;
			VTAILQ_INSERT_TAIL(&vs->segs, vg2, list);
			continue;
		}

		while (vg != NULL && vsm_cmp_av(&vg->av[1], &av[1])) {
			vg = VTAILQ_NEXT(vg, list);
		}

		VAV_Free(av);

		if (vg == NULL)
			continue;

		/* entry compared equal, so it survives */
		vg->markscan = 1;
		vg = VTAILQ_NEXT(vg, list);
	}
	return (retval);
}

static unsigned
vsm_refresh_set(struct vsm *vd, struct vsm_set *vs, struct vsb *vsb)
{
	unsigned retval;
	struct vsm_seg *vg, *vg2;

	retval = vsm_refresh_set2(vd, vs, vsb);
	if (retval & VSM_NUKE_ALL)
		retval |= VSM_MGT_CHANGED;
	VTAILQ_FOREACH_SAFE(vg, &vs->segs, list, vg2) {
		if (!vg->markscan || (retval & VSM_NUKE_ALL)) {
			VTAILQ_REMOVE(&vs->segs, vg, list);
			if (vg->refs) {
				vg->stale = 1;
				VTAILQ_INSERT_TAIL(&vs->stale, vg, list);
			} else {
				VAV_Free(vg->av);
				FREE_OBJ(vg);
			}
		}
	}
	return (retval & ~VSM_NUKE_ALL);
}

/*--------------------------------------------------------------------*/

unsigned
VSM_Status(struct vsm *vd)
{
	unsigned retval = 0, u;
	struct stat st;
	struct vsb *vsb;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);

	/* See if the -n workdir changed */
	if (vd->dfd >= 0) {
		AZ(fstat(vd->dfd, &st));
		if (st.st_ino != vd->dst.st_ino ||
		    st.st_dev != vd->dst.st_dev ||
		    st.st_mode != vd->dst.st_mode ||
		    st.st_nlink == 0) {
			closefd(&vd->dfd);
			retval |= VSM_MGT_CHANGED;
			retval |= VSM_WRK_CHANGED;
		}
	}

	/* Open workdir */
	if (vd->dfd < 0) {
		vd->dfd = open(vd->dname, O_RDONLY);
		if (vd->dfd < 0)
			(void)vsm_diag(vd,
			    "VSM_Status: Cannot open workdir");
		else
			AZ(fstat(vd->dfd, &vd->dst));
	}

	vsb = VSB_new_auto();
	AN(vsb);

	u = vsm_refresh_set(vd, vd->mgt, vsb);
	retval |= u;
	if (u & VSM_MGT_RUNNING)
		retval |= vsm_refresh_set(vd, vd->child, vsb) << 8;
	VSB_destroy(&vsb);
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

	if (vd->dname == NULL) {
		/* Use default (hostname) */
		i = VSM_Arg(vd, 'n', "");
		if (i < 0)
			return (i);
		AN(vd->dname);
	}

	AZ(vd->attached);
	while (1) {
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
}

/*--------------------------------------------------------------------*/

static struct vsm_seg *
vsm_findseg(const struct vsm *vd, const struct vsm_fantom *vf)
{
	struct vsm_set *vs;
	struct vsm_seg *vg;
	uintptr_t x;

	x = vf->priv;
	vs = vd->mgt;
	VTAILQ_FOREACH(vg, &vs->segs, list)
		if (vg->serial == x)
			return (vg);
	VTAILQ_FOREACH(vg, &vs->stale, list)
		if (vg->serial == x)
			return (vg);
	vs = vd->child;
	VTAILQ_FOREACH(vg, &vs->segs, list)
		if (vg->serial == x)
			return (vg);
	VTAILQ_FOREACH(vg, &vs->stale, list)
		if (vg->serial == x)
			return (vg);
	return (NULL);
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
	struct vsm_seg *vg, *vg2;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	AN(vd->attached);
	AN(vf);

	if (vf->priv == 0) {
		vg2 = VTAILQ_FIRST(&vd->mgt->segs);
	} else {
		vg = vsm_findseg(vd, vf);
		if (vg == NULL)
			return (vsm_diag(vd, "VSM_FOREACH: inconsistency"));
		vg2 = VTAILQ_NEXT(vg, list);
		if (vg2 == NULL && vg->set == vd->mgt)
			vg2 = VTAILQ_FIRST(&vd->child->segs);
	}
	if (vg2 == NULL)
		return (0);
	memset(vf, 0, sizeof *vf);
	vf->priv = vg2->serial;
	vf->class = vg2->av[3];
	vf->ident = vg2->av[4];
	return (1);
}

/*--------------------------------------------------------------------*/

int
VSM_Map(struct vsm *vd, struct vsm_fantom *vf)
{
	struct vsm_seg *vg;
	size_t sz, ps, len;
	struct vsb *vsb;
	int fd;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	AN(vd->attached);
	AN(vf);
	vg = vsm_findseg(vd, vf);
	if (vg == NULL)
		return (vsm_diag(vd, "VSM_Map: bad fantom"));

	assert(vg->serial == vf->priv);
	assert(vg->av[3] == vf->class);
	assert(vg->av[4] == vf->ident);

	if (vg->b != NULL) {
		assert(vg->refs > 0);
		AN(vg->e);
		vf->b = vg->b;
		vf->e = vg->e;
		vg->refs++;
		return (0);
	}

	sz = strtoul(vg->av[2], NULL, 10);
	assert(sz > 0);
	ps = getpagesize();
	len = RUP2(sz, ps);

	vsb = VSB_new_auto();
	AN(vsb);
	VSB_printf(vsb, "%s/%s/%s", vd->dname, vg->set->dname, vg->av[1]);
	AZ(VSB_finish(vsb));

	fd = open(VSB_data(vsb), O_RDONLY);	// XXX: openat
	if (fd < 0) {
		VSB_destroy(&vsb);
		return (vsm_diag(vd, "Could not open segment"));
	}

	vg->b = (void*)mmap(NULL, len,
	    PROT_READ,
	    MAP_HASSEMAPHORE | MAP_NOSYNC | MAP_SHARED,
	    fd, 0);

	VSB_destroy(&vsb);


	closefd(&fd);
	if (vg->b == MAP_FAILED)
		return (vsm_diag(vd, "Could not mmap segment"));
	vg->e = (char *)vg->b + sz;
	vg->sz = len;

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
	assert(vg->refs > 0);
	vg->refs--;
	vf->b = NULL;
	vf->e = NULL;
	if (vg->refs > 0)
		return(0);
	vsm_unmapseg(vg);
	if (vg->stale)
		vsm_delseg(vg);
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
	if (vg == NULL || vg->stale)
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
