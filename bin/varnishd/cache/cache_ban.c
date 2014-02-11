/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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
 * Ban processing
 *
 * A ban consists of a number of conditions (or tests), all of which must be
 * satisfied.  Here are some potential bans we could support:
 *
 *	req.url == "/foo"
 *	req.url ~ ".iso" && obj.size > 10MB
 *	req.http.host ~ "web1.com" && obj.http.set-cookie ~ "USER=29293"
 *
 * We make the "&&" mandatory from the start, leaving the syntax space
 * for latter handling of "||" as well.
 *
 * Bans are compiled into bytestrings as follows:
 *	8 bytes	- double: timestamp		XXX: Byteorder ?
 *	4 bytes - be32: length
 *	1 byte - flags: 0x01: BAN_F_{REQ|OBJ|COMPLETED}
 *	N tests
 * A test have this form:
 *	1 byte - arg (see ban_vars.h col 3 "BANS_ARG_XXX")
 *	(n bytes) - http header name, canonical encoding
 *	lump - comparison arg
 *	1 byte - operation (BANS_OPER_)
 *	(lump) - compiled regexp
 * A lump is:
 *	4 bytes - be32: length
 *	n bytes - content
 *
 * In a perfect world, we should vector through VRE to get to PCRE,
 * but since we rely on PCRE's ability to encode the regexp into a
 * byte string, that would be a little bit artificial, so this is
 * the exception that confirmes the rule.
 *
 */

#include "config.h"

#include <math.h>
#include <pcre.h>
#include <stdarg.h>
#include <stdio.h>

#include "cache.h"
#include "storage/storage.h"

#include "hash/hash_slinger.h"
#include "vcli.h"
#include "vcli_priv.h"
#include "vend.h"
#include "vmb.h"
#include "vtim.h"

/*--------------------------------------------------------------------
 * BAN string defines & magic markers
 */

#define BANS_TIMESTAMP		0
#define BANS_LENGTH		8
#define BANS_FLAGS		12
#define BANS_HEAD_LEN		13

#define BANS_FLAG_REQ		(1<<0)
#define BANS_FLAG_OBJ		(1<<1)
#define BANS_FLAG_COMPLETED	(1<<2)
#define BANS_FLAG_HTTP		(1<<3)
#define BANS_FLAG_ERROR		(1<<4)

#define BANS_OPER_EQ		0x10
#define BANS_OPER_NEQ		0x11
#define BANS_OPER_MATCH		0x12
#define BANS_OPER_NMATCH	0x13

#define BANS_ARG_URL		0x18
#define BANS_ARG_REQHTTP	0x19
#define BANS_ARG_OBJHTTP	0x1a
#define BANS_ARG_OBJSTATUS	0x1b

/*--------------------------------------------------------------------*/

struct ban {
	unsigned		magic;
#define BAN_MAGIC		0x700b08ea
	VTAILQ_ENTRY(ban)	list;
	VTAILQ_ENTRY(ban)	l_list;
	int			refcount;
	unsigned		flags;		/* BANS_FLAG_* */

	VTAILQ_HEAD(,objcore)	objcore;
	struct vsb		*vsb;
	uint8_t			*spec;
};

VTAILQ_HEAD(banhead_s,ban);

struct ban_test {
	uint8_t			arg1;
	const char		*arg1_spec;
	uint8_t			oper;
	const char		*arg2;
	const void		*arg2_spec;
};

static void ban_info(enum baninfo event, const uint8_t *ban, unsigned len);
static struct banhead_s ban_head = VTAILQ_HEAD_INITIALIZER(ban_head);
static struct lock ban_mtx;
static struct ban *ban_magic;
static pthread_t ban_thread;
static struct ban * volatile ban_start;
static struct objcore oc_marker = { .magic = OBJCORE_MAGIC, };
static bgthread_t ban_lurker;
static unsigned ban_batch;
static int ban_shutdown = 0;

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

/*--------------------------------------------------------------------
 * Storage handling of bans
 */

static struct ban *
ban_alloc(void)
{
	struct ban *b;

	ALLOC_OBJ(b, BAN_MAGIC);
	if (b != NULL)
		VTAILQ_INIT(&b->objcore);
	return (b);
}

struct ban *
BAN_New(void)
{
	struct ban *b;

	b = ban_alloc();
	if (b != NULL) {
		b->vsb = VSB_new_auto();
		if (b->vsb == NULL) {
			FREE_OBJ(b);
			return (NULL);
		}
		VTAILQ_INIT(&b->objcore);
	}
	return (b);
}

void
BAN_Free(struct ban *b)
{

	CHECK_OBJ_NOTNULL(b, BAN_MAGIC);
	AZ(b->refcount);
	assert(VTAILQ_EMPTY(&b->objcore));

	if (b->vsb != NULL)
		VSB_delete(b->vsb);
	if (b->spec != NULL)
		free(b->spec);
	FREE_OBJ(b);
}

/*--------------------------------------------------------------------
 * Get & Release a tail reference, used to hold the list stable for
 * traversals etc.
 */

struct ban *
BAN_TailRef(void)
{
	struct ban *b;

	ASSERT_CLI();
	Lck_Lock(&ban_mtx);
	b = VTAILQ_LAST(&ban_head, banhead_s);
	AN(b);
	b->refcount++;
	Lck_Unlock(&ban_mtx);
	return (b);
}

void
BAN_TailDeref(struct ban **bb)
{
	struct ban *b;

	b = *bb;
	*bb = NULL;
	Lck_Lock(&ban_mtx);
	b->refcount--;
	Lck_Unlock(&ban_mtx);
}

/*--------------------------------------------------------------------
 * Extract time and length from ban-spec
 */

static double
ban_time(const uint8_t *banspec)
{
	double t;

	assert(sizeof t == (BANS_LENGTH - BANS_TIMESTAMP));
	memcpy(&t, banspec, sizeof t);
	return (t);
}

static unsigned
ban_len(const uint8_t *banspec)
{
	unsigned u;

	u = vbe32dec(banspec + BANS_LENGTH);
	return (u);
}

static int
ban_equal(const uint8_t *bs1, const uint8_t *bs2)
{
	unsigned u;

	/*
	 * Compare two ban-strings.
	 * The memcmp() is safe because the first field we compare is the
	 * length and that is part of the fixed header structure.
	 */
	u = vbe32dec(bs1 + BANS_LENGTH);
	return (!memcmp(bs1 + BANS_LENGTH, bs2 + BANS_LENGTH, u - BANS_LENGTH));
}

static void
ban_mark_completed(struct ban *b)
{
	unsigned ln;

	CHECK_OBJ_NOTNULL(b, BAN_MAGIC);
	Lck_AssertHeld(&ban_mtx);

	AN(b->spec);
	AZ(b->flags & BANS_FLAG_COMPLETED);
	ln = ban_len(b->spec);
	b->flags |= BANS_FLAG_COMPLETED;
	b->spec[BANS_FLAGS] |= BANS_FLAG_COMPLETED;
	VWMB();
	vbe32enc(b->spec + BANS_LENGTH, BANS_HEAD_LEN);
	VSC_C_main->bans_completed++;
	VSC_C_main->bans_persisted_fragmentation += ln - ban_len(b->spec);
}

/*--------------------------------------------------------------------
 * Access a lump of bytes in a ban test spec
 */

static void
ban_add_lump(const struct ban *b, const void *p, uint32_t len)
{
	uint8_t buf[sizeof len];

	vbe32enc(buf, len);
	VSB_bcat(b->vsb, buf, sizeof buf);
	VSB_bcat(b->vsb, p, len);
}

static const void *
ban_get_lump(const uint8_t **bs)
{
	const void *r;
	unsigned ln;

	ln = vbe32dec(*bs);
	*bs += 4;
	r = (const void*)*bs;
	*bs += ln;
	return (r);
}

/*--------------------------------------------------------------------
 * Pick a test apart from a spec string
 */

static void
ban_iter(const uint8_t **bs, struct ban_test *bt)
{

	memset(bt, 0, sizeof *bt);
	bt->arg1 = *(*bs)++;
	if (bt->arg1 == BANS_ARG_REQHTTP || bt->arg1 == BANS_ARG_OBJHTTP) {
		bt->arg1_spec = (const char *)*bs;
		(*bs) += (*bs)[0] + 2;
	}
	bt->arg2 = ban_get_lump(bs);
	bt->oper = *(*bs)++;
	if (bt->oper == BANS_OPER_MATCH || bt->oper == BANS_OPER_NMATCH)
		bt->arg2_spec = ban_get_lump(bs);
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
	while(!ban_shutdown && bi != be) {
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

/*--------------------------------------------------------------------
 * A new object is created, grab a reference to the newest ban
 */

void
BAN_NewObjCore(struct objcore *oc)
{

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AZ(oc->ban);
	AN(oc->objhead);
	Lck_Lock(&ban_mtx);
	oc->ban = ban_start;
	ban_start->refcount++;
	VTAILQ_INSERT_TAIL(&ban_start->objcore, oc, ban_list);
	Lck_Unlock(&ban_mtx);
}

/*--------------------------------------------------------------------
 * An object is destroyed, release its ban reference
 */

void
BAN_DestroyObj(struct objcore *oc)
{

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	if (oc->ban == NULL)
		return;
	CHECK_OBJ_NOTNULL(oc->ban, BAN_MAGIC);
	Lck_Lock(&ban_mtx);
	assert(oc->ban->refcount > 0);
	oc->ban->refcount--;
	VTAILQ_REMOVE(&oc->ban->objcore, oc, ban_list);
	oc->ban = NULL;
	Lck_Unlock(&ban_mtx);
}

/*--------------------------------------------------------------------
 * Find and/or Grab a reference to an objects ban based on timestamp
 * Assume we hold a TailRef, so list traversal is safe.
 */

struct ban *
BAN_RefBan(struct objcore *oc, double t0, const struct ban *tail)
{
	struct ban *b;
	double t1 = 0;

	VTAILQ_FOREACH(b, &ban_head, list) {
		t1 = ban_time(b->spec);
		if (t1 <= t0)
			break;
		if (b == tail)
			break;
	}
	AN(b);
	assert(t1 == t0);
	Lck_Lock(&ban_mtx);
	b->refcount++;
	VTAILQ_INSERT_TAIL(&b->objcore, oc, ban_list);
	Lck_Unlock(&ban_mtx);
	return (b);
}

/*--------------------------------------------------------------------
 * Compile a full ban list and export this area to the stevedores for
 * persistence.
 */

static void
ban_export(void)
{
	struct ban *b;
	struct vsb vsb;
	unsigned ln;

	Lck_AssertHeld(&ban_mtx);
	ln = VSC_C_main->bans_persisted_bytes -
	    VSC_C_main->bans_persisted_fragmentation;
	AN(VSB_new(&vsb, NULL, ln, VSB_AUTOEXTEND));
	VTAILQ_FOREACH_REVERSE(b, &ban_head, banhead_s, list) {
		AZ(VSB_bcat(&vsb, b->spec, ban_len(b->spec)));
	}
	AZ(VSB_finish(&vsb));
	assert(VSB_len(&vsb) == ln);
	STV_BanExport((const uint8_t *)VSB_data(&vsb), VSB_len(&vsb));
	VSB_delete(&vsb);
	VSC_C_main->bans_persisted_bytes = ln;
	VSC_C_main->bans_persisted_fragmentation = 0;
}

static void
ban_info(enum baninfo event, const uint8_t *ban, unsigned len)
{
	if (STV_BanInfo(event, ban, len)) {
		/* One or more stevedores reported failure. Export the
		 * list instead. The exported list should take up less
		 * space due to drops being purged and completed being
		 * truncated. */
		/* XXX: Keep some measure of how much space can be
		 * saved, and only export if it's worth it. Assert if
		 * not */
		ban_export();
	}
}

/*--------------------------------------------------------------------
 * Put a skeleton ban in the list, unless there is an identical,
 * time & condition, ban already in place.
 *
 * If a newer ban has same condition, mark the inserted ban COMPLETED,
 * also mark any older bans, with the same condition COMPLETED.
 */

static void
ban_reload(const uint8_t *ban, unsigned len)
{
	struct ban *b, *b2;
	int duplicate = 0;
	double t0, t1, t2 = 9e99;

	ASSERT_CLI();
	Lck_AssertHeld(&ban_mtx);

	t0 = ban_time(ban);
	assert(len == ban_len(ban));

	VTAILQ_FOREACH(b, &ban_head, list) {
		t1 = ban_time(b->spec);
		assert(t1 < t2);
		t2 = t1;
		if (t1 == t0)
			return;
		if (t1 < t0)
			break;
		if (ban_equal(b->spec, ban))
			duplicate = 1;
	}

	VSC_C_main->bans++;
	VSC_C_main->bans_added++;

	b2 = ban_alloc();
	AN(b2);
	b2->spec = malloc(len);
	AN(b2->spec);
	memcpy(b2->spec, ban, len);
	if (ban[BANS_FLAGS] & BANS_FLAG_REQ) {
		VSC_C_main->bans_req++;
		b2->flags |= BANS_FLAG_REQ;
	}
	if (duplicate)
		VSC_C_main->bans_dups++;
	if (duplicate || (ban[BANS_FLAGS] & BANS_FLAG_COMPLETED))
		ban_mark_completed(b2);
	if (b == NULL)
		VTAILQ_INSERT_TAIL(&ban_head, b2, list);
	else
		VTAILQ_INSERT_BEFORE(b, b2, list);
	VSC_C_main->bans_persisted_bytes += len;

	/* Hunt down older duplicates */
	for (b = VTAILQ_NEXT(b2, list); b != NULL; b = VTAILQ_NEXT(b, list)) {
		if (b->flags & BANS_FLAG_COMPLETED)
			continue;
		if (ban_equal(b->spec, ban)) {
			ban_mark_completed(b);
			VSC_C_main->bans_dups++;
		}
	}
}

/*--------------------------------------------------------------------
 * Reload a series of persisted ban specs
 */

void
BAN_Reload(const uint8_t *ptr, unsigned len)
{
	const uint8_t *pe;
	unsigned l;

	AZ(ban_shutdown);
	pe = ptr + len;
	Lck_Lock(&ban_mtx);
	while (ptr < pe) {
		/* XXX: This can be optimized by traversing the live
		 * ban list together with the reload list (combining
		 * the loops in BAN_Reload and ban_reload). */
		l = ban_len(ptr);
		assert(ptr + l <= pe);
		ban_reload(ptr, l);
		ptr += l;
	}
	Lck_Unlock(&ban_mtx);
}

/*--------------------------------------------------------------------
 * Get a bans timestamp
 */

double
BAN_Time(const struct ban *b)
{

	if (b == NULL)
		return (0.0);

	CHECK_OBJ_NOTNULL(b, BAN_MAGIC);
	return (ban_time(b->spec));
}

/*--------------------------------------------------------------------
 * All silos have read their bans, ready for action
 */

void
BAN_Compile(void)
{

	ASSERT_CLI();
	AZ(ban_shutdown);

	Lck_Lock(&ban_mtx);

	/* Do late reporting of ban_magic */
	AZ(STV_BanInfo(BI_NEW, ban_magic->spec, ban_len(ban_magic->spec)));

	/* All bans have been read from all persistent stevedores. Export
	   the compiled list */
	ban_export();

	Lck_Unlock(&ban_mtx);

	ban_start = VTAILQ_FIRST(&ban_head);
	WRK_BgThread(&ban_thread, "ban-lurker", ban_lurker, NULL);
}

/*--------------------------------------------------------------------
 * Evaluate ban-spec
 */

static int
ban_evaluate(const uint8_t *bs, const struct http *objhttp,
    const struct http *reqhttp, unsigned *tests)
{
	struct ban_test bt;
	const uint8_t *be;
	char *arg1;
	char buf[10];

	be = bs + ban_len(bs);
	bs += 13;
	while (bs < be) {
		(*tests)++;
		ban_iter(&bs, &bt);
		arg1 = NULL;
		switch (bt.arg1) {
		case BANS_ARG_URL:
			AN(reqhttp);
			arg1 = reqhttp->hd[HTTP_HDR_URL].b;
			break;
		case BANS_ARG_REQHTTP:
			AN(reqhttp);
			(void)http_GetHdr(reqhttp, bt.arg1_spec, &arg1);
			break;
		case BANS_ARG_OBJHTTP:
			(void)http_GetHdr(objhttp, bt.arg1_spec, &arg1);
			break;
		case BANS_ARG_OBJSTATUS:
			arg1 = buf;
			sprintf(buf, "%d", objhttp->status);
			break;
		default:
			WRONG("Wrong BAN_ARG code");
		}

		switch (bt.oper) {
		case BANS_OPER_EQ:
			if (arg1 == NULL || strcmp(arg1, bt.arg2))
				return (0);
			break;
		case BANS_OPER_NEQ:
			if (arg1 != NULL && !strcmp(arg1, bt.arg2))
				return (0);
			break;
		case BANS_OPER_MATCH:
			if (arg1 == NULL ||
			    pcre_exec(bt.arg2_spec, NULL, arg1, strlen(arg1),
			    0, 0, NULL, 0) < 0)
				return (0);
			break;
		case BANS_OPER_NMATCH:
			if (arg1 != NULL &&
			    pcre_exec(bt.arg2_spec, NULL, arg1, strlen(arg1),
			    0, 0, NULL, 0) >= 0)
				return (0);
			break;
		default:
			WRONG("Wrong BAN_OPER code");
		}
	}
	return (1);
}

/*--------------------------------------------------------------------
 * Check an object against all applicable bans
 *
 * Return:
 *	-1 not all bans checked, but none of the checked matched
 *		Only if !has_req
 *	0 No bans matched, object moved to ban_start.
 *	1 Ban matched, object removed from ban list.
 */

static int
ban_check_object(struct object *o, struct vsl_log *vsl,
    const struct http *req_http)
{
	struct ban *b;
	struct objcore *oc;
	struct ban * volatile b0;
	unsigned tests;

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	CHECK_OBJ_NOTNULL(req_http, HTTP_MAGIC);
	oc = o->objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(oc->ban, BAN_MAGIC);

	/* First do an optimistic unlocked check */
	b0 = ban_start;
	CHECK_OBJ_NOTNULL(b0, BAN_MAGIC);

	if (b0 == oc->ban)
		return (0);

	/* If that fails, make a safe check */
	Lck_Lock(&ban_mtx);
	b0 = ban_start;
	Lck_Unlock(&ban_mtx);

	if (b0 == oc->ban)
		return (0);

	/*
	 * This loop is safe without locks, because we know we hold
	 * a refcount on a ban somewhere in the list and we do not
	 * inspect the list past that ban.
	 */
	tests = 0;
	for (b = b0; b != oc->ban; b = VTAILQ_NEXT(b, list)) {
		CHECK_OBJ_NOTNULL(b, BAN_MAGIC);
		if (b->flags & BANS_FLAG_COMPLETED)
			continue;
		if (ban_evaluate(b->spec, o->http, req_http, &tests))
			break;
	}

	Lck_Lock(&ban_mtx);
	VSC_C_main->bans_tested++;
	VSC_C_main->bans_tests_tested += tests;

	oc->ban->refcount--;
	VTAILQ_REMOVE(&oc->ban->objcore, oc, ban_list);
	if (b == oc->ban) {	/* not banned */
		VTAILQ_INSERT_TAIL(&b0->objcore, oc, ban_list);
		b0->refcount++;
	}

	Lck_Unlock(&ban_mtx);

	if (b == oc->ban) {	/* not banned */
		oc->ban = b0;
		oc_updatemeta(oc);
		return (0);
	} else {
		oc->ban = NULL;
		VSLb(vsl, SLT_ExpBan, "%u banned lookup", o->vxid);
		VSC_C_main->bans_obj_killed++;
		EXP_Rearm(o, o->exp.t_origin, 0, 0, 0);	// XXX fake now
		return (1);
	}
}

int
BAN_CheckObject(struct object *o, struct req *req)
{

	return (ban_check_object(o, req->vsl, req->http) > 0);
}

static void
ban_cleantail(void)
{
	struct ban *b;

	do {
		Lck_Lock(&ban_mtx);
		b = VTAILQ_LAST(&ban_head, banhead_s);
		if (b != VTAILQ_FIRST(&ban_head) && b->refcount == 0) {
			if (b->flags & BANS_FLAG_COMPLETED)
				VSC_C_main->bans_completed--;
			if (b->flags & BANS_FLAG_OBJ)
				VSC_C_main->bans_obj--;
			if (b->flags & BANS_FLAG_REQ)
				VSC_C_main->bans_req--;
			VSC_C_main->bans--;
			VSC_C_main->bans_deleted++;
			VTAILQ_REMOVE(&ban_head, b, list);
			VSC_C_main->bans_persisted_fragmentation +=
			    ban_len(b->spec);
			ban_info(BI_DROP, b->spec, ban_len(b->spec));
		} else {
			b = NULL;
		}
		Lck_Unlock(&ban_mtx);
		if (b != NULL)
			BAN_Free(b);
	} while (b != NULL);
}

/*--------------------------------------------------------------------
 * Our task here is somewhat tricky:  The canonical locking order is
 * objhead->mtx first, then ban_mtx, because that is the order which
 * makes most sense in HSH_Lookup(), but we come the other way.
 * We optimistically try to get them the other way, and get out of
 * the way if that fails, and retry again later.
 */

static struct objcore *
ban_lurker_getfirst(struct vsl_log *vsl, struct ban *bt)
{
	struct objhead *oh;
	struct objcore *oc;

	while (1) {
		Lck_Lock(&ban_mtx);
		oc = VTAILQ_FIRST(&bt->objcore);
		CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
		if (oc == &oc_marker) {
			VTAILQ_REMOVE(&bt->objcore, oc, ban_list);
			Lck_Unlock(&ban_mtx);
			return (NULL);
		}
		oh = oc->objhead;
		CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
		if (!Lck_Trylock(&oh->mtx)) {
			if (oc->refcnt == 0) {
				Lck_Unlock(&oh->mtx);
			} else {
				/*
				 * We got the lock, and the oc is not being
				 * dismantled under our feet, run with it...
				 */
				AZ(oc->flags & OC_F_BUSY);
				oc->refcnt += 1;
				VTAILQ_REMOVE(&bt->objcore, oc, ban_list);
				VTAILQ_INSERT_TAIL(&bt->objcore, oc, ban_list);
				Lck_Unlock(&oh->mtx);
				Lck_Unlock(&ban_mtx);
				break;
			}
		}

		/* Try again, later */
		Lck_Unlock(&ban_mtx);
		VSC_C_main->bans_lurker_contention++;
		VSL_Flush(vsl, 0);
		VTIM_sleep(cache_param->ban_lurker_sleep);
	}
	return (oc);
}

static void
ban_lurker_test_ban(struct worker *wrk, struct vsl_log *vsl, struct ban *bt,
    struct banhead_s *obans)
{
	struct ban *bl, *bln;
	struct objcore *oc;
	struct object *o;
	unsigned tests;
	int i;

	/*
	 * First see if there is anything to do, and if so, insert marker
	 */
	Lck_Lock(&ban_mtx);
	oc = VTAILQ_FIRST(&bt->objcore);
	if (oc != NULL)
		VTAILQ_INSERT_TAIL(&bt->objcore, &oc_marker, ban_list);
	Lck_Unlock(&ban_mtx);
	if (oc == NULL)
		return;

	while (1) {
		if (++ban_batch > cache_param->ban_lurker_batch) {
			VTIM_sleep(cache_param->ban_lurker_sleep);
			ban_batch = 0;
		}
		oc = ban_lurker_getfirst(vsl, bt);
		if (oc == NULL)
			return;
		o = oc_getobj(&wrk->stats, oc);
		CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
		i = 0;
		VTAILQ_FOREACH_REVERSE_SAFE(bl, obans, banhead_s, l_list, bln) {
			if (bl->flags & BANS_FLAG_COMPLETED) {
				/* Ban was overtaken by new (dup) ban */
				VTAILQ_REMOVE(obans, bl, l_list);
				continue;
			}
			tests = 0;
			i = ban_evaluate(bl->spec, o->http, NULL, &tests);
			VSC_C_main->bans_lurker_tested++;
			VSC_C_main->bans_lurker_tests_tested += tests;
			if (i)
				break;
		}
		if (i) {
			VSLb(vsl, SLT_ExpBan, "%u banned by lurker", o->vxid);
			EXP_Rearm(o, o->exp.t_origin, 0, 0, 0);	// XXX fake now
			VSC_C_main->bans_lurker_obj_killed++;
		}
		(void)HSH_DerefObjCore(&wrk->stats, &oc);
	}
}

/*--------------------------------------------------------------------
 * Ban lurker thread
 */

static int
ban_lurker_work(struct worker *wrk, struct vsl_log *vsl)
{
	struct ban *b, *bt;
	struct banhead_s obans;
	double d;
	int i;

	/* Make a list of the bans we can do something about */
	VTAILQ_INIT(&obans);
	Lck_Lock(&ban_mtx);
	b = ban_start;
	Lck_Unlock(&ban_mtx);
	i = 0;
	d = VTIM_real() - cache_param->ban_lurker_age;
	while (b != NULL) {
		if (b->flags & BANS_FLAG_COMPLETED) {
			;
		} else if (b->flags & BANS_FLAG_REQ) {
			;
		} else if (b == VTAILQ_LAST(&ban_head, banhead_s)) {
			;
		} else if (ban_time(b->spec) > d) {
			;
		} else {
			VTAILQ_INSERT_TAIL(&obans, b, l_list);
			i++;
		}
		b = VTAILQ_NEXT(b, list);
	}
	if (DO_DEBUG(DBG_LURKER))
		VSLb(vsl, SLT_Debug, "lurker: %d actionable bans", i);
	if (i == 0)
		return (0);

	/* Go though all the bans to test the objects */
	VTAILQ_FOREACH_REVERSE(bt, &ban_head, banhead_s, list) {
		if (bt == VTAILQ_LAST(&obans, banhead_s)) {
			if (DO_DEBUG(DBG_LURKER))
				VSLb(vsl, SLT_Debug,
				    "Lurk bt completed %p", bt);
			Lck_Lock(&ban_mtx);
			/* We can be raced by a new ban */
			if (!(bt->flags & BANS_FLAG_COMPLETED))
				ban_mark_completed(bt);
			Lck_Unlock(&ban_mtx);
			VTAILQ_REMOVE(&obans, bt, l_list);
			if (VTAILQ_EMPTY(&obans))
				break;
		}
		if (DO_DEBUG(DBG_LURKER))
			VSLb(vsl, SLT_Debug, "Lurk bt %p", bt);
		ban_lurker_test_ban(wrk, vsl, bt, &obans);
		if (VTAILQ_EMPTY(&obans))
			break;
	}
	return (1);
}

static void * __match_proto__(bgthread_t)
ban_lurker(struct worker *wrk, void *priv)
{
	struct vsl_log vsl;
	volatile double d;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AZ(priv);

	VSL_Setup(&vsl, NULL, 0);

	while (!ban_shutdown) {
		d = cache_param->ban_lurker_sleep;
		if (d <= 0.0 || !ban_lurker_work(wrk, &vsl))
			d = 0.609;	// Random, non-magic
		ban_cleantail();
		VTIM_sleep(d);
	}
	pthread_exit(0);
	NEEDLESS_RETURN(NULL);
}

/*--------------------------------------------------------------------
 * CLI functions to add bans
 */

static void
ccf_ban(struct cli *cli, const char * const *av, void *priv)
{
	int narg, i;
	struct ban *b;
	char *p;

	(void)priv;

	/* First do some cheap checks on the arguments */
	for (narg = 0; av[narg + 2] != NULL; narg++)
		continue;
	if ((narg % 4) != 3) {
		VCLI_Out(cli, "Wrong number of arguments");
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	}
	for (i = 3; i < narg; i += 4) {
		if (strcmp(av[i + 2], "&&")) {
			VCLI_Out(cli, "Found \"%s\" expected &&", av[i + 2]);
			VCLI_SetResult(cli, CLIS_PARAM);
			return;
		}
	}

	b = BAN_New();
	if (b == NULL) {
		VCLI_Out(cli, "Out of Memory");
		VCLI_SetResult(cli, CLIS_CANT);
		return;
	}
	for (i = 0; i < narg; i += 4)
		if (BAN_AddTest(b, av[i + 2], av[i + 3], av[i + 4]))
			break;
	p = BAN_Insert(b);
	if (p != NULL) {
		VCLI_Out(cli, "%s", p);
		BAN_Free_Errormsg(p);
		VCLI_SetResult(cli, CLIS_PARAM);
	}
}

static void
ban_render(struct cli *cli, const uint8_t *bs)
{
	struct ban_test bt;
	const uint8_t *be;

	be = bs + ban_len(bs);
	bs += BANS_HEAD_LEN;
	while (bs < be) {
		ban_iter(&bs, &bt);
		switch (bt.arg1) {
		case BANS_ARG_URL:
			VCLI_Out(cli, "req.url");
			break;
		case BANS_ARG_REQHTTP:
			VCLI_Out(cli, "req.http.%.*s",
			    bt.arg1_spec[0] - 1, bt.arg1_spec + 1);
			break;
		case BANS_ARG_OBJHTTP:
			VCLI_Out(cli, "obj.http.%.*s",
			    bt.arg1_spec[0] - 1, bt.arg1_spec + 1);
			break;
		case BANS_ARG_OBJSTATUS:
			VCLI_Out(cli, "obj.status");
			break;
		default:
			WRONG("Wrong BANS_ARG");
		}
		switch (bt.oper) {
		case BANS_OPER_EQ:	VCLI_Out(cli, " == "); break;
		case BANS_OPER_NEQ:	VCLI_Out(cli, " != "); break;
		case BANS_OPER_MATCH:	VCLI_Out(cli, " ~ "); break;
		case BANS_OPER_NMATCH:	VCLI_Out(cli, " !~ "); break;
		default:
			WRONG("Wrong BANS_OPER");
		}
		VCLI_Out(cli, "%s", bt.arg2);
		if (bs < be)
			VCLI_Out(cli, " && ");
	}
}

static void
ccf_ban_list(struct cli *cli, const char * const *av, void *priv)
{
	struct ban *b, *bl;

	(void)av;
	(void)priv;

	/* Get a reference so we are safe to traverse the list */
	bl = BAN_TailRef();

	VCLI_Out(cli, "Present bans:\n");
	VTAILQ_FOREACH(b, &ban_head, list) {
		VCLI_Out(cli, "%10.6f %5u %s", ban_time(b->spec),
		    bl == b ? b->refcount - 1 : b->refcount,
		    b->flags & BANS_FLAG_COMPLETED ? "C" : " ");
		if (DO_DEBUG(DBG_LURKER)) {
			VCLI_Out(cli, "%s%s%s %p ",
			    b->flags & BANS_FLAG_REQ ? "R" : "-",
			    b->flags & BANS_FLAG_OBJ ? "O" : "-",
			    b->flags & BANS_FLAG_ERROR ? "E" : "-",
			    b);
		}
		VCLI_Out(cli, "  ");
		ban_render(cli, b->spec);
		VCLI_Out(cli, "\n");
		if (VCLI_Overflow(cli))
			break;
		if (DO_DEBUG(DBG_LURKER)) {
			Lck_Lock(&ban_mtx);
			struct objcore *oc;
			VTAILQ_FOREACH(oc, &b->objcore, ban_list)
				VCLI_Out(cli, "  oc = %p\n", oc);
			Lck_Unlock(&ban_mtx);
		}
	}

	BAN_TailDeref(&bl);
}

static struct cli_proto ban_cmds[] = {
	{ CLI_BAN,				"", ccf_ban },
	{ CLI_BAN_LIST,				"", ccf_ban_list },
	{ NULL }
};

void
BAN_Init(void)
{

	Lck_New(&ban_mtx, lck_ban);
	CLI_AddFuncs(ban_cmds);

	ban_magic = BAN_New();
	AN(ban_magic);
	AZ(BAN_Insert(ban_magic));
	Lck_Lock(&ban_mtx);
	ban_mark_completed(ban_magic);
	Lck_Unlock(&ban_mtx);
}

/*--------------------------------------------------------------------
 * Shutdown of the ban system.
 *
 * When this function returns, no new bans will be accepted, and no
 * bans will be dropped (ban lurker thread stopped), so that no
 * STV_BanInfo calls will be executed.
 */

void
BAN_Shutdown(void)
{
	void *status;

	Lck_Lock(&ban_mtx);
	ban_shutdown = 1;
	Lck_Unlock(&ban_mtx);

	AZ(pthread_join(ban_thread, &status));
	AZ(status);

	Lck_Lock(&ban_mtx);
	/* Export the ban list to compact it */
	ban_export();
	Lck_Unlock(&ban_mtx);
}
