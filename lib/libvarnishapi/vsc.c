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
#include <string.h>
#include <stdlib.h>

#include "vas.h"
#include "vav.h"
#include "vsm.h"
#include "vsc.h"
#include "vqueue.h"
#include "miniobj.h"
#include "varnishapi.h"

#include "vsm_api.h"

struct vsc_sf {
	unsigned		magic;
#define VSL_SF_MAGIC		0x558478dd
	VTAILQ_ENTRY(vsc_sf)	next;
	int			flags;
#define VSL_SF_EXCL		(1 << 0)
#define VSL_SF_CL_WC		(1 << 1)
#define VSL_SF_ID_WC		(1 << 2)
#define VSL_SF_NM_WC		(1 << 3)
	char			*class;
	char			*ident;
	char			*name;
};

struct vsc {
	unsigned		magic;
#define VSC_MAGIC		0x3373554a

	int			sf_init;
	VTAILQ_HEAD(, vsc_sf)	sf_list;

};


/*--------------------------------------------------------------------*/

void
VSC_Setup(struct VSM_data *vd)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	AZ(vd->vsc);
	AZ(vd->vsl);
	ALLOC_OBJ(vd->vsc, VSC_MAGIC);
	AN(vd->vsc);
	VTAILQ_INIT(&vd->vsc->sf_list);
}

/*--------------------------------------------------------------------*/

void
VSC_Delete(struct VSM_data *vd)
{
	struct vsc_sf *sf;
	struct vsc *vsc;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	vsc = vd->vsc;
	vd->vsc = NULL;
	CHECK_OBJ_NOTNULL(vsc, VSC_MAGIC);
	while(!VTAILQ_EMPTY(&vsc->sf_list)) {
		sf = VTAILQ_FIRST(&vsc->sf_list);
		VTAILQ_REMOVE(&vsc->sf_list, sf, next);
		free(sf->class);
		free(sf->ident);
		free(sf->name);
		FREE_OBJ(sf);
	}
}

/*--------------------------------------------------------------------*/

static int
vsc_sf_arg(const struct VSM_data *vd, const char *opt)
{
	struct vsc *vsc;
	struct vsc_sf *sf;
	char **av, *q, *p;
	int i;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	vsc = vd->vsc;
	CHECK_OBJ_NOTNULL(vsc, VSC_MAGIC);

	if (VTAILQ_EMPTY(&vsc->sf_list)) {
		if (*opt == '^')
			vsc->sf_init = 1;
	}

	av = VAV_Parse(opt, NULL, ARGV_COMMA);
	AN(av);
	if (av[0] != NULL) {
		vd->diag(vd->priv, "Parse error: %s", av[0]);
		return (-1);
	}
	for (i = 1; av[i] != NULL; i++) {
		ALLOC_OBJ(sf, VSL_SF_MAGIC);
		AN(sf);
		VTAILQ_INSERT_TAIL(&vsc->sf_list, sf, next);

		p = av[i];
		if (*p == '^') {
			sf->flags |= VSL_SF_EXCL;
			p++;
		}

		q = strchr(p, '.');
		if (q != NULL) {
			*q++ = '\0';
			if (*p != '\0')
				REPLACE(sf->class, p);
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
		if (sf->class != NULL) {
			q = strchr(sf->class, '*');
			if (q != NULL && q[1] == '\0') {
				*q = '\0';
				sf->flags |= VSL_SF_CL_WC;
			}
		}
		if (sf->ident != NULL) {
			q = strchr(sf->ident, '*');
			if (q != NULL && q[1] == '\0') {
				*q = '\0';
				sf->flags |= VSL_SF_ID_WC;
			}
		}
		if (sf->name != NULL) {
			q = strchr(sf->name, '*');
			if (q != NULL && q[1] == '\0') {
				*q = '\0';
				sf->flags |= VSL_SF_NM_WC;
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

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	AN(vd->vsc);
	switch (arg) {
	case 'f': return (vsc_sf_arg(vd, opt));
	case 'n': return (VSM_n_Arg(vd, opt));
	default:
		return (0);
	}
}

/*--------------------------------------------------------------------*/

int
VSC_Open(struct VSM_data *vd, int diag)
{
	int i;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	AN(vd->vsc);

	i = VSM_Open(vd, diag);
	return (i);
}

/*--------------------------------------------------------------------*/

struct VSC_C_main *
VSC_Main(struct VSM_data *vd)
{
	struct VSM_chunk *sha;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	CHECK_OBJ_NOTNULL(vd->vsc, VSC_MAGIC);

	sha = VSM_find_alloc(vd, VSC_CLASS, "", "");
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
iter_call(const struct vsc *vsc, VSC_iter_f *func, void *priv,
    const struct VSC_point *const sp)
{
	struct vsc_sf *sf;
	int good;

	CHECK_OBJ_NOTNULL(vsc, VSC_MAGIC);

	if (VTAILQ_EMPTY(&vsc->sf_list))
		return (func(priv, sp));

	good = vsc->sf_init;

	VTAILQ_FOREACH(sf, &vsc->sf_list, next) {
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

#define VSC_DO(U,l,t)							\
	static int							\
	iter_##l(const struct vsc *vsc, struct VSM_chunk *sha,		\
	    VSC_iter_f *func, void *priv)				\
	{								\
		struct VSC_C_##l *st;					\
		struct VSC_point sp;					\
		int i;							\
									\
		CHECK_OBJ_NOTNULL(vsc, VSC_MAGIC);			\
		CHECK_OBJ_NOTNULL(sha, VSM_CHUNK_MAGIC);		\
		st = VSM_PTR(sha);					\
		sp.class = t;						\
		sp.ident = sha->ident;

#define VSC_F(nn,tt,ll,ff,dd,ee)					\
		sp.name = #nn;						\
		sp.fmt = #tt;						\
		sp.flag = ff;						\
		sp.desc = dd;						\
		sp.ptr = &st->nn;					\
		i = iter_call(vsc, func, priv, &sp);			\
		if (i)							\
			return(i);

#define VSC_DONE(U,l,t)							\
		return (0);						\
	}

#include "vsc_all.h"
#undef VSC_DO
#undef VSC_F
#undef VSC_DONE

int
VSC_Iter(struct VSM_data *vd, VSC_iter_f *func, void *priv)
{
	struct vsc *vsc;
	struct VSM_chunk *sha;
	int i;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	vsc = vd->vsc;
	CHECK_OBJ_NOTNULL(vsc, VSC_MAGIC);

	i = 0;
	VSM_FOREACH(sha, vd) {
		CHECK_OBJ_NOTNULL(sha, VSM_CHUNK_MAGIC);
		if (strcmp(sha->class, VSC_CLASS))
			continue;
		/*lint -save -e525 -e539 */
#define VSC_F(n,t,l,f,d,e)
#define VSC_DONE(a,b,c)
#define VSC_DO(U,l,t)						\
		if (!strcmp(sha->type, t)) {			\
			i = iter_##l(vsc, sha, func, priv);	\
			if (!i)					\
				continue;			\
		}
#include "vsc_all.h"
#undef VSC_F
#undef VSC_DO
#undef VSC_DONE
		/*lint -restore */
		break;
	}
	return (i);
}
