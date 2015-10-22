/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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

#include <pcre.h>

#include "cache.h"
#include "cache_ban.h"

#include "vend.h"
#include "vtim.h"

/*--------------------------------------------------------------------
 * Variables we can purge on
 */

static const struct pvar {
	const char		*name;
	unsigned		flag;
	uint8_t			tag;
} pvars[] = {
#define PVAR(a, b, c)	{ (a), (b), (c) },
#include "tbl/ban_vars.h"
#undef PVAR
	{ 0, 0, 0}
};

static void
ban_add_lump(const struct ban *b, const void *p, uint32_t len)
{
	uint8_t buf[sizeof len];

	buf[0] = 0xff;
	while (VSB_len(b->vsb) & PALGN)
		VSB_bcat(b->vsb, buf, 1);
	vbe32enc(buf, len);
	VSB_bcat(b->vsb, buf, sizeof buf);
	VSB_bcat(b->vsb, p, len);
}

/*--------------------------------------------------------------------
 */

static int
ban_error(struct ban *b, const char *fmt, ...)
{
	va_list ap;

	CHECK_OBJ_NOTNULL(b, BAN_MAGIC);
	AN(b->vsb);

	/* First error is sticky */
	if (!(b->flags & BANS_FLAG_ERROR)) {
		b->flags |= BANS_FLAG_ERROR;

		/* Record the error message in the vsb */
		VSB_clear(b->vsb);
		va_start(ap, fmt);
		(void)VSB_vprintf(b->vsb, fmt, ap);
		va_end(ap);
	}
	return (-1);
}

/*--------------------------------------------------------------------
 * Parse and add a http argument specification
 * Output something which HTTP_GetHdr understands
 */

static void
ban_parse_http(const struct ban *b, const char *a1)
{
	int l;

	l = strlen(a1) + 1;
	assert(l <= 127);
	VSB_putc(b->vsb, (char)l);
	VSB_cat(b->vsb, a1);
	VSB_putc(b->vsb, ':');
	VSB_putc(b->vsb, '\0');
}

/*--------------------------------------------------------------------
 * Parse and add a ban test specification
 */

static int
ban_parse_regexp(struct ban *b, const char *a3)
{
	const char *error;
	int erroroffset, rc;
	size_t sz;
	pcre *re;

	re = pcre_compile(a3, 0, &error, &erroroffset, NULL);
	if (re == NULL)
		return (ban_error(b, "Regex compile error: %s", error));
	rc = pcre_fullinfo(re, NULL, PCRE_INFO_SIZE, &sz);
	AZ(rc);
	ban_add_lump(b, re, sz);
	pcre_free(re);
	return (0);
}

/*--------------------------------------------------------------------
 * Add a (and'ed) test-condition to a ban
 */

int
BAN_AddTest(struct ban *b, const char *a1, const char *a2, const char *a3)
{
	const struct pvar *pv;
	int i;

	CHECK_OBJ_NOTNULL(b, BAN_MAGIC);
	AN(b->vsb);
	AN(a1);
	AN(a2);
	AN(a3);

	if (b->flags & BANS_FLAG_ERROR)
		return (-1);

	for (pv = pvars; pv->name != NULL; pv++)
		if (!strncmp(a1, pv->name, strlen(pv->name)))
			break;

	if (pv->name == NULL)
		return (ban_error(b,
		    "Unknown or unsupported field \"%s\"", a1));

	b->flags |= pv->flag;

	VSB_putc(b->vsb, pv->tag);
	if (pv->flag & BANS_FLAG_HTTP)
		ban_parse_http(b, a1 + strlen(pv->name));

	ban_add_lump(b, a3, strlen(a3) + 1);
	if (!strcmp(a2, "~")) {
		VSB_putc(b->vsb, BANS_OPER_MATCH);
		i = ban_parse_regexp(b, a3);
		if (i)
			return (i);
	} else if (!strcmp(a2, "!~")) {
		VSB_putc(b->vsb, BANS_OPER_NMATCH);
		i = ban_parse_regexp(b, a3);
		if (i)
			return (i);
	} else if (!strcmp(a2, "==")) {
		VSB_putc(b->vsb, BANS_OPER_EQ);
	} else if (!strcmp(a2, "!=")) {
		VSB_putc(b->vsb, BANS_OPER_NEQ);
	} else {
		return (ban_error(b,
		    "expected conditional (~, !~, == or !=) got \"%s\"", a2));
	}
	return (0);
}

/*--------------------------------------------------------------------
 * We maintain ban_start as a pointer to the first element of the list
 * as a separate variable from the VTAILQ, to avoid depending on the
 * internals of the VTAILQ macros.  We tacitly assume that a pointer
 * write is always atomic in doing so.
 *
 * Returns:
 *   0: Ban successfully inserted
 *  -1: Ban not inserted due to shutdown in progress. The ban has been
 *      deleted.
 */

static char ban_error_nomem[] = "Could not get memory";

static char *
ban_ins_error(const char *p)
{
	char *r = NULL;

	if (p != NULL)
		r = strdup(p);
	if (r == NULL)
		r = ban_error_nomem;
	return (r);
}

void
BAN_Free_Errormsg(char *p)
{
	if (p != ban_error_nomem)
		free(p);
}

char *
BAN_Insert(struct ban *b)
{
	struct ban  *bi, *be;
	ssize_t ln;
	double t0;
	char *p;

	CHECK_OBJ_NOTNULL(b, BAN_MAGIC);
	AN(b->vsb);

	if (ban_shutdown) {
		BAN_Free(b);
		return (ban_ins_error("Shutting down"));
	}

	AZ(VSB_finish(b->vsb));
	ln = VSB_len(b->vsb);
	assert(ln >= 0);

	if (b->flags & BANS_FLAG_ERROR) {
		p = ban_ins_error(VSB_data(b->vsb));
		BAN_Free(b);
		return (p);
	}

	b->spec = malloc(ln + BANS_HEAD_LEN);
	if (b->spec == NULL) {
		BAN_Free(b);
		return (ban_ins_error(NULL));
	}

	memset(b->spec, 0, BANS_HEAD_LEN);
	t0 = VTIM_real();
	memcpy(b->spec + BANS_TIMESTAMP, &t0, sizeof t0);
	b->spec[BANS_FLAGS] = b->flags & 0xff;
	memcpy(b->spec + BANS_HEAD_LEN, VSB_data(b->vsb), ln);
	ln += BANS_HEAD_LEN;
	vbe32enc(b->spec + BANS_LENGTH, ln);

	VSB_delete(b->vsb);
	b->vsb = NULL;

	Lck_Lock(&ban_mtx);
	if (ban_shutdown) {
		/* Check again, we might have raced */
		Lck_Unlock(&ban_mtx);
		BAN_Free(b);
		return (ban_ins_error("Shutting down"));
	}
	VTAILQ_INSERT_HEAD(&ban_head, b, list);
	ban_start = b;
	VSC_C_main->bans++;
	VSC_C_main->bans_added++;
	if (b->flags & BANS_FLAG_OBJ)
		VSC_C_main->bans_obj++;
	if (b->flags & BANS_FLAG_REQ)
		VSC_C_main->bans_req++;

	be = VTAILQ_LAST(&ban_head, banhead_s);
	if (cache_param->ban_dups && be != b)
		be->refcount++;
	else
		be = NULL;

	/* ban_magic is magic, and needs to be inserted early to give
	 * a handle to grab a ref on. We don't report it here as the
	 * stevedores will not be opened and ready to accept it
	 * yet. Instead it is reported on BAN_Compile, which is after
	 * the stevedores has been opened, but before any new objects
	 * can have entered the cache (thus no objects in the mean
	 * time depending on ban_magic in the list) */
	VSC_C_main->bans_persisted_bytes += ln;
	if (b != ban_magic)
		ban_info(BI_NEW, b->spec, ln); /* Notify stevedores */
	Lck_Unlock(&ban_mtx);

	if (be == NULL)
		return (NULL);

	/* Hunt down duplicates, and mark them as completed */
	bi = b;
	Lck_Lock(&ban_mtx);
	while (!ban_shutdown && bi != be) {
		bi = VTAILQ_NEXT(bi, list);
		if (bi->flags & BANS_FLAG_COMPLETED)
			continue;
		if (!ban_equal(b->spec, bi->spec))
			continue;
		ban_mark_completed(bi);
		VSC_C_main->bans_dups++;
	}
	be->refcount--;
	Lck_Unlock(&ban_mtx);

	return (NULL);
}
