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

struct ban_proto {
	unsigned		magic;
#define BAN_PROTO_MAGIC		0xd8adc494
	unsigned		flags;		/* BANS_FLAG_* */

	struct vsb		*vsb;
	char			*err;
};

/*--------------------------------------------------------------------
 * Variables we can ban on
 */

static const struct pvar {
	const char		*name;
	unsigned		flag;
	uint8_t			tag;
} pvars[] = {
#define PVAR(a, b, c)	{ (a), (b), (c) },
#include "tbl/ban_vars.h"
	{ 0, 0, 0}
};

/*--------------------------------------------------------------------
 */

static char ban_build_err_no_mem[] = "No Memory";

/*--------------------------------------------------------------------
 */

struct ban_proto *
BAN_Build(void)
{
	struct ban_proto *bp;

	ALLOC_OBJ(bp, BAN_PROTO_MAGIC);
	if (bp == NULL)
		return (bp);
	bp->vsb = VSB_new_auto();
	if (bp->vsb == NULL) {
		FREE_OBJ(bp);
		return (NULL);
	}
	return (bp);
}

void
BAN_Abandon(struct ban_proto *bp)
{

	CHECK_OBJ_NOTNULL(bp, BAN_PROTO_MAGIC);
	VSB_destroy(&bp->vsb);
	FREE_OBJ(bp);
}

/*--------------------------------------------------------------------
 */

static void
ban_add_lump(const struct ban_proto *bp, const void *p, uint32_t len)
{
	uint8_t buf[PRNDUP(sizeof len)] = { 0xff };

	while (VSB_len(bp->vsb) & PALGN)
		VSB_putc(bp->vsb, buf[0]);
	vbe32enc(buf, len);
	VSB_bcat(bp->vsb, buf, sizeof buf);
	VSB_bcat(bp->vsb, p, len);
}

/*--------------------------------------------------------------------
 */

static const char *
ban_error(struct ban_proto *bp, const char *fmt, ...)
{
	va_list ap;

	CHECK_OBJ_NOTNULL(bp, BAN_PROTO_MAGIC);
	AN(bp->vsb);

	/* First error is sticky */
	if (bp->err == NULL) {
		if (fmt == ban_build_err_no_mem) {
			bp->err = ban_build_err_no_mem;
		} else {
			/* Record the error message in the vsb */
			VSB_clear(bp->vsb);
			va_start(ap, fmt);
			VSB_vprintf(bp->vsb, fmt, ap);
			va_end(ap);
			AZ(VSB_finish(bp->vsb));
			bp->err = VSB_data(bp->vsb);
		}
	}
	return (bp->err);
}

/*--------------------------------------------------------------------
 * Parse and add a http argument specification
 * Output something which HTTP_GetHdr understands
 */

static void
ban_parse_http(const struct ban_proto *bp, const char *a1)
{
	int l;

	l = strlen(a1) + 1;
	assert(l <= 127);
	VSB_putc(bp->vsb, (char)l);
	VSB_cat(bp->vsb, a1);
	VSB_putc(bp->vsb, ':');
	VSB_putc(bp->vsb, '\0');
}

/*--------------------------------------------------------------------
 * Parse and add a ban test specification
 */

static const char *
ban_parse_regexp(struct ban_proto *bp, const char *a3)
{
	const char *error;
	int erroroffset, rc;
	size_t sz;
	pcre *re;

	re = pcre_compile(a3, 0, &error, &erroroffset, NULL);
	if (re == NULL)
		return (ban_error(bp, "Regex compile error: %s", error));
	rc = pcre_fullinfo(re, NULL, PCRE_INFO_SIZE, &sz);
	AZ(rc);
	ban_add_lump(bp, re, sz);
	pcre_free(re);
	return (0);
}

/*--------------------------------------------------------------------
 * Add a (and'ed) test-condition to a ban
 */

const char *
BAN_AddTest(struct ban_proto *bp,
    const char *a1, const char *a2, const char *a3)
{
	const struct pvar *pv;
	const char *err;

	CHECK_OBJ_NOTNULL(bp, BAN_PROTO_MAGIC);
	AN(bp->vsb);
	AN(a1);
	AN(a2);
	AN(a3);

	if (bp->err != NULL)
		return (bp->err);

	for (pv = pvars; pv->name != NULL; pv++)
		if (!strncmp(a1, pv->name, strlen(pv->name)))
			break;

	if (pv->name == NULL)
		return (ban_error(bp,
		    "Unknown or unsupported field \"%s\"", a1));

	bp->flags |= pv->flag;

	VSB_putc(bp->vsb, pv->tag);
	if (pv->flag & BANS_FLAG_HTTP)
		ban_parse_http(bp, a1 + strlen(pv->name));

	ban_add_lump(bp, a3, strlen(a3) + 1);
	if (!strcmp(a2, "~")) {
		VSB_putc(bp->vsb, BANS_OPER_MATCH);
		err = ban_parse_regexp(bp, a3);
		if (err)
			return (err);
	} else if (!strcmp(a2, "!~")) {
		VSB_putc(bp->vsb, BANS_OPER_NMATCH);
		err = ban_parse_regexp(bp, a3);
		if (err)
			return (err);
	} else if (!strcmp(a2, "==")) {
		VSB_putc(bp->vsb, BANS_OPER_EQ);
	} else if (!strcmp(a2, "!=")) {
		VSB_putc(bp->vsb, BANS_OPER_NEQ);
	} else {
		return (ban_error(bp,
		    "expected conditional (~, !~, == or !=) got \"%s\"", a2));
	}
	return (NULL);
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

const char *
BAN_Commit(struct ban_proto *bp)
{
	struct ban  *b, *bi;
	ssize_t ln;
	double t0;

	CHECK_OBJ_NOTNULL(bp, BAN_PROTO_MAGIC);
	AN(bp->vsb);

	if (ban_shutdown)
		return (ban_error(bp, "Shutting down"));

	AZ(VSB_finish(bp->vsb));
	ln = VSB_len(bp->vsb);
	assert(ln >= 0);

	ALLOC_OBJ(b, BAN_MAGIC);
	if (b == NULL)
		return (ban_error(bp, ban_build_err_no_mem));
	VTAILQ_INIT(&b->objcore);

	b->spec = malloc(ln + BANS_HEAD_LEN);
	if (b->spec == NULL) {
		free(b);
		return (ban_error(bp, ban_build_err_no_mem));
	}

	b->flags = bp->flags;

	memset(b->spec, 0, BANS_HEAD_LEN);
	t0 = VTIM_real();
	memcpy(b->spec + BANS_TIMESTAMP, &t0, sizeof t0);
	b->spec[BANS_FLAGS] = b->flags & 0xff;
	memcpy(b->spec + BANS_HEAD_LEN, VSB_data(bp->vsb), ln);
	ln += BANS_HEAD_LEN;
	vbe32enc(b->spec + BANS_LENGTH, ln);

	Lck_Lock(&ban_mtx);
	if (ban_shutdown) {
		/* We could have raced a shutdown */
		Lck_Unlock(&ban_mtx);
		BAN_Free(b);
		return (ban_error(bp, "Shutting down"));
	}
	bi = VTAILQ_FIRST(&ban_head);
	VTAILQ_INSERT_HEAD(&ban_head, b, list);
	ban_start = b;

	VSC_C_main->bans++;
	VSC_C_main->bans_added++;
	bans_persisted_bytes += ln;
	VSC_C_main->bans_persisted_bytes = bans_persisted_bytes;

	if (b->flags & BANS_FLAG_OBJ)
		VSC_C_main->bans_obj++;
	if (b->flags & BANS_FLAG_REQ)
		VSC_C_main->bans_req++;

	if (bi != NULL)
		ban_info_new(b->spec, ln);	/* Notify stevedores */

	if (cache_param->ban_dups) {
		/* Hunt down duplicates, and mark them as completed */
		for (bi = VTAILQ_NEXT(b, list); bi != NULL;
		    bi = VTAILQ_NEXT(bi, list)) {
			if (!(bi->flags & BANS_FLAG_COMPLETED) &&
			    ban_equal(b->spec, bi->spec)) {
				ban_mark_completed(bi);
				VSC_C_main->bans_dups++;
			}
		}
	}
	if (!(b->flags & BANS_FLAG_REQ))
		ban_kick_lurker();
	Lck_Unlock(&ban_mtx);

	BAN_Abandon(bp);
	return (NULL);
}
