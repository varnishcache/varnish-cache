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
#include "shmlog.h"
#include "vre.h"
#include "miniobj.h"
#include "varnishapi.h"

#include "vsl.h"

/*--------------------------------------------------------------------*/

struct varnish_stats *
VSL_OpenStats(struct VSL_data *vd)
{
	struct shmalloc *sha;

	CHECK_OBJ_NOTNULL(vd, VSL_MAGIC);
	if (VSL_Open(vd))
		return (NULL);
	sha = vsl_find_alloc(vd, VSL_CLASS_STAT, "", "");
	assert(sha != NULL);
	return (SHA_PTR(sha));
}

/*--------------------------------------------------------------------
 * -1 -> unknown stats encountered.
 */

static int
iter_main(struct shmalloc *sha, vsl_stat_f *func, void *priv)
{
	struct varnish_stats *st = SHA_PTR(sha);
	struct vsl_statpt sp;
	int i;

	sp.type = "";
	sp.ident = "";
#define MAC_STAT(nn, tt, ll, ff, dd)					\
	sp.nm = #nn;							\
	sp.fmt = #tt;							\
	sp.flag = ff;							\
	sp.desc = dd;							\
	sp.ptr = &st->nn;						\
	i = func(priv, &sp);						\
	if (i)								\
		return(i);
#include "stat_field.h"
#undef MAC_STAT
	return (0);
}

static int
iter_sma(struct shmalloc *sha, vsl_stat_f *func, void *priv)
{
	struct varnish_stats_sma *st = SHA_PTR(sha);
	struct vsl_statpt sp;
	int i;

	sp.type = VSL_TYPE_STAT_SMA;
	sp.ident = sha->ident;
#define MAC_STAT_SMA(nn, tt, ll, ff, dd)				\
	sp.nm = #nn;							\
	sp.fmt = #tt;							\
	sp.flag = ff;							\
	sp.desc = dd;							\
	sp.ptr = &st->nn;						\
	i = func(priv, &sp);						\
	if (i)								\
		return(i);
#include "stat_field.h"
#undef MAC_STAT_SMA
	return (0);
}

int
VSL_IterStat(const struct VSL_data *vd, vsl_stat_f *func, void *priv)
{
	struct shmalloc *sha;
	int i;

	i = 0;
	VSL_FOREACH(sha, vd) {
		CHECK_OBJ_NOTNULL(sha, SHMALLOC_MAGIC);
		if (strcmp(sha->class, VSL_CLASS_STAT))
			continue;
		if (!strcmp(sha->type, VSL_TYPE_STAT))
			i = iter_main(sha, func, priv);
		else if (!strcmp(sha->type, VSL_TYPE_STAT_SMA))
			i = iter_sma(sha, func, priv);
		else
			i = -1;
		if (i != 0)
			break;
	}
	return (i);
}
