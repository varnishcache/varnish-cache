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

#include "vas.h"
#include "miniobj.h"
#include "vre.h"

#include "vapi/vsl.h"
#include "vsl_api.h"

struct vslq_query {
	unsigned		magic;
#define VSLQ_QUERY_MAGIC	0x122322A5

	vre_t			*regex;
};

struct vslq_query *
vslq_newquery(struct VSL_data *vsl, enum VSL_grouping_e grouping,
    const char *querystring)
{
	struct vslq_query *query;
	const char *error;
	int pos;
	vre_t *regex;

	(void)grouping;
	AN(querystring);
	regex = VRE_compile(querystring, 0, &error, &pos);
	if (regex == NULL) {
		vsl_diag(vsl, "failed to compile regex at pos %d: %s",
		    pos, error);
		return (NULL);
	}

	ALLOC_OBJ(query, VSLQ_QUERY_MAGIC);
	if (query != NULL)
		query->regex = regex;
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

	AN(query->regex);
	VRE_free(&query->regex);
	AZ(query->regex);

	FREE_OBJ(query);
}

int
vslq_runquery(const struct vslq_query *query, struct VSL_transaction * const ptrans[])
{
	struct VSL_transaction *t;
	struct VSL_cursor *c;
	int i, len;
	const char *data;

	CHECK_OBJ_NOTNULL(query, VSLQ_QUERY_MAGIC);
	AN(query->regex);

	t = ptrans[0];
	while (t) {
		c = t->c;
		while (1) {
			i = VSL_Next(c);
			if (i == 0)
				break;
			assert(i == 1);
			AN(c->rec.ptr);
			len = VSL_LEN(c->rec.ptr);
			data = VSL_CDATA(c->rec.ptr);
			i = VRE_exec(query->regex, data, len, 0, 0, NULL, 0,
			    NULL);
			if (i != VRE_ERROR_NOMATCH) {
				AZ(VSL_ResetCursor(c));
				return (1);
			}
		}
		AZ(VSL_ResetCursor(c));
		t = *++ptrans;
	}

	return (0);
}
