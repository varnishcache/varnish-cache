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

#include <sys/stat.h>

#include <fnmatch.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vdef.h"
#include "vas.h"
#include "miniobj.h"
#include "vqueue.h"
#include "vjsn.h"
#include "vsb.h"
#include "vend.h"

#include "vapi/vsc.h"
#include "vapi/vsm.h"

#include "vsc_priv.h"

struct vsc_vf {
	unsigned		magic;
#define VSC_VF_MAGIC		0x516519f8
	VTAILQ_ENTRY(vsc_vf)	list;
	struct VSM_fantom	fantom;
	struct VSC_section	section;
	struct vjsn		*vjsn;
	int			order;
};
VTAILQ_HEAD(vsc_vf_head, vsc_vf);

struct vsc_pt {
	unsigned		magic;
#define VSC_PT_MAGIC		0xa4ff159a
	VTAILQ_ENTRY(vsc_pt)	list;
	struct VSC_point	point;
};
VTAILQ_HEAD(vsc_pt_head, vsc_pt);

struct vsc_sf {
	unsigned		magic;
#define VSC_SF_MAGIC		0x558478dd
	VTAILQ_ENTRY(vsc_sf)	list;
	char			*pattern;
};
VTAILQ_HEAD(vsc_sf_head, vsc_sf);

struct vsc {
	unsigned		magic;
#define VSC_MAGIC		0x3373554a

	struct vsc_vf_head	vf_list;
	struct vsc_pt_head	pt_list;
	struct vsc_sf_head	sf_list_include;
	struct vsc_sf_head	sf_list_exclude;
	struct VSM_fantom	iter_fantom;
};

/*--------------------------------------------------------------------
 * Build the static level, type and point descriptions
 */

#define VSC_LEVEL_F(v,l,e,d) \
    static const struct VSC_level_desc level_##v = {#v, l, e, d};
#include "tbl/vsc_levels.h"

static const struct VSC_level_desc * const levels[] = {
#define VSC_LEVEL_F(v,l,e,d) &level_##v,
#include "tbl/vsc_levels.h"
#undef VSC_LEVEL_F
};

static const size_t nlevels =
    sizeof(levels)/sizeof(*levels);

/*--------------------------------------------------------------------*/

static struct vsc *
vsc_setup(struct vsm *vd)
{
	struct vsc *vsc;

	vsc = VSM_GetVSC(vd);
	if (vsc == NULL) {
		ALLOC_OBJ(vsc, VSC_MAGIC);
		AN(vsc);
		VTAILQ_INIT(&vsc->vf_list);
		VTAILQ_INIT(&vsc->pt_list);
		VTAILQ_INIT(&vsc->sf_list_include);
		VTAILQ_INIT(&vsc->sf_list_exclude);
		VSM_SetVSC(vd, vsc);
	}
	CHECK_OBJ_NOTNULL(vsc, VSC_MAGIC);
	return (vsc);
}

/*--------------------------------------------------------------------*/

static void
vsc_delete_vf_list(struct vsc_vf_head *head)
{
	struct vsc_vf *vf;

	while (!VTAILQ_EMPTY(head)) {
		vf = VTAILQ_FIRST(head);
		CHECK_OBJ_NOTNULL(vf, VSC_VF_MAGIC);
		VTAILQ_REMOVE(head, vf, list);
		FREE_OBJ(vf);
	}
}

static void
vsc_delete_pt_list(struct vsc_pt_head *head)
{
	struct vsc_pt *pt;

	while (!VTAILQ_EMPTY(head)) {
		pt = VTAILQ_FIRST(head);
		CHECK_OBJ_NOTNULL(pt, VSC_PT_MAGIC);
		VTAILQ_REMOVE(head, pt, list);
		FREE_OBJ(pt);
	}
}

static void
vsc_delete_sf_list(struct vsc_sf_head *head)
{
	struct vsc_sf *sf;

	while (!VTAILQ_EMPTY(head)) {
		sf = VTAILQ_FIRST(head);
		CHECK_OBJ_NOTNULL(sf, VSC_SF_MAGIC);
		VTAILQ_REMOVE(head, sf, list);
		free(sf->pattern);
		FREE_OBJ(sf);
	}
}

void
VSC_Delete(struct vsc *vsc)
{

	CHECK_OBJ_NOTNULL(vsc, VSC_MAGIC);
	vsc_delete_sf_list(&vsc->sf_list_include);
	vsc_delete_sf_list(&vsc->sf_list_exclude);
	vsc_delete_pt_list(&vsc->pt_list);
	vsc_delete_vf_list(&vsc->vf_list);
	FREE_OBJ(vsc);
}

/*--------------------------------------------------------------------*/

static int
vsc_f_arg(struct vsm *vd, const char *opt)
{
	struct vsc *vsc = vsc_setup(vd);
	struct vsc_sf *sf;
	unsigned exclude = 0;

	AN(vd);
	AN(opt);

	ALLOC_OBJ(sf, VSC_SF_MAGIC);
	AN(sf);

	if (opt[0] == '^') {
		exclude = 1;
		opt++;
	}

	sf->pattern = strdup(opt);
	AN(sf->pattern);

	if (exclude)
		VTAILQ_INSERT_TAIL(&vsc->sf_list_exclude, sf, list);
	else
		VTAILQ_INSERT_TAIL(&vsc->sf_list_include, sf, list);

	return (1);
}

/*--------------------------------------------------------------------*/

int
VSC_Arg(struct vsm *vd, int arg, const char *opt)
{

	switch (arg) {
	case 'f': return (vsc_f_arg(vd, opt));
	case 'n': return (VSM_n_Arg(vd, opt));
	default:
		return (0);
	}
}

/*--------------------------------------------------------------------*/

static struct vsc_vf *
vsc_add_vf(struct vsc *vsc, const struct VSM_fantom *fantom, int order)
{
	struct vsc_vf *vf, *vf2;
	struct vsb *vsb;

	ALLOC_OBJ(vf, VSC_VF_MAGIC);
	AN(vf);
	vf->fantom = *fantom;
	vsb = VSB_new_auto();
	AN(vsb);
	VSB_printf(vsb, "%s", vf->fantom.type);
	if (*vf->fantom.ident != '\0')
		VSB_printf(vsb, ".%s", vf->fantom.ident);
	AZ(VSB_finish(vsb));
	REPLACE(vf->section.ident, VSB_data(vsb));
	VSB_destroy(&vsb);
	vf->order = order;

	VTAILQ_FOREACH(vf2, &vsc->vf_list, list) {
		if (vf->order < vf2->order)
			break;
	}
	if (vf2 != NULL)
		VTAILQ_INSERT_BEFORE(vf2, vf, list);
	else
		VTAILQ_INSERT_TAIL(&vsc->vf_list, vf, list);
	return (vf);
}

/*lint -esym(528, vsc_add_pt) */
/*lint -sem(vsc_add_pt, custodial(3)) */
static void
vsc_add_pt(struct vsc *vsc, const volatile void *ptr,
    const struct VSC_desc *desc, const struct vsc_vf *vf)
{
	struct vsc_pt *pt;

	ALLOC_OBJ(pt, VSC_PT_MAGIC);
	AN(pt);

	pt->point.desc = desc;
	pt->point.ptr = ptr;
	pt->point.section = &vf->section;

	VTAILQ_INSERT_TAIL(&vsc->pt_list, pt, list);
}

/*--------------------------------------------------------------------
 */

static void
vsc_build_vf_list(struct vsm *vd)
{
	uint64_t u;
	struct vsc *vsc = vsc_setup(vd);
	const char *p;
	const char *e;
	struct vjsn *vj;
	struct vjsn_val *vv;
	struct vsc_vf *vf;

	vsc_delete_pt_list(&vsc->pt_list);
	vsc_delete_vf_list(&vsc->vf_list);

	VSM_FOREACH(&vsc->iter_fantom, vd) {
		if (strcmp(vsc->iter_fantom.class, VSC_CLASS))
			continue;
		AZ(VSM_Map(vd, &vsc->iter_fantom));
		u = vbe64dec(vsc->iter_fantom.b);
		assert(u > 0);
		p = (char*)vsc->iter_fantom.b + 8 + u;
		vj = vjsn_parse(p, &e);
		if (e != NULL) {
			fprintf(stderr, "%s\n", p);
			fprintf(stderr, "JSON ERROR %s\n", e);
			exit(2);
		}
		AN(vj);
		vv = vjsn_child(vj->value, "order");
		AN(vv);
		assert(vv->type == VJSN_NUMBER);
		vf = vsc_add_vf(vsc, &vsc->iter_fantom, atoi(vv->value));
		AN(vf);
		vf->vjsn = vj;
		// vjsn_dump(vf->vjsn, stderr);
		AZ(e);
	}
}

static void
vsc_build_pt_list(struct vsm *vd)
{
	struct vsc *vsc = vsc_setup(vd);
	struct vsc_vf *vf;
	struct vjsn_val *vve, *vv, *vt;
	struct VSC_desc *vdsc = NULL;

	vsc_delete_pt_list(&vsc->pt_list);

	VTAILQ_FOREACH(vf, &vsc->vf_list, list) {
		vve = vjsn_child(vf->vjsn->value, "elem");
		AN(vve);
		VTAILQ_FOREACH(vv, &vve->children, list) {
			vdsc = calloc(sizeof *vdsc, 1);
			AN(vdsc);

#define DOF(n, k)						\
			vt = vjsn_child(vv, k);			\
			AN(vt);					\
			assert(vt->type == VJSN_STRING);	\
			vdsc->n = vt->value;

			DOF(name,  "name");
			DOF(ctype, "ctype");
			DOF(sdesc, "oneliner");
			DOF(ldesc, "docs");
#undef DOF
			vt = vjsn_child(vv, "type");
			AN(vt);
			assert(vt->type == VJSN_STRING);

			if (!strcmp(vt->value, "counter")) {
				vdsc->semantics = 'c';
			} else if (!strcmp(vt->value, "gauge")) {
				vdsc->semantics = 'g';
			} else if (!strcmp(vt->value, "bitmap")) {
				vdsc->semantics = 'b';
			} else {
				vdsc->semantics = '?';
			}

			vt = vjsn_child(vv, "format");
			AN(vt);
			assert(vt->type == VJSN_STRING);

			if (!strcmp(vt->value, "integer")) {
				vdsc->format = 'i';
			} else if (!strcmp(vt->value, "bytes")) {
				vdsc->format = 'B';
			} else if (!strcmp(vt->value, "bitmap")) {
				vdsc->format = 'b';
			} else if (!strcmp(vt->value, "duration")) {
				vdsc->format = 'd';
			} else {
				vdsc->format = '?';
			}

			vdsc->level = &level_info;

			vt = vjsn_child(vv, "index");
			AN(vt);
			vsc_add_pt(vsc,
			    (char*)vf->fantom.b + atoi(vt->value),
			    vdsc, vf);
		}
	}
}

/*--------------------------------------------------------------------
 */

static int
vsc_filter_match_pt(struct vsb *vsb, const struct vsc_sf *sf, const
    struct vsc_pt *pt)
{
	VSB_clear(vsb);
	if (strcmp(pt->point.section->ident, ""))
		VSB_printf(vsb, "%s.", pt->point.section->ident);
	VSB_printf(vsb, "%s", pt->point.desc->name);
	AZ(VSB_finish(vsb));
	return (!fnmatch(sf->pattern, VSB_data(vsb), 0));
}

static void
vsc_filter_pt_list(struct vsm *vd)
{
	struct vsc *vsc = vsc_setup(vd);
	struct vsc_pt_head tmplist;
	struct vsc_sf *sf;
	struct vsc_pt *pt, *pt2;
	struct vsb *vsb;

	if (VTAILQ_EMPTY(&vsc->sf_list_include) &&
	    VTAILQ_EMPTY(&vsc->sf_list_exclude))
		return;

	vsb = VSB_new_auto();
	AN(vsb);
	VTAILQ_INIT(&tmplist);

	/* Include filters. Empty include filter list implies one that
	 * matches everything. Points are sorted by the order of include
	 * filter they match. */
	if (!VTAILQ_EMPTY(&vsc->sf_list_include)) {
		VTAILQ_FOREACH(sf, &vsc->sf_list_include, list) {
			CHECK_OBJ_NOTNULL(sf, VSC_SF_MAGIC);
			VTAILQ_FOREACH_SAFE(pt, &vsc->pt_list, list, pt2) {
				CHECK_OBJ_NOTNULL(pt, VSC_PT_MAGIC);
				if (vsc_filter_match_pt(vsb, sf, pt)) {
					VTAILQ_REMOVE(&vsc->pt_list,
					    pt, list);
					VTAILQ_INSERT_TAIL(&tmplist,
					    pt, list);
				}
			}
		}
		vsc_delete_pt_list(&vsc->pt_list);
		VTAILQ_CONCAT(&vsc->pt_list, &tmplist, list);
	}

	/* Exclude filters */
	VTAILQ_FOREACH(sf, &vsc->sf_list_exclude, list) {
		CHECK_OBJ_NOTNULL(sf, VSC_SF_MAGIC);
		VTAILQ_FOREACH_SAFE(pt, &vsc->pt_list, list, pt2) {
			CHECK_OBJ_NOTNULL(pt, VSC_PT_MAGIC);
			if (vsc_filter_match_pt(vsb, sf, pt)) {
				VTAILQ_REMOVE(&vsc->pt_list, pt, list);
				VTAILQ_INSERT_TAIL(&tmplist, pt, list);
			}
		}
	}
	vsc_delete_pt_list(&tmplist);

	VSB_destroy(&vsb);
}

/*--------------------------------------------------------------------
 */

int
VSC_Iter(struct vsm *vd, struct VSM_fantom *fantom, VSC_iter_f *func,
    void *priv)
{
	struct vsc *vsc = vsc_setup(vd);
	struct vsc_pt *pt;
	int i;

	if (VSM_valid != VSM_StillValid(vd, &vsc->iter_fantom)) {
		/* Tell app that list will be nuked */
		(void)func(priv, NULL);
		vsc_build_vf_list(vd);
		vsc_build_pt_list(vd);
		vsc_filter_pt_list(vd);
	}
	if (fantom != NULL)
		*fantom = vsc->iter_fantom;
	VTAILQ_FOREACH(pt, &vsc->pt_list, list) {
		i = func(priv, &pt->point);
		if (i)
			return (i);
	}
	return (0);
}

/*--------------------------------------------------------------------
 */

const struct VSC_level_desc *
VSC_ChangeLevel(const struct VSC_level_desc *old, int chg)
{
	int i;

	if (old == NULL)
		old = levels[0];
	for (i = 0; i < nlevels; i++)
		if (old == levels[i])
			break;
	if (i == nlevels)
		i = 0;
	else
		i += chg;
	if (i >= nlevels)
		i = nlevels - 1;
	if (i < 0)
		i = 0;
	return (levels[i]);
}

