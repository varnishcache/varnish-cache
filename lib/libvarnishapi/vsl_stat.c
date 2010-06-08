/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Redpill Linpro AS
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

#include "svnid.h"
SVNID("$Id$")

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include "vas.h"
#include "vsm.h"
#include "vsc.h"
#include "vre.h"
#include "vqueue.h"
#include "miniobj.h"
#include "varnishapi.h"

#include "vslapi.h"

/*--------------------------------------------------------------------*/

struct vsc_main *
VSM_OpenStats(struct VSM_data *vd)
{
	struct vsm_chunk *sha;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);

	sha = vsm_find_alloc(vd, VSC_CLASS, "", "");
	assert(sha != NULL);
	return (VSM_PTR(sha));
}

/*--------------------------------------------------------------------
 * -1 -> unknown stats encountered.
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

static int
iter_call(const struct VSM_data *vd, vsl_stat_f *func, void *priv,
    const struct vsl_statpt *const sp)
{
	struct vsl_sf *sf;
	int good = vd->sf_init;

	if (VTAILQ_EMPTY(&vd->sf_list))
		return (func(priv, sp));

	VTAILQ_FOREACH(sf, &vd->sf_list, next) {
		if (iter_test(sf->class, sp->class, sf->flags & VSL_SF_CL_WC))
			continue;
		if (iter_test(sf->ident, sp->ident, sf->flags & VSL_SF_ID_WC))
			continue;
		if (iter_test(sf->name, sp->name, sf->flags & VSL_SF_NM_WC))
			continue;
		if (sf->flags & VSL_SF_EXCL)
			good = 0;
		else
			good = 1;
	}
	if (!good)
		return (0);
	return (func(priv, sp));
}

static int
iter_main(const struct VSM_data *vd, struct vsm_chunk *sha, vsl_stat_f *func,
    void *priv)
{
	struct vsc_main *st = VSM_PTR(sha);
	struct vsl_statpt sp;
	int i;

	sp.class = "";
	sp.ident = "";
#define VSC_F_MAIN(nn, tt, ll, ff, dd)					\
	sp.name = #nn;							\
	sp.fmt = #tt;							\
	sp.flag = ff;							\
	sp.desc = dd;							\
	sp.ptr = &st->nn;						\
	i = iter_call(vd, func, priv, &sp);				\
	if (i)								\
		return(i);
#include "vsc_fields.h"
#undef VSC_F_MAIN
	return (0);
}

static int
iter_sma(const struct VSM_data *vd, struct vsm_chunk *sha, vsl_stat_f *func,
    void *priv)
{
	struct vsc_sma *st = VSM_PTR(sha);
	struct vsl_statpt sp;
	int i;

	sp.class = VSC_TYPE_SMA;
	sp.ident = sha->ident;
#define VSC_F_SMA(nn, tt, ll, ff, dd)				\
	sp.name = #nn;							\
	sp.fmt = #tt;							\
	sp.flag = ff;							\
	sp.desc = dd;							\
	sp.ptr = &st->nn;						\
	i = iter_call(vd, func, priv, &sp);				\
	if (i)								\
		return(i);
#include "vsc_fields.h"
#undef VSC_F_SMA
	return (0);
}

int
VSL_IterStat(const struct VSM_data *vd, vsl_stat_f *func, void *priv)
{
	struct vsm_chunk *sha;
	int i;

	i = 0;
	VSM_FOREACH(sha, vd) {
		CHECK_OBJ_NOTNULL(sha, VSM_CHUNK_MAGIC);
		if (strcmp(sha->class, VSC_CLASS))
			continue;
		if (!strcmp(sha->type, VSC_TYPE_MAIN))
			i = iter_main(vd, sha, func, priv);
		else if (!strcmp(sha->type, VSC_TYPE_SMA))
			i = iter_sma(vd, sha, func, priv);
		else
			i = -1;
		if (i != 0)
			break;
	}
	return (i);
}
