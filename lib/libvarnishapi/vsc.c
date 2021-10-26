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

#include <sys/stat.h>

#include <fnmatch.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vdef.h"
#include "vas.h"
#include "miniobj.h"
#include "vqueue.h"
#include "vjsn.h"
#include "vsb.h"
#include "vsc_priv.h"
#include "vmb.h"

#include "vapi/vsc.h"
#include "vapi/vsm.h"

struct vsc_sf_mode {
	const char		*name;
	unsigned		include;
	unsigned		fail;
	unsigned		append;
};

static const struct vsc_sf_mode VSC_SF_INCLUDE[1] = {{"include", 1, 1, 1}};
static const struct vsc_sf_mode VSC_SF_EXCLUDE[1] = {{"exclude", 0, 0, 1}};
static const struct vsc_sf_mode VSC_SF_REQUIRE[1] = {{"require", 1, 0, 0}};

struct vsc_sf {
	unsigned			magic;
#define VSC_SF_MAGIC			0x558478dd
	VTAILQ_ENTRY(vsc_sf)		list;
	char				*pattern;
	const struct vsc_sf_mode	*mode;
};

VTAILQ_HEAD(vsc_sf_head, vsc_sf);

struct vsc_pt {
	struct VSC_point	point;
	char			*name;
};

struct vsc_seg {
	unsigned		magic;
#define VSC_SEG_MAGIC		0x801177d4
	VTAILQ_ENTRY(vsc_seg)	list;
	struct vsm_fantom	fantom[1];
	struct vsc_head		*head;
	char			*body;

	struct vjsn		*vj;

	unsigned		npoints;
	struct vsc_pt		*points;
	int			exposed;
};

struct vsc {
	unsigned		magic;
#define VSC_MAGIC		0x3373554a

	unsigned		raw;
	struct vsc_sf_head	sf_list;
	VTAILQ_HEAD(,vsc_seg)	segs;

	VSC_new_f		*fnew;
	VSC_destroy_f		*fdestroy;
	void			*priv;
};

/*--------------------------------------------------------------------
 * Build the static level, type and point descriptions
 */

enum vsc_levels {
#define VSC_LEVEL_F(v,l,e,d) v,
#include "tbl/vsc_levels.h"
};

static const struct VSC_level_desc levels[] = {
#define VSC_LEVEL_F(v,l,e,d) [v] = {#v, l, e, d},
#include "tbl/vsc_levels.h"
};

static const ssize_t nlevels = sizeof(levels)/sizeof(*levels);

/*--------------------------------------------------------------------*/

struct vsc *
VSC_New(void)
{
	struct vsc *vsc;

	ALLOC_OBJ(vsc, VSC_MAGIC);
	if (vsc == NULL)
		return (vsc);
	VTAILQ_INIT(&vsc->sf_list);
	VTAILQ_INIT(&vsc->segs);
	return (vsc);
}

/*--------------------------------------------------------------------*/

static int
vsc_sf_arg(struct vsc *vsc, const char *glob, const struct vsc_sf_mode *mode)
{
	struct vsc_sf *sf;

	CHECK_OBJ_NOTNULL(vsc, VSC_MAGIC);
	AN(glob);
	AN(mode);

	ALLOC_OBJ(sf, VSC_SF_MAGIC);
	AN(sf);
	REPLACE(sf->pattern, glob);
	sf->mode = mode;
	AN(mode->name);
	if (mode->append)
		VTAILQ_INSERT_TAIL(&vsc->sf_list, sf, list);
	else
		VTAILQ_INSERT_HEAD(&vsc->sf_list, sf, list);
	return (1);
}

static int
vsc_f_arg(struct vsc *vsc, const char *opt)
{

	CHECK_OBJ_NOTNULL(vsc, VSC_MAGIC);
	AN(opt);

	if (opt[0] == '^')
		return (vsc_sf_arg(vsc, opt + 1, VSC_SF_EXCLUDE));
	return (vsc_sf_arg(vsc, opt, VSC_SF_INCLUDE));
}

/*--------------------------------------------------------------------*/

int
VSC_Arg(struct vsc *vsc, char arg, const char *opt)
{

	CHECK_OBJ_NOTNULL(vsc, VSC_MAGIC);

	switch (arg) {
	case 'I': return (vsc_sf_arg(vsc, opt, VSC_SF_INCLUDE));
	case 'X': return (vsc_sf_arg(vsc, opt, VSC_SF_EXCLUDE));
	case 'R': return (vsc_sf_arg(vsc, opt, VSC_SF_REQUIRE));
	case 'f': return (vsc_f_arg(vsc, opt));
	case 'r': vsc->raw = !vsc->raw; return (1);
	default: return (0);
	}
}

unsigned
VSC_IsRaw(const struct vsc *vsc)
{

	CHECK_OBJ_NOTNULL(vsc, VSC_MAGIC);
	return (vsc->raw);
}

/*--------------------------------------------------------------------
 */

static int
vsc_filter(const struct vsc *vsc, const char *nm)
{
	struct vsc_sf *sf;
	unsigned res = 0;

	CHECK_OBJ_NOTNULL(vsc, VSC_MAGIC);
	VTAILQ_FOREACH(sf, &vsc->sf_list, list) {
		if (!fnmatch(sf->pattern, nm, 0))
			return (!sf->mode->include);
		res |= sf->mode->fail;
	}
	return (res);
}

/*--------------------------------------------------------------------
 */

static void
vsc_clean_point(struct vsc_pt *point)
{
	REPLACE(point->name, NULL);
}

static void
vsc_fill_point(const struct vsc *vsc, const struct vsc_seg *seg,
    const struct vjsn_val *vv, struct vsb *vsb, struct vsc_pt *point)
{
	struct vjsn_val *vt;

	CHECK_OBJ_NOTNULL(vsc, VSC_MAGIC);
	memset(point, 0, sizeof *point);

	vt = vjsn_child(vv, "name");
	AN(vt);
	assert(vjsn_is_string(vt));

	VSB_clear(vsb);
	VSB_printf(vsb, "%s.%s", seg->fantom->ident, vt->value);
	AZ(VSB_finish(vsb));

	if (vsc_filter(vsc, VSB_data(vsb)))
		return;

	point->name = strdup(VSB_data(vsb));
	AN(point->name);
	point->point.name = point->name;

#define DOF(n, k)				\
	vt = vjsn_child(vv, k);			\
	AN(vt);					\
	assert(vjsn_is_string(vt));		\
	point->point.n = vt->value;

	DOF(ctype, "ctype");
	DOF(sdesc, "oneliner");
	DOF(ldesc, "docs");
#undef DOF
	vt = vjsn_child(vv, "type");
	AN(vt);
	assert(vjsn_is_string(vt));

	if (!strcmp(vt->value, "counter")) {
		point->point.semantics = 'c';
	} else if (!strcmp(vt->value, "gauge")) {
		point->point.semantics = 'g';
	} else if (!strcmp(vt->value, "bitmap")) {
		point->point.semantics = 'b';
	} else {
		point->point.semantics = '?';
	}

	vt = vjsn_child(vv, "format");
	AN(vt);
	assert(vjsn_is_string(vt));

	if (!strcmp(vt->value, "integer")) {
		point->point.format = 'i';
	} else if (!strcmp(vt->value, "bytes")) {
		point->point.format = 'B';
	} else if (!strcmp(vt->value, "bitmap")) {
		point->point.format = 'b';
	} else if (!strcmp(vt->value, "duration")) {
		point->point.format = 'd';
	} else {
		point->point.format = '?';
	}

	vt = vjsn_child(vv, "level");
	AN(vt);
	assert(vjsn_is_string(vt));

	if (!strcmp(vt->value, "info"))  {
		point->point.level = &levels[info];
	} else if (!strcmp(vt->value, "diag")) {
		point->point.level = &levels[diag];
	} else if (!strcmp(vt->value, "debug")) {
		point->point.level = &levels[debug];
	} else {
		WRONG("Illegal level");
	}

	vt = vjsn_child(vv, "index");
	AN(vt);

	point->point.ptr = (volatile void*)(seg->body + atoi(vt->value));
	point->point.raw = vsc->raw;
}

static void
vsc_del_seg(const struct vsc *vsc, struct vsm *vsm, struct vsc_seg **spp)
{
	unsigned u;
	struct vsc_pt *pp;
	struct vsc_seg *sp;

	CHECK_OBJ_NOTNULL(vsc, VSC_MAGIC);
	TAKE_OBJ_NOTNULL(sp, spp, VSC_SEG_MAGIC);
	AZ(VSM_Unmap(vsm, sp->fantom));
	if (sp->vj != NULL) {
		vjsn_delete(&sp->vj);
	} else {
		pp = sp->points;
		for (u = 0; u < sp->npoints; u++, pp++)
			vsc_clean_point(pp);
		free(sp->points);
	}
	FREE_OBJ(sp);
}

static struct vsc_seg *
vsc_add_seg(const struct vsc *vsc, struct vsm *vsm, const struct vsm_fantom *fp)
{
	struct vsc_seg *sp, *spd;
	const char *e;
	struct vjsn_val *vv, *vve;
	struct vsb *vsb;
	struct vsc_pt *pp;

	CHECK_OBJ_NOTNULL(vsc, VSC_MAGIC);

	ALLOC_OBJ(sp, VSC_SEG_MAGIC);
	AN(sp);
	*sp->fantom = *fp;
	if (VSM_Map(vsm, sp->fantom)) {
		/*
		 * If the seg was removed between our call to VSM_Status()
		 * and now, we won't be able to map it.
		 */
		FREE_OBJ(sp);
		return (NULL);
	}
	sp->head = sp->fantom->b;
	if (sp->head->ready == 0) {
		VRMB();
		usleep(100000);
	}
	assert(sp->head->ready > 0);
	sp->body = (char*)sp->fantom->b + sp->head->body_offset;

	if (!strcmp(fp->class, VSC_CLASS)) {
		VTAILQ_FOREACH(spd, &vsc->segs, list)
			if (spd->head->doc_id == sp->head->doc_id)
				break;
		AN(spd);
		// XXX: Refcount ?
		vve = vjsn_child(spd->vj->value, "elements");
		AN(vve);
		sp->npoints = strtoul(vve->value, NULL, 0);
		sp->points = calloc(sp->npoints, sizeof *sp->points);
		AN(sp->points);
		vsb = VSB_new_auto();
		AN(vsb);
		vve = vjsn_child(spd->vj->value, "elem");
		AN(vve);
		pp = sp->points;
		VTAILQ_FOREACH(vv, &vve->children, list) {
			vsc_fill_point(vsc, sp, vv, vsb, pp);
			pp++;
		}
		VSB_destroy(&vsb);
		return (sp);
	}
	assert(!strcmp(fp->class, VSC_DOC_CLASS));
	sp->vj = vjsn_parse(sp->body, &e);
	XXXAZ(e);
	AN(sp->vj);
	return (sp);
}

/*--------------------------------------------------------------------
 */

static void
vsc_expose(const struct vsc *vsc, struct vsc_seg *sp, int del)
{
	struct vsc_pt *pp;
	unsigned u;
	int expose;

	if (vsc->fnew != NULL && !sp->exposed &&
	    !del && sp->head->ready == 1)
		expose = 1;
	else if (vsc->fdestroy != NULL && sp->exposed &&
	    (del || sp->head->ready == 2))
		expose = 0;
	else
		return;

	pp = sp->points;
	for (u = 0; u < sp->npoints; u++, pp++) {
		if (pp->name == NULL)
			continue;
		if (expose)
			pp->point.priv = vsc->fnew(vsc->priv, &pp->point);
		else
			vsc->fdestroy(vsc->priv, &pp->point);
	}
	sp->exposed = expose;
}

/*--------------------------------------------------------------------
 */

static int
vsc_iter_seg(const struct vsc *vsc, const struct vsc_seg *sp,
    VSC_iter_f *fiter, void *priv)
{
	unsigned u;
	int i = 0;
	struct vsc_pt *pp;

	CHECK_OBJ_NOTNULL(vsc, VSC_MAGIC);
	CHECK_OBJ_NOTNULL(sp, VSC_SEG_MAGIC);
	AN(fiter);
	pp = sp->points;
	for (u = 0; u < sp->npoints && i == 0; u++, pp++) {
		if (pp->name != NULL)
			i = fiter(priv, &pp->point);
	}
	return (i);
}

int
VSC_Iter(struct vsc *vsc, struct vsm *vsm, VSC_iter_f *fiter, void *priv)
{
	struct vsm_fantom ifantom;
	struct vsc_seg *sp, *sp2;
	int i = 0;

	CHECK_OBJ_NOTNULL(vsc, VSC_MAGIC);
	AN(vsm);
	sp = VTAILQ_FIRST(&vsc->segs);
	VSM_FOREACH(&ifantom, vsm) {
		AN(ifantom.class);
		if (strcmp(ifantom.class, VSC_CLASS) &&
		    strcmp(ifantom.class, VSC_DOC_CLASS))
			continue;
		while (sp != NULL &&
		    (strcmp(ifantom.ident, sp->fantom->ident) ||
		    VSM_StillValid(vsm, sp->fantom) != VSM_valid)) {
			sp2 = sp;
			sp = VTAILQ_NEXT(sp, list);
			VTAILQ_REMOVE(&vsc->segs, sp2, list);
			vsc_expose(vsc, sp2, 1);
			vsc_del_seg(vsc, vsm, &sp2);
		}
		if (sp == NULL) {
			sp = vsc_add_seg(vsc, vsm, &ifantom);
			if (sp != NULL) {
				VTAILQ_INSERT_TAIL(&vsc->segs, sp, list);
				vsc_expose(vsc, sp, 0);
			}
		} else {
			vsc_expose(vsc, sp, 0);
		}
		if (sp != NULL) {
			if (fiter != NULL && sp->head->ready < 2)
				i = vsc_iter_seg(vsc, sp, fiter, priv);
			sp = VTAILQ_NEXT(sp, list);
		}

		if (i)
			break;
	}
	while (sp != NULL) {
		sp2 = sp;
		sp = VTAILQ_NEXT(sp, list);
		VTAILQ_REMOVE(&vsc->segs, sp2, list);
		vsc_expose(vsc, sp2, 1);
		vsc_del_seg(vsc, vsm, &sp2);
	}
	return (i);
}

/*--------------------------------------------------------------------
 */

void
VSC_State(struct vsc *vsc, VSC_new_f *fn, VSC_destroy_f *fd, void *priv)
{
	struct vsc_seg *sp;

	CHECK_OBJ_NOTNULL(vsc, VSC_MAGIC);
	assert((fn == NULL && fd == NULL) || (fn != NULL && fd != NULL));
	if (fd == NULL) {
		VTAILQ_FOREACH(sp, &vsc->segs, list)
			vsc_expose(vsc, sp, 1);
	}
	vsc->fnew = fn;
	vsc->fdestroy = fd;
	vsc->priv = priv;
}

/*--------------------------------------------------------------------
 */

const struct VSC_level_desc *
VSC_ChangeLevel(const struct VSC_level_desc *old, int chg)
{
	int i;

	if (old == NULL)
		old = &levels[0];
	for (i = 0; i < nlevels; i++)
		if (old == &levels[i])
			break;
	if (i == nlevels)
		i = 0;

	i += chg;
	if (i >= nlevels)
		i = nlevels - 1;
	if (i < 0)
		i = 0;
	return (&levels[i]);
}

/*--------------------------------------------------------------------*/

void
VSC_Destroy(struct vsc **vscp, struct vsm *vsm)
{
	struct vsc *vsc;
	struct vsc_sf *sf, *sf2;
	struct vsc_seg *sp, *sp2;

	TAKE_OBJ_NOTNULL(vsc, vscp, VSC_MAGIC);

	VTAILQ_FOREACH_SAFE(sf, &vsc->sf_list, list, sf2) {
		CHECK_OBJ_NOTNULL(sf, VSC_SF_MAGIC);
		VTAILQ_REMOVE(&vsc->sf_list, sf, list);
		free(sf->pattern);
		FREE_OBJ(sf);
	}
	VTAILQ_FOREACH_SAFE(sp, &vsc->segs, list, sp2) {
		VTAILQ_REMOVE(&vsc->segs, sp, list);
		vsc_expose(vsc, sp, 1);
		vsc_del_seg(vsc, vsm, &sp);
	}
	FREE_OBJ(vsc);
}

