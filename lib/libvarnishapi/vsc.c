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

#include <sys/types.h>
#include <sys/stat.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vdef.h"
#include "vas.h"
#include "miniobj.h"
#include "vqueue.h"

#include "vapi/vsc.h"
#include "vapi/vsm.h"

#include "vsm_api.h"

enum {
#define VSC_TYPE_F(n,t,l,e,d) \
	VSC_type_order_##n,
#include "tbl/vsc_types.h"
#undef VSC_TYPE_F
};

struct vsc_vf {
	unsigned		magic;
#define VSC_VF_MAGIC		0x516519f8
	VTAILQ_ENTRY(vsc_vf)	list;
	struct VSM_fantom	fantom;
	struct VSC_section	section;
	int			order;
};

struct vsc_pt {
	unsigned		magic;
#define VSC_PT_MAGIC		0xa4ff159a
	VTAILQ_ENTRY(vsc_pt)	list;
	struct VSC_point	point;
};

struct vsc_sf {
	unsigned		magic;
#define VSC_SF_MAGIC		0x558478dd
	VTAILQ_ENTRY(vsc_sf)	list;
	int			flags;
#define VSC_SF_EXCL		(1 << 0)
#define VSC_SF_TY_WC		(1 << 1)
#define VSC_SF_ID_WC		(1 << 2)
#define VSC_SF_NM_WC		(1 << 3)
	char			*type;
	char			*ident;
	char			*name;
};

struct vsc {
	unsigned		magic;
#define VSC_MAGIC		0x3373554a

	VTAILQ_HEAD(, vsc_vf)	vf_list;
	VTAILQ_HEAD(, vsc_pt)	pt_list;
	VTAILQ_HEAD(, vsc_sf)	sf_list;
	struct VSM_fantom	iter_fantom;
};


/*--------------------------------------------------------------------*/

static struct vsc *
vsc_setup(struct VSM_data *vd)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	if (vd->vsc == NULL) {
		ALLOC_OBJ(vd->vsc, VSC_MAGIC);
		AN(vd->vsc);
		VTAILQ_INIT(&vd->vsc->vf_list);
		VTAILQ_INIT(&vd->vsc->pt_list);
		VTAILQ_INIT(&vd->vsc->sf_list);
	}
	CHECK_OBJ_NOTNULL(vd->vsc, VSC_MAGIC);
	return (vd->vsc);
}

/*--------------------------------------------------------------------*/

static void
vsc_delete_vf_list(struct vsc *vsc)
{
	struct vsc_vf *vf;

	while (!VTAILQ_EMPTY(&vsc->vf_list)) {
		vf = VTAILQ_FIRST(&vsc->vf_list);
		CHECK_OBJ_NOTNULL(vf, VSC_VF_MAGIC);
		VTAILQ_REMOVE(&vsc->vf_list, vf, list);
		FREE_OBJ(vf);
	}
}

static void
vsc_delete_pt_list(struct vsc *vsc)
{
	struct vsc_pt *pt;

	while (!VTAILQ_EMPTY(&vsc->pt_list)) {
		pt = VTAILQ_FIRST(&vsc->pt_list);
		CHECK_OBJ_NOTNULL(pt, VSC_PT_MAGIC);
		VTAILQ_REMOVE(&vsc->pt_list, pt, list);
		FREE_OBJ(pt);
	}
}

static void
vsc_delete_sf_list(struct vsc *vsc)
{
	struct vsc_sf *sf;

	while (!VTAILQ_EMPTY(&vsc->sf_list)) {
		sf = VTAILQ_FIRST(&vsc->sf_list);
		CHECK_OBJ_NOTNULL(sf, VSC_SF_MAGIC);
		VTAILQ_REMOVE(&vsc->sf_list, sf, list);
		free(sf->type);
		free(sf->ident);
		free(sf->name);
		FREE_OBJ(sf);
	}
}

void
VSC_Delete(struct VSM_data *vd)
{
	struct vsc *vsc;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	vsc = vd->vsc;
	vd->vsc = NULL;
	CHECK_OBJ_NOTNULL(vsc, VSC_MAGIC);
	vsc_delete_sf_list(vsc);
	vsc_delete_pt_list(vsc);
	vsc_delete_vf_list(vsc);
	FREE_OBJ(vsc);
}

/*--------------------------------------------------------------------*/

static int
vsc_f_arg(struct VSM_data *vd, const char *opt)
{
	struct vsc *vsc = vsc_setup(vd);
	struct vsc_sf *sf;
	const char *error = NULL;
	const char *p, *q;
	char *r;
	int i;
	int flags = 0;
	char *parts[3];

	AN(vd);
	AN(opt);

	if (opt[0] == '^') {
		flags |= VSC_SF_EXCL;
		opt++;
	}

	/* Split on '.' */
	memset(parts, 0, sizeof parts);
	for (i = 0, p = opt; *p != '\0'; i++) {
		for (q = p; *q != '\0' && *q != '.'; q++)
			if (*q == '\\')
				q++;
		if (i < 3) {
			parts[i] = malloc(1 + q - p);
			AN(parts[i]);
			memcpy(parts[i], p, q - p);
			parts[i][q - p] = '\0';
			p = r = parts[i];

			/* Unescape */
			while (1) {
				if (*p == '\\')
					p++;
				if (*p == '\0')
					break;
				*r++ = *p++;
			}
			*r = '\0';
		}
		p = q;
		if (*p == '.')
			p++;
	}
	if (i < 1 || i > 3) {
		(void)vsm_diag(vd, "-f: Wrong number of elements");
		for (i = 0; i < 3; i++)
			free(parts[i]);
		return (-1);
	}

	/* Set fields */
	ALLOC_OBJ(sf, VSC_SF_MAGIC);
	AN(sf);
	sf->flags = flags;
	AN(parts[0]);
	sf->type = parts[0];
	if (i == 2) {
		AN(parts[1]);
		sf->name = parts[1];
	} else if (i == 3) {
		AN(parts[1]);
		sf->ident = parts[1];
		AN(parts[2]);
		sf->name = parts[2];
	}

	/* Check for wildcards */
	if (sf->type != NULL) {
		r = strchr(sf->type, '*');
		if (r != NULL && r[1] == '\0') {
			*r = '\0';
			sf->flags |= VSC_SF_TY_WC;
		} else if (r != NULL)
			error = "-f: Wildcard not last";
	}
	if (sf->ident != NULL) {
		r = strchr(sf->ident, '*');
		if (r != NULL && r[1] == '\0') {
			*r = '\0';
			sf->flags |= VSC_SF_ID_WC;
		} else if (r != NULL)
			error = "-f: Wildcard not last";
	}
	if (sf->name != NULL) {
		r = strchr(sf->name, '*');
		if (r != NULL && r[1] == '\0') {
			*r = '\0';
			sf->flags |= VSC_SF_NM_WC;
		} else if (r != NULL)
			error = "-f: Wildcard not last";
	}

	if (error != NULL) {
		(void)vsm_diag(vd, "%s", error);
		free(sf->type);
		free(sf->ident);
		free(sf->name);
		FREE_OBJ(sf);
		return (-1);
	}

	VTAILQ_INSERT_TAIL(&vsc->sf_list, sf, list);
	return (1);
}

/*--------------------------------------------------------------------*/

int
VSC_Arg(struct VSM_data *vd, int arg, const char *opt)
{

	switch (arg) {
	case 'f': return (vsc_f_arg(vd, opt));
	case 'n': return (VSM_n_Arg(vd, opt));
	case 'N': return (VSM_N_Arg(vd, opt));
	default:
		return (0);
	}
}

/*--------------------------------------------------------------------*/

struct VSC_C_mgt *
VSC_Mgt(const struct VSM_data *vd, struct VSM_fantom *fantom)
{

	return (VSC_Get(vd, fantom, VSC_type_mgt, ""));
}

/*--------------------------------------------------------------------*/

struct VSC_C_main *
VSC_Main(const struct VSM_data *vd, struct VSM_fantom *fantom)
{

	return (VSC_Get(vd, fantom, VSC_type_main, ""));
}

/*--------------------------------------------------------------------
 */

void *
VSC_Get(const struct VSM_data *vd, struct VSM_fantom *fantom, const char *type,
    const char *ident)
{
	struct VSM_fantom f2 = VSM_FANTOM_NULL;

	if (fantom == NULL)
		fantom = &f2;
	if (VSM_invalid == VSM_StillValid(vd, fantom) &&
	    !VSM_Get(vd, fantom, VSC_CLASS, type, ident))
		return (NULL);
	return ((void*)fantom->b);
}

/*--------------------------------------------------------------------*/

static void
vsc_add_vf(struct vsc *vsc, const struct VSM_fantom *fantom,
    const struct VSC_type_desc *desc, int order)
{
	struct vsc_vf *vf, *vf2;

	ALLOC_OBJ(vf, VSC_VF_MAGIC);
	AN(vf);
	vf->fantom = *fantom;
	vf->section.type = vf->fantom.type;
	vf->section.ident = vf->fantom.ident;
	vf->section.desc = desc;
	vf->section.fantom = &vf->fantom;
	vf->order = order;

	VTAILQ_FOREACH(vf2, &vsc->vf_list, list) {
		if (vf->order < vf2->order)
			break;
	}
	if (vf2 != NULL)
		VTAILQ_INSERT_BEFORE(vf2, vf, list);
	else
		VTAILQ_INSERT_TAIL(&vsc->vf_list, vf, list);
}

/*lint -esym(528, vsc_add_pt) */
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

#define VSC_DO(U,l,t)							\
	static void							\
	iter_##l(struct vsc *vsc, const struct VSC_desc *descs,		\
	    struct vsc_vf *vf)						\
	{								\
		struct VSC_C_##l *st;					\
									\
		CHECK_OBJ_NOTNULL(vsc, VSC_MAGIC);			\
		st = vf->fantom.b;

#define VSC_F(nn,tt,ll,ss,ff,vv,dd,ee)					\
		vsc_add_pt(vsc, &st->nn, descs++, vf);

#define VSC_DONE(U,l,t)							\
	}

#include "tbl/vsc_all.h"
#undef VSC_DO
#undef VSC_F
#undef VSC_DONE

/*--------------------------------------------------------------------
 */

#include <stdio.h>

static void
vsc_build_vf_list(struct VSM_data *vd)
{
	struct vsc *vsc = vsc_setup(vd);

	vsc_delete_pt_list(vsc);
	vsc_delete_vf_list(vsc);

	VSM_FOREACH(&vsc->iter_fantom, vd) {
		if (strcmp(vsc->iter_fantom.class, VSC_CLASS))
			continue;
#define VSC_TYPE_F(n,t,l,e,d)						\
		if (!strcmp(vsc->iter_fantom.type, t))			\
			vsc_add_vf(vsc, &vsc->iter_fantom,		\
			    &VSC_type_desc_##n, VSC_type_order_##n);
#include "tbl/vsc_types.h"
#undef VSC_TYPE_F
	}
}

static void
vsc_build_pt_list(struct VSM_data *vd)
{
	struct vsc *vsc = vsc_setup(vd);
	struct vsc_vf *vf;

	vsc_delete_pt_list(vsc);

	VTAILQ_FOREACH(vf, &vsc->vf_list, list) {
#define VSC_DO(U,l,t)						\
		CHECK_OBJ_NOTNULL(vf, VSC_VF_MAGIC);		\
		if (!strcmp(vf->fantom.type, t))		\
			iter_##l(vsc, VSC_desc_##l, vf);
#define VSC_F(n,t,l,s,f,v,d,e)
#define VSC_DONE(a,b,c)
#include "tbl/vsc_all.h"
#undef VSC_DO
#undef VSC_F
#undef VSC_DONE
	}
}

/*--------------------------------------------------------------------
 */

static inline int
iter_test(const char *s1, const char *s2, int wc)
{

	if (s1 == NULL)
		return (0);
	if (!wc)
		return (strcmp(s1, s2));
	for (; *s1 != '\0' && *s1 == *s2; s1++, s2++)
		continue;
	return (*s1 != '\0');
}

static void
vsc_filter_pt_list(struct VSM_data *vd)
{
	struct vsc *vsc = vsc_setup(vd);
	struct vsc_sf *sf;
	struct vsc_pt *pt, *pt2;
	VTAILQ_HEAD(, vsc_pt)	pt_list;

	if (VTAILQ_EMPTY(&vsc->sf_list))
		return;

	VTAILQ_INIT(&pt_list);
	VTAILQ_FOREACH(sf, &vsc->sf_list, list) {
		CHECK_OBJ_NOTNULL(sf, VSC_SF_MAGIC);
		VTAILQ_FOREACH_SAFE(pt, &vsc->pt_list, list, pt2) {
			CHECK_OBJ_NOTNULL(pt, VSC_PT_MAGIC);
			if (iter_test(sf->type, pt->point.section->type,
			    sf->flags & VSC_SF_TY_WC))
				continue;
			if (iter_test(sf->ident, pt->point.section->ident,
			    sf->flags & VSC_SF_ID_WC))
				continue;
			if (iter_test(sf->name, pt->point.desc->name,
			    sf->flags & VSC_SF_NM_WC))
				continue;
			VTAILQ_REMOVE(&vsc->pt_list, pt, list);
			if (sf->flags & VSC_SF_EXCL) {
				FREE_OBJ(pt);
			} else {
				VTAILQ_INSERT_TAIL(&pt_list, pt, list);
			}
		}
	}
	vsc_delete_pt_list(vsc);
	VTAILQ_CONCAT(&vsc->pt_list, &pt_list, list);
}

/*--------------------------------------------------------------------
 */

int
VSC_Iter(struct VSM_data *vd, struct VSM_fantom *fantom, VSC_iter_f *func,
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
VSC_LevelDesc(unsigned level)
{
	switch (level) {
#define VSC_LEVEL_F(v,l,e,d)	\
	case VSC_level_##v:	\
		return (&VSC_level_desc_##v);
#include "tbl/vsc_levels.h"
#undef VSC_LEVEL_F
	default:
		return (NULL);
	}
}

/*--------------------------------------------------------------------
 * Build the static level, type and point descriptions
 */

#define VSC_LEVEL_F(v,l,e,d)			\
	const struct VSC_level_desc VSC_level_desc_##v = \
		{VSC_level_##v, l, e, d};
#include "tbl/vsc_levels.h"
#undef VSC_LEVEL_F

#define VSC_TYPE_F(n,t,l,e,d)	const char *VSC_type_##n = t;
#include "tbl/vsc_types.h"
#undef VSC_TYPE_F

#define VSC_TYPE_F(n,t,l,e,d)			\
	const struct VSC_type_desc VSC_type_desc_##n = {l,e,d};
#include "tbl/vsc_types.h"
#undef VSC_TYPE_F

#define VSC_DO(U,l,t)		const struct VSC_desc VSC_desc_##l[] = {
#define VSC_F(n,t,l,s,f,v,d,e)		{#n,#t,s,f,&VSC_level_desc_##v,d,e},
#define VSC_DONE(U,l,t)		};
#include "tbl/vsc_all.h"
#undef VSC_DO
#undef VSC_F
#undef VSC_DONE
