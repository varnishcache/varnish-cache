/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 */

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "miniobj.h"
#include "vas.h"
#include "vdef.h"

#include "vapi/vsc.h"
#include "vapi/vsm.h"
#include "vapi/vsm_int.h"
#include "vav.h"
#include "vqueue.h"
#include "vsm_api.h"

struct vsc_vf {
	unsigned		magic;
#define VSC_VF_MAGIC		0x516519f8
	VTAILQ_ENTRY(vsc_vf)	list;
	struct VSM_fantom	fantom;
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
	struct VSM_fantom	main_fantom;
	struct VSM_fantom	iter_fantom;
};


/*--------------------------------------------------------------------*/

static struct vsc *
vsc_setup(struct VSM_data *vd)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	if (vd->vsc == NULL) {
		ALLOC_OBJ(vd->vsc, VSC_MAGIC);
		VTAILQ_INIT(&vd->vsc->vf_list);
		VTAILQ_INIT(&vd->vsc->pt_list);
		VTAILQ_INIT(&vd->vsc->sf_list);
	}
	CHECK_OBJ_NOTNULL(vd->vsc, VSC_MAGIC);
	return (vd->vsc);
}

/*--------------------------------------------------------------------*/

static void
vsc_delete_pts(struct vsc *vsc)
{
	struct vsc_vf *vf;
	struct vsc_pt *pt;

	while (!VTAILQ_EMPTY(&vsc->pt_list)) {
		pt = VTAILQ_FIRST(&vsc->pt_list);
		VTAILQ_REMOVE(&vsc->pt_list, pt, list);
		FREE_OBJ(pt);
	}
	while (!VTAILQ_EMPTY(&vsc->vf_list)) {
		vf = VTAILQ_FIRST(&vsc->vf_list);
		VTAILQ_REMOVE(&vsc->vf_list, vf, list);
		FREE_OBJ(vf);
	}
}

static void
vsc_delete_sfs(struct vsc *vsc)
{
	struct vsc_sf *sf;

	while (!VTAILQ_EMPTY(&vsc->sf_list)) {
		sf = VTAILQ_FIRST(&vsc->sf_list);
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
	vsc_delete_sfs(vsc);
	vsc_delete_pts(vsc);
	FREE_OBJ(vsc);
}

/*--------------------------------------------------------------------*/

static int
vsc_f_arg(struct VSM_data *vd, const char *opt)
{
	struct vsc *vsc = vsc_setup(vd);
	struct vsc_sf *sf;
	char **av, *q, *p;
	int i;

	av = VAV_Parse(opt, NULL, ARGV_COMMA);
	AN(av);
	if (av[0] != NULL)
		return (vsm_diag(vd, "Parse error: %s", av[0]));
	for (i = 1; av[i] != NULL; i++) {
		ALLOC_OBJ(sf, VSC_SF_MAGIC);
		AN(sf);
		VTAILQ_INSERT_TAIL(&vsc->sf_list, sf, list);

		p = av[i];
		if (*p == '^') {
			sf->flags |= VSC_SF_EXCL;
			p++;
		}

		q = strchr(p, '.');
		if (q != NULL) {
			*q++ = '\0';
			if (*p != '\0')
				REPLACE(sf->type, p);
			p = q;
			if (*p != '\0') {
				q = strchr(p, '.');
				if (q != NULL) {
					*q++ = '\0';
					if (*p != '\0')
						REPLACE(sf->ident, p);
					p = q;
				}
			}
		}
		if (*p != '\0') {
			REPLACE(sf->name, p);
		}

		/* Check for wildcards */
		if (sf->type != NULL) {
			q = strchr(sf->type, '*');
			if (q != NULL && q[1] == '\0') {
				*q = '\0';
				sf->flags |= VSC_SF_TY_WC;
			}
		}
		if (sf->ident != NULL) {
			q = strchr(sf->ident, '*');
			if (q != NULL && q[1] == '\0') {
				*q = '\0';
				sf->flags |= VSC_SF_ID_WC;
			}
		}
		if (sf->name != NULL) {
			q = strchr(sf->name, '*');
			if (q != NULL && q[1] == '\0') {
				*q = '\0';
				sf->flags |= VSC_SF_NM_WC;
			}
		}
	}
	VAV_Free(av);
	return (1);
}

/*--------------------------------------------------------------------*/

int
VSC_Arg(struct VSM_data *vd, int arg, const char *opt)
{

	switch (arg) {
	case 'f': return (vsc_f_arg(vd, opt));
	case 'n': return (VSM_n_Arg(vd, opt));
	default:
		return (0);
	}
}

/*--------------------------------------------------------------------*/

struct VSC_C_main *
VSC_Main(struct VSM_data *vd)
{
	struct vsc *vsc = vsc_setup(vd);

	if (!VSM_StillValid(vd, &vsc->main_fantom) &&
	    !VSM_Get(vd, &vsc->main_fantom, VSC_CLASS, "", ""))
		return (NULL);
	return ((void*)vsc->main_fantom.b);
}

/*--------------------------------------------------------------------
 */

static void
vsc_add_vf(struct vsc *vsc, const struct VSM_fantom *fantom)
{
	struct vsc_vf *vf;

	ALLOC_OBJ(vf, VSC_VF_MAGIC);
	AN(vf);
	vf->fantom = *fantom;
	VTAILQ_INSERT_TAIL(&vsc->vf_list, vf, list);
}

static void
vsc_add_pt(struct vsc *vsc, const struct VSC_desc *desc,
    const volatile void *ptr, struct VSM_fantom *fantom)
{
	struct vsc_pt *pt;

	ALLOC_OBJ(pt, VSC_PT_MAGIC);
	AN(pt);
	pt->point.desc = desc;
	pt->point.ptr = ptr;
	pt->point.fantom = fantom;
	VTAILQ_INSERT_TAIL(&vsc->pt_list, pt, list);
}

#define VSC_DO(U,l,t)							\
	static void							\
	iter_##l(struct vsc *vsc, const struct VSC_desc *descs,		\
	    struct VSM_fantom *fantom)					\
	{								\
		struct VSC_C_##l *st;					\
									\
		CHECK_OBJ_NOTNULL(vsc, VSC_MAGIC);			\
		st = fantom->b;

#define VSC_F(nn,tt,ll,ff,dd,ee)					\
		vsc_add_pt(vsc, descs++, &st->nn, fantom);

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
vsc_build_pt_list(struct VSM_data *vd)
{
	struct vsc *vsc = vsc_setup(vd);
	struct VSM_fantom fantom;
	struct vsc_vf *vf;

	vsc_delete_pts(vsc);

	VSM_FOREACH(&fantom, vd) {
		if (strcmp(fantom.class, VSC_CLASS))
			continue;
		vsc_add_vf(vsc, &fantom);
	}

	VTAILQ_FOREACH(vf, &vsc->vf_list, list) {
		/*lint -save -e525 -e539 */
#define VSC_F(n,t,l,f,d,e)
#define VSC_DONE(a,b,c)
#define VSC_DO(U,l,t)						\
		if (!strcmp(vf->fantom.type, t))		\
			iter_##l(vsc, VSC_desc_##l, &vf->fantom);
#include "tbl/vsc_all.h"
#undef VSC_F
#undef VSC_DO
#undef VSC_DONE
		/*lint -restore */
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
		VTAILQ_FOREACH_SAFE(pt, &vsc->pt_list, list, pt2) {
			if (iter_test(sf->type, pt->point.fantom->type,
			    sf->flags & VSC_SF_TY_WC))
				continue;
			if (iter_test(sf->ident, pt->point.fantom->ident,
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
	vsc_delete_pts(vsc);
	VTAILQ_CONCAT(&vsc->pt_list, &pt_list, list);
}

/*--------------------------------------------------------------------
 */

int
VSC_Iter(struct VSM_data *vd, VSC_iter_f *func, void *priv)
{
	struct vsc *vsc = vsc_setup(vd);
	struct vsc_pt *pt;
	int i;

	if (1 != VSM_StillValid(vd, &vsc->iter_fantom)) {
		/* Tell app that list will be nuked */
		(void)func(priv, NULL);
		vsc_build_pt_list(vd);
		vsc_filter_pt_list(vd);
	}
	VTAILQ_FOREACH(pt, &vsc->pt_list, list) {
		i = func(priv, &pt->point);
		if (i)
			return (i);
	}
	return (0);
}

/*--------------------------------------------------------------------
 * Build the static point descriptions
 */

#define VSC_F(n,t,l,f,d,e)	{#n,#t,f,d,e},
#define VSC_DO(U,l,t) const struct VSC_desc VSC_desc_##l[] = {
#define VSC_DONE(U,l,t) };
#include "tbl/vsc_all.h"
#undef VSC_F
#undef VSC_DO
#undef VSC_DONE
