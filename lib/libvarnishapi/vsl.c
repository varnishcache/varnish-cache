/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2013 Varnish Software AS
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
#include <sys/types.h>

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "miniobj.h"
#include "vas.h"
#include "vdef.h"

#include "vapi/vsm.h"
#include "vapi/vsl.h"
#include "vapi/vsm_int.h"
#include "vin.h"
#include "vbm.h"
#include "vmb.h"
#include "vre.h"
#include "vsb.h"
#include "vsl_api.h"
#include "vsm_api.h"

/*--------------------------------------------------------------------*/

const char *VSL_tags[256] = {
#  define SLTM(foo,sdesc,ldesc)       [SLT_##foo] = #foo,
#  include "tbl/vsl_tags.h"
#  undef SLTM
};

int
vsl_diag(struct VSL_data *vsl, const char *fmt, ...)
{
	va_list ap;

	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);
	AN(fmt);

	if (vsl->diag == NULL)
		vsl->diag = VSB_new_auto();
	AN(vsl->diag);
	VSB_clear(vsl->diag);
	va_start(ap, fmt);
	VSB_vprintf(vsl->diag, fmt, ap);
	va_end(ap);
	AZ(VSB_finish(vsl->diag));
	return (-1);
}

struct VSL_data *
VSL_New(void)
{
	struct VSL_data *vsl;

	ALLOC_OBJ(vsl, VSL_MAGIC);
	if (vsl == NULL)
		return (NULL);

	vsl->vbm_select = vbit_init(256);
	vsl->vbm_supress = vbit_init(256);

	return (vsl);
}

void
VSL_Delete(struct VSL_data *vsl)
{

	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);

	vbit_destroy(vsl->vbm_select);
	vbit_destroy(vsl->vbm_supress);
	VSL_ResetError(vsl);
	FREE_OBJ(vsl);
}

const char *
VSL_Error(const struct VSL_data *vsl)
{

	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);

	if (vsl->diag == NULL)
		return (NULL);
	else
		return (VSB_data(vsl->diag));
}

void
VSL_ResetError(struct VSL_data *vsl)
{

	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);

	if (vsl->diag == NULL)
		return;
	VSB_delete(vsl->diag);
	vsl->diag = NULL;
}

int
VSL_Match(struct VSL_data *vsl, const struct VSL_cursor *c)
{
	enum VSL_tag_e tag;

	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);
	if (c == NULL || c->rec.ptr == NULL)
		return (0);
	tag = VSL_TAG(c->rec.ptr);
	if (tag <= SLT__Bogus || tag >= SLT__Reserved)
		return (0);
	if (vbit_test(vsl->vbm_select, tag))
		return (1);
	else if (vbit_test(vsl->vbm_supress, tag))
		return (0);

	/* Default show */
	return (1);
}

const char *VSL_transactions[256] = {
	/*                 12345678901234 */
	[VSL_t_unknown] = "<< Unknown  >>",
	[VSL_t_sess]	= "<< Session  >>",
	[VSL_t_req]	= "<< Request  >>",
	[VSL_t_esireq]	= "<< ESI-req  >>",
	[VSL_t_bereq]	= "<< BeReq    >>",
	[VSL_t_raw]	= "<< Record   >>",
};

#define VSL_PRINT(...)					\
	do {						\
		if (0 > fprintf(__VA_ARGS__))		\
			return (-5);			\
	} while (0)

int
VSL_Print(struct VSL_data *vsl, const struct VSL_cursor *c, void *fo)
{
	enum VSL_tag_e tag;
	uint32_t vxid;
	unsigned len;
	const char *data;
	int type;

	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);
	if (c == NULL || c->rec.ptr == NULL)
		return (0);
	if (fo == NULL)
		fo = stdout;
	tag = VSL_TAG(c->rec.ptr);
	vxid = VSL_ID(c->rec.ptr);
	len = VSL_LEN(c->rec.ptr);
	type = VSL_CLIENT(c->rec.ptr) ? 'c' : VSL_BACKEND(c->rec.ptr) ?
	    'b' : '-';
	data = VSL_CDATA(c->rec.ptr);

	if (tag == SLT_Debug) {
		VSL_PRINT(fo, "%10u %-14s %c \"", vxid, VSL_tags[tag], type);
		while (len-- > 0) {
			if (*data >= ' ' && *data <= '~')
				VSL_PRINT(fo, "%c", *data);
			else
				VSL_PRINT(fo, "%%%02x", (unsigned char)*data);
			data++;
		}
		VSL_PRINT(fo, "\"\n");
	} else
		VSL_PRINT(fo, "%10u %-14s %c %.*s\n", vxid, VSL_tags[tag],
		    type, (int)len, data);

	return (0);
}

int
VSL_PrintTerse(struct VSL_data *vsl, const struct VSL_cursor *c, void *fo)
{
	enum VSL_tag_e tag;
	unsigned len;
	const char *data;

	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);
	if (c == NULL || c->rec.ptr == NULL)
		return (0);
	if (fo == NULL)
		fo = stdout;
	tag = VSL_TAG(c->rec.ptr);
	len = VSL_LEN(c->rec.ptr);
	data = VSL_CDATA(c->rec.ptr);

	if (tag == SLT_Debug) {
		VSL_PRINT(fo, "%-14s \"", VSL_tags[tag]);
		while (len-- > 0) {
			if (*data >= ' ' && *data <= '~')
				VSL_PRINT(fo, "%c", *data);
			else
				VSL_PRINT(fo, "%%%02x", (unsigned char)*data);
			data++;
		}
		VSL_PRINT(fo, "\"\n");
	} else
		VSL_PRINT(fo, "%-14s %.*s\n", VSL_tags[tag], (int)len, data);

	return (0);
}

int
VSL_PrintAll(struct VSL_data *vsl, struct VSL_cursor *c, void *fo)
{
	int i;

	if (c == NULL)
		return (0);
	while (1) {
		i = VSL_Next(c);
		if (i <= 0)
			return (i);
		if (!VSL_Match(vsl, c))
			continue;
		i = VSL_Print(vsl, c, fo);
		if (i != 0)
			return (i);
	}
}

int
VSL_PrintTransactions(struct VSL_data *vsl, struct VSL_transaction *pt[],
    void *fo)
{
	struct VSL_transaction *t;
	int i;
	int delim = 0;
	int verbose;

	(void)vsl;
	if (pt[0] == NULL)
		return (0);

	t = pt[0];
	while (t) {
		verbose = 0;
		if (t->level == 0 || vsl->v_opt)
			verbose = 1;

		if (t->level) {
			/* Print header */
			if (t->level > 3)
				VSL_PRINT(fo, "*%1.1u* ", t->level);
			else
				VSL_PRINT(fo, "%-3.*s ", t->level, "***");
			VSL_PRINT(fo, "%*.s%-14s %*.s%-10u\n",
			    verbose ? 10 + 1 : 0, " ",
			    VSL_transactions[t->type],
			    verbose ? 1 + 1 : 0, " ",
			    t->vxid);
			delim = 1;
		}

		while (1) {
			/* Print records */
			i = VSL_Next(t->c);
			if (i < 0)
				return (i);
			if (i == 0)
				break;
			if (!VSL_Match(vsl, t->c))
				continue;
			if (t->level > 3)
				VSL_PRINT(fo, "-%1.1u- ", t->level);
			else if (t->level)
				VSL_PRINT(fo, "%-3.*s ", t->level, "---");
			if (verbose)
				i = VSL_Print(vsl, t->c, fo);
			else
				i = VSL_PrintTerse(vsl, t->c, fo);
			if (i != 0)
				return (i);
		}
		t = *++pt;
	}

	if (delim)
		VSL_PRINT(fo, "\n");;

	return (0);
}
