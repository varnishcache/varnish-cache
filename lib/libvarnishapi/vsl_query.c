/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2013 Varnish Software AS
 * All rights reserved.
 *
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
 *
 */

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "vas.h"
#include "miniobj.h"
#include "vre.h"
#include "vsb.h"

#include "vapi/vsl.h"
#include "vsl_api.h"
#include "vxp.h"

#define NEEDLESS_RETURN(foo) return(foo)

struct vslq_query {
	unsigned		magic;
#define VSLQ_QUERY_MAGIC	0x122322A5

	struct vex		*vex;
};

static int
vslq_test_rec(const struct vex *vex, const struct VSLC_ptr *rec)
{
	int reclen;
	const char *recdata;

	AN(vex);
	AN(rec);

	reclen = VSL_LEN(rec->ptr);
	recdata = VSL_CDATA(rec->ptr);

	switch (vex->tok) {
	case T_SEQ:		/* eq */
		assert(vex->val->type == VEX_STRING);
		if (reclen == strlen(vex->val->val_string) &&
		    !strncmp(vex->val->val_string, recdata, reclen))
			return (1);
		return (0);
	case T_SNEQ:		/* ne */
		assert(vex->val->type == VEX_STRING);
		if (reclen != strlen(vex->val->val_string) ||
		    strncmp(vex->val->val_string, recdata, reclen))
			return (1);
		return (0);
	default:
		INCOMPL();
	}

	return (0);
}

static int
vslq_test(const struct vex *vex, struct VSL_transaction * const ptrans[])
{
	struct VSL_transaction *t;
	int i;

	CHECK_OBJ_NOTNULL(vex, VEX_MAGIC);
	CHECK_OBJ_NOTNULL(vex->tag, VEX_TAG_MAGIC);
	CHECK_OBJ_NOTNULL(vex->val, VEX_VAL_MAGIC);
	AN(vex->val->val_string);

	for (t = ptrans[0]; t != NULL; t = *++ptrans) {
		AZ(VSL_ResetCursor(t->c));
		while (1) {
			i = VSL_Next(t->c);
			if (i < 0)
				return (i);
			if (i == 0)
				break;
			assert(i == 1);
			AN(t->c->rec.ptr);

			if (vex->tag->tag != VSL_TAG(t->c->rec.ptr))
				continue;

			i = vslq_test_rec(vex, &t->c->rec);
			if (i)
				return (i);


		}
	}

	return (0);
}

static int
vslq_exec(const struct vex *vex, struct VSL_transaction * const ptrans[])
{
	int r;

	CHECK_OBJ_NOTNULL(vex, VEX_MAGIC);

	switch (vex->tok) {
	case T_OR:
		AN(vex->a);
		AN(vex->b);
		r = vslq_exec(vex->a, ptrans);
		if (r != 0)
			return (r);
		return (vslq_exec(vex->b, ptrans));
	case T_AND:
		AN(vex->a);
		AN(vex->b);
		r = vslq_exec(vex->a, ptrans);
		if (r <= 0)
			return (r);
		return (vslq_exec(vex->b, ptrans));
	case T_NOT:
		AN(vex->a);
		AZ(vex->b);
		r = vslq_exec(vex->a, ptrans);
		if (r < 0)
			return (r);
		return (!r);
	default:
		return (vslq_test(vex, ptrans));
	}
	NEEDLESS_RETURN(0);
}

struct vslq_query *
vslq_newquery(struct VSL_data *vsl, enum VSL_grouping_e grouping,
    const char *querystring)
{
	struct vsb *vsb;
	struct vex *vex;
	struct vslq_query *query = NULL;

	(void)grouping;
	AN(querystring);

	vsb = VSB_new_auto();
	AN(vsb);
	vex = vex_New(querystring, vsb);
	VSB_finish(vsb);
	if (vex == NULL)
		vsl_diag(vsl, "Query expression error:\n%s", VSB_data(vsb));
	else {
		ALLOC_OBJ(query, VSLQ_QUERY_MAGIC);
		query->vex = vex;
	}
	VSB_delete(vsb);
	return (query);
}

void
vslq_deletequery(struct vslq_query **pquery)
{
	struct vslq_query *query;

	AN(pquery);
	query = *pquery;
	*pquery = NULL;
	CHECK_OBJ_NOTNULL(query, VSLQ_QUERY_MAGIC);

	AN(query->vex);
	vex_Free(&query->vex);
	AZ(query->vex);

	FREE_OBJ(query);
}

int
vslq_runquery(const struct vslq_query *query,
    struct VSL_transaction * const ptrans[])
{
	struct VSL_transaction *t;
	int r;

	CHECK_OBJ_NOTNULL(query, VSLQ_QUERY_MAGIC);

	r = vslq_exec(query->vex, ptrans);
	for (t = ptrans[0]; t != NULL; t = *++ptrans)
		AZ(VSL_ResetCursor(t->c));
	return (r);
}
