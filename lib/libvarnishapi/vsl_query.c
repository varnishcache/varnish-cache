/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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
#include <ctype.h>

#include "vas.h"
#include "miniobj.h"
#include "vre.h"
#include "vsb.h"
#include "vbm.h"

#include "vapi/vsl.h"
#include "vsl_api.h"
#include "vxp.h"

#define NEEDLESS_RETURN(foo) return(foo)

struct vslq_query {
	unsigned		magic;
#define VSLQ_QUERY_MAGIC	0x122322A5

	struct vex		*vex;
};

#define VSLQ_TEST_NUMOP(TYPE, PRE_LHS, OP, PRE_RHS)	\
	switch (TYPE) {					\
	case VEX_INT:					\
		if (PRE_LHS##_int OP PRE_RHS##_int)	\
			return (1);			\
		return (0);				\
	case VEX_FLOAT:					\
		if (PRE_LHS##_float OP PRE_RHS##_float)	\
			return (1);			\
		return (0);				\
	default:					\
		WRONG("Wrong RHS type");		\
	}

static int
vslq_test_rec(const struct vex *vex, const struct VSLC_ptr *rec)
{
	const struct vex_rhs *rhs;
	long long lhs_int = 0;
	double lhs_float = 0.;
	const char *b, *e;
	char *p;
	int i;

	AN(vex);
	AN(rec);

	b = VSL_CDATA(rec->ptr);
	e = b + VSL_LEN(rec->ptr) - 1;

	/* Prefix */
	if (vex->lhs->prefix != NULL) {
		if (strncasecmp(b, vex->lhs->prefix, vex->lhs->prefixlen))
			return (0);
		if (b[vex->lhs->prefixlen] != ':')
			return (0);
		b += vex->lhs->prefixlen + 1;
		/* Skip ws */
		while (*b && isspace(*b))
			b++;
	}

	/* Field */
	if (vex->lhs->field > 0) {
		for (e = b, i = 0; *e && i < vex->lhs->field; i++) {
			b = e;
			/* Skip ws */
			while (*b && isspace(*b))
				b++;
			e = b;
			/* Skip non-ws */
			while (*e && !isspace(*e))
				e++;
		}
		assert(b <= e);
		if (*b == '\0' || i < vex->lhs->field)
			/* Missing field - no match */
			return (0);
	}

	if (vex->tok == T_TRUE)
		/* Always true */
		return (1);

	rhs = vex->rhs;
	CHECK_OBJ_NOTNULL(rhs, VEX_RHS_MAGIC);

	/* Prepare */
	switch (vex->tok) {
	case T_EQ:		/* == */
	case T_NEQ:		/* != */
	case '<':
	case '>':
	case T_LEQ:		/* <= */
	case T_GEQ:		/* >= */
		/* Numerical comparison */
		if (*b == '\0')
			/* Empty string doesn't match */
			return (0);
		switch (rhs->type) {
		case VEX_INT:
			lhs_int = strtoll(b, &p, 0);
			if (*p == '\0' || isspace(*p))
				break;
			/* Can't parse - no match */
			return (0);
		case VEX_FLOAT:
			lhs_float = strtod(b, &p);
			if (*p == '\0' || isspace(*p))
				break;
			/* Can't parse - no match */
			return (0);
		default:
			WRONG("Wrong RHS type");
		}
		break;
	default:
		break;
	}

	/* Compare */
	switch (vex->tok) {
	case T_EQ:		/* == */
		VSLQ_TEST_NUMOP(rhs->type, lhs, ==, rhs->val);
	case T_NEQ:		/* != */
		VSLQ_TEST_NUMOP(rhs->type, lhs, !=, rhs->val);
	case '<':		/* < */
		VSLQ_TEST_NUMOP(rhs->type, lhs, <, rhs->val);
	case '>':
		VSLQ_TEST_NUMOP(rhs->type, lhs, >, rhs->val);
	case T_LEQ:		/* <= */
		VSLQ_TEST_NUMOP(rhs->type, lhs, <=, rhs->val);
	case T_GEQ:		/* >= */
		VSLQ_TEST_NUMOP(rhs->type, lhs, >=, rhs->val);
	case T_SEQ:		/* eq */
		assert(rhs->type == VEX_STRING);
		if (e - b != rhs->val_stringlen)
			return (0);
		if (vex->options & VEX_OPT_CASELESS) {
			if (strncasecmp(b, rhs->val_string, e - b))
				return (0);
		} else {
			if (strncmp(b, rhs->val_string, e - b))
				return (0);
		}
		return (1);
	case T_SNEQ:		/* ne */
		assert(rhs->type == VEX_STRING);
		if (e - b != rhs->val_stringlen)
			return (1);
		if (vex->options & VEX_OPT_CASELESS) {
			if (strncasecmp(b, rhs->val_string, e - b))
				return (1);
		} else {
			if (strncmp(b, rhs->val_string, e - b))
				return (1);
		}
		return (0);
	case '~':		/* ~ */
		assert(rhs->type == VEX_REGEX && rhs->val_regex != NULL);
		i = VRE_exec(rhs->val_regex, b, e - b, 0, 0, NULL, 0, NULL);
		if (i != VRE_ERROR_NOMATCH)
			return (1);
		return (0);
	case T_NOMATCH:		/* !~ */
		assert(rhs->type == VEX_REGEX && rhs->val_regex != NULL);
		i = VRE_exec(rhs->val_regex, b, e - b, 0, 0, NULL, 0, NULL);
		if (i == VRE_ERROR_NOMATCH)
			return (1);
		return (0);
	default:
		WRONG("Bad expression token");
	}

	return (0);
}

static int
vslq_test(const struct vex *vex, struct VSL_transaction * const ptrans[])
{
	struct VSL_transaction *t;
	int i;

	CHECK_OBJ_NOTNULL(vex, VEX_MAGIC);
	CHECK_OBJ_NOTNULL(vex->lhs, VEX_LHS_MAGIC);
	AN(vex->lhs->tags);

	for (t = ptrans[0]; t != NULL; t = *++ptrans) {
		if (vex->lhs->level >= 0) {
			if (vex->lhs->level_pm < 0) {
				/* OK if less than or equal */
				if (t->level > vex->lhs->level)
					continue;
			} else if (vex->lhs->level_pm > 0) {
				/* OK if greater than or equal */
				if (t->level < vex->lhs->level)
					continue;
			} else {
				/* OK if equal */
				if (t->level != vex->lhs->level)
					continue;
			}
		}

		AZ(VSL_ResetCursor(t->c));
		while (1) {
			i = VSL_Next(t->c);
			if (i < 0)
				return (i);
			if (i == 0)
				break;
			assert(i == 1);
			AN(t->c->rec.ptr);

			if (!vbit_test(vex->lhs->tags, VSL_TAG(t->c->rec.ptr)))
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
	vex = vex_New(querystring, vsb, vsl->C_opt ? VEX_OPT_CASELESS : 0);
	AZ(VSB_finish(vsb));
	if (vex == NULL)
		vsl_diag(vsl, "%s", VSB_data(vsb));
	else {
		ALLOC_OBJ(query, VSLQ_QUERY_MAGIC);
		XXXAN(query);
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
