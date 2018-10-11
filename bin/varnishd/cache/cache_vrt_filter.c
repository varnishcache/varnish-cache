/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2016 Varnish Software AS
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
 */

#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "vct.h"
#include "cache_varnishd.h"
#include "cache_vcl.h"

#include "cache_filter.h"

/*--------------------------------------------------------------------
 */

struct vfp_filter {
	unsigned			magic;
#define VFP_FILTER_MAGIC		0xd40894e9
	const struct vfp		*filter;
	int				nlen;
	VTAILQ_ENTRY(vfp_filter)	list;
};

static struct vfp_filter_head vfp_filters =
    VTAILQ_HEAD_INITIALIZER(vfp_filters);

void
VRT_AddVFP(VRT_CTX, const struct vfp *filter)
{
	struct vfp_filter *vp;
	struct vfp_filter_head *hd = &vfp_filters;

	VTAILQ_FOREACH(vp, hd, list) {
		xxxassert(vp->filter != filter);
		xxxassert(strcasecmp(vp->filter->name, filter->name));
	}
	if (ctx != NULL) {
		hd = &ctx->vcl->vfps;
		VTAILQ_FOREACH(vp, hd, list) {
			xxxassert(vp->filter != filter);
			xxxassert(strcasecmp(vp->filter->name, filter->name));
		}
	}
	ALLOC_OBJ(vp, VFP_FILTER_MAGIC);
	AN(vp);
	vp->filter = filter;
	vp->nlen = strlen(filter->name);
	VTAILQ_INSERT_TAIL(hd, vp, list);
}

void
VRT_RemoveVFP(VRT_CTX, const struct vfp *filter)
{
	struct vfp_filter *vp;
	struct vfp_filter_head *hd = &ctx->vcl->vfps;

	VTAILQ_FOREACH(vp, hd, list) {
		if (vp->filter == filter)
			break;
	}
	XXXAN(vp);
	VTAILQ_REMOVE(hd, vp, list);
	FREE_OBJ(vp);
}

int
VCL_StackVFP(struct vfp_ctx *vc, const struct vcl *vcl, const char *fl)
{
	const char *p, *q;
	const struct vfp_filter *vp;

	VSLb(vc->wrk->vsl, SLT_Filters, "%s", fl);

	for (p = fl; *p; p = q) {
		if (vct_isspace(*p)) {
			q = p + 1;
			continue;
		}
		for (q = p; *q; q++)
			if (vct_isspace(*q))
				break;
		VTAILQ_FOREACH(vp, &vfp_filters, list) {
			if (vp->nlen != q - p)
				continue;
			if (!memcmp(p, vp->filter->name, vp->nlen))
				break;
		}
		if (vp == NULL) {
			VTAILQ_FOREACH(vp, &vcl->vfps, list) {
				if (vp->nlen != q - p)
					continue;
				if (!memcmp(p, vp->filter->name, vp->nlen))
					break;
			}
		}
		if (vp == NULL)
			return (VFP_Error(vc,
			    "Filter '%.*s' not found", (int)(q-p), p));
		if (VFP_Push(vc, vp->filter) == NULL)
			return (-1);
	}
	return (0);
}

void
VCL_VRT_Init(void)
{
	VRT_AddVFP(NULL, &VFP_testgunzip);
	VRT_AddVFP(NULL, &VFP_gunzip);
	VRT_AddVFP(NULL, &VFP_gzip);
	VRT_AddVFP(NULL, &VFP_esi);
	VRT_AddVFP(NULL, &VFP_esi_gzip);
}
