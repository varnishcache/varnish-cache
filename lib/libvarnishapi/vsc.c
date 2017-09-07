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
#include <unistd.h>

#include "vdef.h"
#include "vas.h"
#include "miniobj.h"
#include "vqueue.h"
#include "vjsn.h"
#include "vsb.h"
#include "vend.h"
#include "vmb.h"

#include "vapi/vsc.h"
#include "vapi/vsm.h"

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

	struct vsc_sf_head	sf_list_include;
	struct vsc_sf_head	sf_list_exclude;
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

struct VSC_point *
VSC_Clone_Point(const struct VSC_point * const vp)
{
	struct VSC_point *pt;
	char *p;

	pt = calloc(sizeof *pt, 1);
	AN(pt);
	*pt = *vp;
	p = strdup(pt->name); AN(p); pt->name = p;
	p = strdup(pt->sdesc); AN(p); pt->sdesc = p;
	p = strdup(pt->ldesc); AN(p); pt->ldesc = p;
	return (pt);
}

void
VSC_Destroy_Point(struct VSC_point **p)
{
	AN(p);
	free(TRUST_ME((*p)->ldesc));
	free(TRUST_ME((*p)->sdesc));
	free(TRUST_ME((*p)->name));
	free(*p);
	*p = NULL;
}

/*--------------------------------------------------------------------*/

struct vsc *
VSC_New(void)
{
	struct vsc *vsc;

	ALLOC_OBJ(vsc, VSC_MAGIC);
	if (vsc == NULL)
		return (vsc);
	VTAILQ_INIT(&vsc->sf_list_include);
	VTAILQ_INIT(&vsc->sf_list_exclude);
	return (vsc);
}

/*--------------------------------------------------------------------*/

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
VSC_Destroy(struct vsc **vscp)
{
	struct vsc *vsc;

	TAKE_OBJ_NOTNULL(vsc, vscp, VSC_MAGIC);
	vsc_delete_sf_list(&vsc->sf_list_include);
	vsc_delete_sf_list(&vsc->sf_list_exclude);
	FREE_OBJ(vsc);
}

/*--------------------------------------------------------------------*/

static int
vsc_f_arg(struct vsc *vsc, const char *opt)
{
	struct vsc_sf *sf;
	unsigned exclude = 0;

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
VSC_Arg(struct vsc *vsc, char arg, const char *opt)
{

	CHECK_OBJ_NOTNULL(vsc, VSC_MAGIC);

	switch (arg) {
	case 'f': return (vsc_f_arg(vsc, opt));
	default: return (0);
	}
}

/*--------------------------------------------------------------------
 */

static int
vsc_filter(const struct vsc *vsc, const char *nm)
{
	struct vsc_sf *sf;

	CHECK_OBJ_NOTNULL(vsc, VSC_MAGIC);
	VTAILQ_FOREACH(sf, &vsc->sf_list_exclude, list)
		if (!fnmatch(sf->pattern, nm, 0))
			return (1);
	if (VTAILQ_EMPTY(&vsc->sf_list_include))
		return (0);
	VTAILQ_FOREACH(sf, &vsc->sf_list_include, list)
		if (!fnmatch(sf->pattern, nm, 0))
			return (0);
	return (1);
}

/*--------------------------------------------------------------------
 */

static int
vsc_iter_elem(const struct vsc *vsc, const struct vsm_fantom *fantom,
    const struct vjsn_val *vv, struct vsb *vsb, VSC_iter_f *func, void *priv)
{
	struct VSC_point	point;
	struct vjsn_val *vt;

	CHECK_OBJ_NOTNULL(vsc, VSC_MAGIC);
	memset(&point, 0, sizeof point);

	vt = vjsn_child(vv, "name");
	AN(vt);
	assert(vt->type == VJSN_STRING);

	VSB_clear(vsb);
	VSB_printf(vsb, "%s.%s", fantom->ident, vt->value);
	AZ(VSB_finish(vsb));

	if (vsc_filter(vsc, VSB_data(vsb)))
		return (0);

	point.name = VSB_data(vsb);

#define DOF(n, k)				\
	vt = vjsn_child(vv, k);			\
	AN(vt);					\
	assert(vt->type == VJSN_STRING);	\
	point.n = vt->value;

	DOF(ctype, "ctype");
	DOF(sdesc, "oneliner");
	DOF(ldesc, "docs");
#undef DOF
	vt = vjsn_child(vv, "type");
	AN(vt);
	assert(vt->type == VJSN_STRING);

	if (!strcmp(vt->value, "counter")) {
		point.semantics = 'c';
	} else if (!strcmp(vt->value, "gauge")) {
		point.semantics = 'g';
	} else if (!strcmp(vt->value, "bitmap")) {
		point.semantics = 'b';
	} else {
		point.semantics = '?';
	}

	vt = vjsn_child(vv, "format");
	AN(vt);
	assert(vt->type == VJSN_STRING);

	if (!strcmp(vt->value, "integer")) {
		point.format = 'i';
	} else if (!strcmp(vt->value, "bytes")) {
		point.format = 'B';
	} else if (!strcmp(vt->value, "bitmap")) {
		point.format = 'b';
	} else if (!strcmp(vt->value, "duration")) {
		point.format = 'd';
	} else {
		point.format = '?';
	}

	vt = vjsn_child(vv, "level");
	AN(vt);
	assert(vt->type == VJSN_STRING);

	if (!strcmp(vt->value, "info"))  {
		point.level = &level_info;
	} else if (!strcmp(vt->value, "diag")) {
		point.level = &level_diag;
	} else if (!strcmp(vt->value, "debug")) {
		point.level = &level_debug;
	} else {
		WRONG("Illegal level");
	}

	vt = vjsn_child(vv, "index");
	AN(vt);

	point.ptr = (volatile void*)
	    ((volatile char*)fantom->b + atoi(vt->value));

	return (func(priv, &point));
}

static int
vsc_iter_fantom(const struct vsc *vsc, const struct vsm_fantom *fantom,
    struct vsb *vsb, VSC_iter_f *func, void *priv)
{
	int i = 0;
	const char *p;
	const char *e;
	struct vjsn *vj;
	struct vjsn_val *vv, *vve;

	CHECK_OBJ_NOTNULL(vsc, VSC_MAGIC);

	p = (char*)fantom->b + 8 + vbe64dec(fantom->b);
	assert (p < (char*)fantom->e);
	vj = vjsn_parse(p, &e);
	XXXAZ(e);
	AN(vj);
	vve = vjsn_child(vj->value, "elem");
	AN(vve);
	VTAILQ_FOREACH(vv, &vve->children, list) {
		i = vsc_iter_elem(vsc, fantom, vv, vsb, func, priv);
		if (i)
			break;
	}
	vjsn_delete(&vj);
	return (i);
}

/*--------------------------------------------------------------------
 */

int __match_proto__()	// We don't want vsc to be const
VSC_Iter(struct vsc *vsc, struct vsm *vsm, struct vsm_fantom *f,
    VSC_iter_f *func, void *priv)
{
	struct vsm_fantom	ifantom;
	uint64_t u;
	int i = 0;
	struct vsb *vsb;

	CHECK_OBJ_NOTNULL(vsc, VSC_MAGIC);
	AN(vsm);
	vsb = VSB_new_auto();
	AN(vsb);
	VSM_FOREACH(&ifantom, vsm) {
		if (strcmp(ifantom.class, VSC_CLASS))
			continue;
		AZ(VSM_Map(vsm, &ifantom));
		u = vbe64dec(ifantom.b);
		if (u == 0) {
			VRMB();
			usleep(100000);
			u = vbe64dec(ifantom.b);
		}
		assert(u > 0);
		i = vsc_iter_fantom(vsc, &ifantom, vsb, func, priv);
		if (f != NULL) {
			*f = ifantom;
		} else {
			AZ(VSM_Unmap(vsm, &ifantom));
		}
		if (i)
			break;
	}
	VSB_destroy(&vsb);
	return (i);
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

