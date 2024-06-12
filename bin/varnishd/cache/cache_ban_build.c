/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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

#include <stdlib.h>

#include "cache_varnishd.h"
#include "cache_ban.h"

#include "vend.h"
#include "vtim.h"
#include "vnum.h"

void BAN_Build_Init(void);
void BAN_Build_Fini(void);

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

/* operators allowed per argument (pvar.tag) */
static const unsigned arg_opervalid[BAN_ARGARRSZ + 1] = {
#define ARGOPER(arg, mask) [BAN_ARGIDX(arg)] = (mask),
#include "tbl/ban_arg_oper.h"
	[BAN_ARGARRSZ] = 0
};

// init'ed in _Init
static const char *arg_operhelp[BAN_ARGARRSZ + 1];

// operators
const char * const ban_oper[BAN_OPERARRSZ + 1] = {
#define OPER(op, str) [BAN_OPERIDX(op)] = (str),
#include "tbl/ban_oper.h"
	[BAN_OPERARRSZ] = NULL
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

// TODO: change to (struct ban_proto **)
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
	struct vsb vsb[1];
	char errbuf[VRE_ERROR_LEN];
	int errorcode, erroroffset;
	size_t sz;
	vre_t *re, *rex;

	re = VRE_compile(a3, 0, &errorcode, &erroroffset, 0);
	if (re == NULL) {
		AN(VSB_init(vsb, errbuf, sizeof errbuf));
		AZ(VRE_error(vsb, errorcode));
		AZ(VSB_finish(vsb));
		VSB_fini(vsb);
		return (ban_error(bp, "Regex compile error: %s", errbuf));
	}

	rex = VRE_export(re, &sz);
	AN(rex);
	ban_add_lump(bp, rex, sz);
	VRE_free(&rex);
	VRE_free(&re);
	return (0);
}

static int
ban_parse_oper(const char *p)
{
	int i;

	for (i = 0; i < BAN_OPERARRSZ; i++) {
		if (!strcmp(p, ban_oper[i]))
			return (BANS_OPER_OFF_ + i);
	}
	return (-1);
}

/*--------------------------------------------------------------------
 * Add a (and'ed) test-condition to a ban
 */

const char *
BAN_AddTest(struct ban_proto *bp,
    const char *a1, const char *a2, const char *a3)
{
	const struct pvar *pv;
	double darg;
	uint64_t dtmp;
	uint8_t denc[sizeof darg];
	int op;

	CHECK_OBJ_NOTNULL(bp, BAN_PROTO_MAGIC);
	AN(bp->vsb);
	AN(a1);
	AN(a2);
	AN(a3);

	if (bp->err != NULL)
		return (bp->err);

	for (pv = pvars; pv->name != NULL; pv++) {
		if (!(pv->flag & BANS_FLAG_HTTP) && !strcmp(a1, pv->name))
			break;
		if ((pv->flag & BANS_FLAG_HTTP) && !strncmp(a1, pv->name, strlen(pv->name)))
			break;
	}

	if (pv->name == NULL)
		return (ban_error(bp,
		    "Unknown or unsupported field \"%s\"", a1));

	bp->flags |= pv->flag;

	VSB_putc(bp->vsb, pv->tag);
	if (pv->flag & BANS_FLAG_HTTP) {
		if (strlen(a1 + strlen(pv->name)) < 1)
			return (ban_error(bp,
			    "Missing header name: \"%s\"", pv->name));
		assert(BANS_HAS_ARG1_SPEC(pv->tag));
		ban_parse_http(bp, a1 + strlen(pv->name));
	}

	op = ban_parse_oper(a2);
	if (op < BANS_OPER_OFF_ ||
	    ((1U << BAN_OPERIDX(op)) & arg_opervalid[BAN_ARGIDX(pv->tag)]) == 0)
		return (ban_error(bp,
		    "expected conditional (%s) got \"%s\"",
		    arg_operhelp[BAN_ARGIDX(pv->tag)], a2));

	if ((pv->flag & BANS_FLAG_DURATION) == 0) {
		assert(! BANS_HAS_ARG2_DOUBLE(pv->tag));

		ban_add_lump(bp, a3, strlen(a3) + 1);
		VSB_putc(bp->vsb, op);

		if (! BANS_HAS_ARG2_SPEC(op))
			return (NULL);

		return (ban_parse_regexp(bp, a3));
	}

	assert(pv->flag & BANS_FLAG_DURATION);
	assert(BANS_HAS_ARG2_DOUBLE(pv->tag));
	darg = VNUM_duration(a3);
	if (isnan(darg)) {
		return (ban_error(bp,
		    "expected duration <n.nn>[ms|s|m|h|d|w|y] got \"%s\"", a3));
	}

	assert(sizeof darg == sizeof dtmp);
	assert(sizeof dtmp == sizeof denc);
	memcpy(&dtmp, &darg, sizeof dtmp);
	vbe64enc(denc, dtmp);

	ban_add_lump(bp, denc, sizeof denc);
	VSB_putc(bp->vsb, op);
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
	vtim_real t0;
	uint64_t u;

	CHECK_OBJ_NOTNULL(bp, BAN_PROTO_MAGIC);
	AN(bp->vsb);
	assert(sizeof u == sizeof t0);

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
	memcpy(&u, &t0, sizeof u);
	vbe64enc(b->spec + BANS_TIMESTAMP, u);
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

	if (bi != NULL)
		ban_info_new(b->spec, ln);	/* Notify stevedores */
	Lck_Unlock(&ban_mtx);

	BAN_Abandon(bp);
	return (NULL);
}

static void
ban_build_arg_operhelp(struct vsb *vsb, int arg)
{
	unsigned mask;
	const char *p = NULL, *n = NULL;
	int i;

	ASSERT_BAN_ARG(arg);
	mask = arg_opervalid[BAN_ARGIDX(arg)];

	for (i = 0; i < BAN_OPERARRSZ; i++) {
		if ((mask & (1U << i)) == 0)
			continue;
		if (p == NULL)
			p = ban_oper[i];
		else if (n == NULL)
			n = ban_oper[i];
		else {
			VSB_cat(vsb, p);
			VSB_cat(vsb, ", ");
			p = n;
			n = ban_oper[i];
		}
	}

	if (n) {
		AN(p);
		VSB_cat(vsb, p);
		VSB_cat(vsb, " or ");
		VSB_cat(vsb, n);
		return;
	}

	AN(p);
	VSB_cat(vsb, p);
}

void
BAN_Build_Init(void) {
	struct vsb *vsb;
	int i;

	vsb = VSB_new_auto();
	AN(vsb);
	for (i = BANS_ARG_OFF_; i < BANS_ARG_LIM; i ++) {
		VSB_clear(vsb);
		ban_build_arg_operhelp(vsb, i);
		AZ(VSB_finish(vsb));

		arg_operhelp[BAN_ARGIDX(i)] = strdup(VSB_data(vsb));
		AN(arg_operhelp[BAN_ARGIDX(i)]);
	}
	arg_operhelp[BAN_ARGIDX(i)] = NULL;
	VSB_destroy(&vsb);
}

void
BAN_Build_Fini(void) {
	int i;

	for (i = 0; i < BAN_ARGARRSZ; i++)
		free(TRUST_ME(arg_operhelp[i]));
}
