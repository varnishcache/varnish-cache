/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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
#include "vbm.h"
#include "vmb.h"
#include "vre.h"
#include "vsb.h"
#include "vsl_api.h"
#include "vsm_api.h"

/*--------------------------------------------------------------------*/

const char * const VSL_tags[SLT__MAX] = {
#  define SLTM(foo,flags,sdesc,ldesc)       [SLT_##foo] = #foo,
#  include "tbl/vsl_tags.h"
#  undef SLTM
};

const unsigned VSL_tagflags[SLT__MAX] = {
#  define SLTM(foo, flags, sdesc, ldesc)	[SLT_##foo] = flags,
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

	vsl->L_opt = 1000;
	vsl->T_opt = 120.;
	vsl->vbm_select = vbit_init(SLT__MAX);
	vsl->vbm_supress = vbit_init(SLT__MAX);
	VTAILQ_INIT(&vsl->vslf_select);
	VTAILQ_INIT(&vsl->vslf_suppress);

	return (vsl);
}

static void
vsl_IX_free(vslf_list *filters)
{
	struct vslf *vslf;

	while (!VTAILQ_EMPTY(filters)) {
		vslf = VTAILQ_FIRST(filters);
		CHECK_OBJ_NOTNULL(vslf, VSLF_MAGIC);
		VTAILQ_REMOVE(filters, vslf, list);
		if (vslf->tags)
			vbit_destroy(vslf->tags);
		AN(vslf->vre);
		VRE_free(&vslf->vre);
		AZ(vslf->vre);
	}
}

void
VSL_Delete(struct VSL_data *vsl)
{

	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);

	vbit_destroy(vsl->vbm_select);
	vbit_destroy(vsl->vbm_supress);
	vsl_IX_free(&vsl->vslf_select);
	vsl_IX_free(&vsl->vslf_suppress);
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

static int
vsl_match_IX(struct VSL_data *vsl, const vslf_list *list,
    const struct VSL_cursor *c)
{
	enum VSL_tag_e tag;
	const char *cdata;
	int len;
	const struct vslf *vslf;

	(void)vsl;
	tag = VSL_TAG(c->rec.ptr);
	cdata = VSL_CDATA(c->rec.ptr);
	len = VSL_LEN(c->rec.ptr);

	VTAILQ_FOREACH(vslf, list, list) {
		CHECK_OBJ_NOTNULL(vslf, VSLF_MAGIC);
		if (vslf->tags != NULL && !vbit_test(vslf->tags, tag))
			continue;
		if (VRE_exec(vslf->vre, cdata, len, 0, 0, NULL, 0, NULL) >= 0)
			return (1);
	}
	return (0);
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
	if (vsl->c_opt && !VSL_CLIENT(c->rec.ptr))
		return (0);
	if (vsl->b_opt && !VSL_BACKEND(c->rec.ptr))
		return (0);
	if (!VTAILQ_EMPTY(&vsl->vslf_select) &&
	    vsl_match_IX(vsl, &vsl->vslf_select, c))
		return (1);
	else if (vbit_test(vsl->vbm_select, tag))
		return (1);
	else if (!VTAILQ_EMPTY(&vsl->vslf_suppress) &&
	    vsl_match_IX(vsl, &vsl->vslf_suppress, c))
		return (0);
	else if (vbit_test(vsl->vbm_supress, tag))
		return (0);

	/* Default show */
	return (1);
}

static const char * const VSL_transactions[VSL_t__MAX] = {
	/*                 12345678901234 */
	[VSL_t_unknown] = "<< Unknown  >>",
	[VSL_t_sess]	= "<< Session  >>",
	[VSL_t_req]	= "<< Request  >>",
	[VSL_t_bereq]	= "<< BeReq    >>",
	[VSL_t_raw]	= "<< Record   >>",
};

#define VSL_PRINT(...)					\
	do {						\
		if (0 > fprintf(__VA_ARGS__))		\
			return (-5);			\
	} while (0)

int
VSL_Print(const struct VSL_data *vsl, const struct VSL_cursor *c, void *fo)
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

	if (VSL_tagflags[tag] & SLT_F_BINARY) {
		VSL_PRINT(fo, "%10u %-14s %c \"", vxid, VSL_tags[tag], type);
		while (len-- > 0) {
			if (len == 0 && tag == SLT_Debug && *data == '\0')
				break;
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
VSL_PrintTerse(const struct VSL_data *vsl, const struct VSL_cursor *c, void *fo)
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

	if (VSL_tagflags[tag] & SLT_F_BINARY) {
		VSL_PRINT(fo, "%-14s \"", VSL_tags[tag]);
		while (len-- > 0) {
			if (len == 0 && tag == SLT_Debug && *data == '\0')
				break;
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
VSL_PrintAll(struct VSL_data *vsl, const struct VSL_cursor *c, void *fo)
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

int __match_proto__(VSLQ_dispatch_f)
VSL_PrintTransactions(struct VSL_data *vsl, struct VSL_transaction * const pt[],
    void *fo)
{
	struct VSL_transaction *t;
	int i;
	int delim = 0;
	int verbose;

	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);
	if (fo == NULL)
		fo = stdout;
	if (pt[0] == NULL)
		return (0);

	for (t = pt[0]; t != NULL; t = *++pt) {
		if (vsl->c_opt || vsl->b_opt) {
			switch (t->type) {
			case VSL_t_req:
				if (!vsl->c_opt)
					continue;
				break;
			case VSL_t_bereq:
				if (!vsl->b_opt)
					continue;
				break;
			case VSL_t_raw:
				break;
			default:
				continue;
			}
		}

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
	}

	if (delim)
		VSL_PRINT(fo, "\n");;

	return (0);
}

FILE*
VSL_WriteOpen(struct VSL_data *vsl, const char *name, int append, int unbuf)
{
	const char head[] = VSL_FILE_ID;
	FILE* f;

	f = fopen(name, append ? "a" : "w");
	if (f == NULL) {
		vsl_diag(vsl, "%s", strerror(errno));
		return (NULL);
	}
	if (unbuf)
		setbuf(f, NULL);
	if (0 == ftell(f))
		fwrite(head, 1, sizeof head, f);
	return (f);
}

int
VSL_Write(const struct VSL_data *vsl, const struct VSL_cursor *c, void *fo)
{
	size_t r;

	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);
	if (c == NULL || c->rec.ptr == NULL)
		return (0);
	if (fo == NULL)
		fo = stdout;
	r = fwrite(c->rec.ptr, sizeof *c->rec.ptr,
	    VSL_NEXT(c->rec.ptr) - c->rec.ptr, fo);
	if (r == 0)
		return (-5);
	return (0);
}

int
VSL_WriteAll(struct VSL_data *vsl, const struct VSL_cursor *c, void *fo)
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
		i = VSL_Write(vsl, c, fo);
		if (i != 0)
			return (i);
	}
}

int __match_proto__(VSLQ_dispatch_f)
VSL_WriteTransactions(struct VSL_data *vsl, struct VSL_transaction * const pt[],
    void *fo)
{
	struct VSL_transaction *t;
	int i;

	if (pt == NULL)
		return (0);
	for (i = 0, t = pt[0]; i == 0 && t != NULL; t = *++pt)
		i = VSL_WriteAll(vsl, t->c, fo);
	return (i);
}
