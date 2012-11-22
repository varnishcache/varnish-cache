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
 *
 * Ban processing
 *
 * A ban consists of a number of conditions (or tests), all of which must be
 * satisfied.  Here are some potential bans we could support:
 *
 *	req.url == "/foo"
 *	req.url ~ ".iso" && obj.size > 10MB
 *	req.http.host ~ "web1.com" && obj.set-cookie ~ "USER=29293"
 *
 * We make the "&&" mandatory from the start, leaving the syntax space
 * for latter handling of "||" as well.
 *
 * Bans are compiled into bytestrings as follows:
 *	8 bytes	- double: timestamp		XXX: Byteorder ?
 *	4 bytes - be32: length
 *	1 byte - flags: 0x01: BAN_F_REQ
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

#include <pcre.h>
#include <stdio.h>

#include "cache.h"
#include "storage/storage.h"

#include "hash/hash_slinger.h"
#include "vcli.h"
#include "vcli_priv.h"
#include "vend.h"
#include "vmb.h"
#include "vtim.h"

struct ban {
	unsigned		magic;
#define BAN_MAGIC		0x700b08ea
	VTAILQ_ENTRY(ban)	list;
	int			refcount;
	unsigned		flags;
#define BAN_F_GONE		(1 << 0)
#define BAN_F_REQ		(1 << 2)
#define BAN_F_LURK		(3 << 6)	/* ban-lurker-color */
	VTAILQ_HEAD(,objcore)	objcore;
	struct vsb		*vsb;
	uint8_t			*spec;
};

#define LURK_SHIFT 6

struct ban_test {
	uint8_t			arg1;
	const char		*arg1_spec;
	uint8_t			oper;
	const char		*arg2;
	const void		*arg2_spec;
};

static VTAILQ_HEAD(banhead_s,ban) ban_head = VTAILQ_HEAD_INITIALIZER(ban_head);
static struct lock ban_mtx;
static struct ban *ban_magic;
static pthread_t ban_thread;
static struct ban * volatile ban_start;
static bgthread_t ban_lurker;

/*--------------------------------------------------------------------
 * BAN string defines & magic markers
 */

#define BANS_TIMESTAMP		0
#define BANS_LENGTH		8
#define BANS_FLAGS		12
#define BANS_HEAD_LEN		13
#define BANS_FLAG_REQ		0x01
#define BANS_FLAG_GONE		0x02

#define BANS_OPER_EQ		0x10
#define BANS_OPER_NEQ		0x11
#define BANS_OPER_MATCH		0x12
#define BANS_OPER_NMATCH	0x13

#define BANS_ARG_URL		0x18
#define BANS_ARG_REQHTTP	0x19
#define BANS_ARG_OBJHTTP	0x1a
#define BANS_ARG_OBJSTATUS	0x1b

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
ban_mark_gone(struct ban *b)
{

	CHECK_OBJ_NOTNULL(b, BAN_MAGIC);
	b->flags |= BAN_F_GONE;
	b->spec[BANS_FLAGS] |= BANS_FLAG_GONE;
	VWMB();
	vbe32enc(b->spec + BANS_LENGTH, 0);
	VSC_C_main->bans_gone++;
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
ban_parse_regexp(struct cli *cli, const struct ban *b, const char *a3)
{
	const char *error;
	int erroroffset, rc;
	size_t sz;
	pcre *re;

	re = pcre_compile(a3, 0, &error, &erroroffset, NULL);
	if (re == NULL) {
		VSL(SLT_Debug, 0, "REGEX: <%s>", error);
		VCLI_Out(cli, "%s", error);
		VCLI_SetResult(cli, CLIS_PARAM);
		return (-1);
	}
	rc = pcre_fullinfo(re, NULL, PCRE_INFO_SIZE, &sz);
	AZ(rc);
	ban_add_lump(b, re, sz);
	return (0);
}

/*--------------------------------------------------------------------
 * Add a (and'ed) test-condition to a ban
 */

int
BAN_AddTest(struct cli *cli, struct ban *b, const char *a1, const char *a2,
    const char *a3)
{
	const struct pvar *pv;
	int i;

	CHECK_OBJ_NOTNULL(b, BAN_MAGIC);

	for (pv = pvars; pv->name != NULL; pv++)
		if (!strncmp(a1, pv->name, strlen(pv->name)))
			break;
	if (pv->name == NULL) {
		VCLI_Out(cli, "unknown or unsupported field \"%s\"", a1);
		VCLI_SetResult(cli, CLIS_PARAM);
		return (-1);
	}

	if (pv->flag & PVAR_REQ)
		b->flags |= BAN_F_REQ;

	VSB_putc(b->vsb, pv->tag);
	if (pv->flag & PVAR_HTTP)
		ban_parse_http(b, a1 + strlen(pv->name));

	ban_add_lump(b, a3, strlen(a3) + 1);
	if (!strcmp(a2, "~")) {
		VSB_putc(b->vsb, BANS_OPER_MATCH);
		i = ban_parse_regexp(cli, b, a3);
		if (i)
			return (i);
	} else if (!strcmp(a2, "!~")) {
		VSB_putc(b->vsb, BANS_OPER_NMATCH);
		i = ban_parse_regexp(cli, b, a3);
		if (i)
			return (i);
	} else if (!strcmp(a2, "==")) {
		VSB_putc(b->vsb, BANS_OPER_EQ);
	} else if (!strcmp(a2, "!=")) {
		VSB_putc(b->vsb, BANS_OPER_NEQ);
	} else {
		VCLI_Out(cli,
		    "expected conditional (~, !~, == or !=) got \"%s\"", a2);
		VCLI_SetResult(cli, CLIS_PARAM);
		return (-1);
	}
	return (0);
}

/*--------------------------------------------------------------------
 * We maintain ban_start as a pointer to the first element of the list
 * as a separate variable from the VTAILQ, to avoid depending on the
 * internals of the VTAILQ macros.  We tacitly assume that a pointer
 * write is always atomic in doing so.
 */

void
BAN_Insert(struct ban *b)
{
	struct ban  *bi, *be;
	ssize_t ln;
	double t0;

	CHECK_OBJ_NOTNULL(b, BAN_MAGIC);

	AZ(VSB_finish(b->vsb));
	ln = VSB_len(b->vsb);
	assert(ln >= 0);

	b->spec = malloc(ln + BANS_HEAD_LEN);
	XXXAN(b->spec);

	t0 = VTIM_real();
	memcpy(b->spec + BANS_TIMESTAMP, &t0, sizeof t0);
	b->spec[BANS_FLAGS] = (b->flags & BAN_F_REQ) ? BANS_FLAG_REQ : 0;
	memcpy(b->spec + BANS_HEAD_LEN, VSB_data(b->vsb), ln);
	ln += BANS_HEAD_LEN;
	vbe32enc(b->spec + BANS_LENGTH, ln);

	VSB_delete(b->vsb);
	b->vsb = NULL;

	Lck_Lock(&ban_mtx);
	VTAILQ_INSERT_HEAD(&ban_head, b, list);
	ban_start = b;
	VSC_C_main->bans++;
	VSC_C_main->bans_added++;
	if (b->flags & BAN_F_REQ)
		VSC_C_main->bans_req++;

	be = VTAILQ_LAST(&ban_head, banhead_s);
	if (cache_param->ban_dups && be != b)
		be->refcount++;
	else
		be = NULL;

	STV_BanInfo(BI_NEW, b->spec, ln);	/* Notify stevedores */
	Lck_Unlock(&ban_mtx);

	if (be == NULL)
		return;

	/* Hunt down duplicates, and mark them as gone */
	bi = b;
	Lck_Lock(&ban_mtx);
	while(bi != be) {
		bi = VTAILQ_NEXT(bi, list);
		if (bi->flags & BAN_F_GONE)
			continue;
		if (!ban_equal(b->spec, bi->spec))
			continue;
		ban_mark_gone(bi);
		VSC_C_main->bans_dups++;
	}
	be->refcount--;
	Lck_Unlock(&ban_mtx);
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
 * Put a skeleton ban in the list, unless there is an identical,
 * time & condition, ban already in place.
 *
 * If a newer ban has same condition, mark the new ban GONE.
 * mark any older bans, with the same condition, GONE as well.
 */

void
BAN_Reload(const uint8_t *ban, unsigned len)
{
	struct ban *b, *b2;
	int gone = 0;
	double t0, t1, t2 = 9e99;

	ASSERT_CLI();

	t0 = ban_time(ban);
	assert(len == ban_len(ban));

	Lck_Lock(&ban_mtx);

	VTAILQ_FOREACH(b, &ban_head, list) {
		t1 = ban_time(b->spec);
		assert(t1 < t2);
		t2 = t1;
		if (t1 == t0) {
			Lck_Unlock(&ban_mtx);
			return;
		}
		if (t1 < t0)
			break;
		if (ban_equal(b->spec, ban)) {
			gone |= BAN_F_GONE;
			VSC_C_main->bans_dups++;
			VSC_C_main->bans_gone++;
		}
	}

	VSC_C_main->bans++;
	VSC_C_main->bans_added++;

	b2 = ban_alloc();
	AN(b2);
	b2->spec = malloc(len);
	AN(b2->spec);
	memcpy(b2->spec, ban, len);
	b2->flags |= gone;
	if (ban[BANS_FLAGS] & BANS_FLAG_REQ) {
		VSC_C_main->bans_req++;
		b2->flags |= BAN_F_REQ;
	}
	if (b == NULL)
		VTAILQ_INSERT_TAIL(&ban_head, b2, list);
	else
		VTAILQ_INSERT_BEFORE(b, b2, list);

	/* Hunt down older duplicates */
	for (b = VTAILQ_NEXT(b2, list); b != NULL; b = VTAILQ_NEXT(b, list)) {
		if (b->flags & BAN_F_GONE)
			continue;
		if (ban_equal(b->spec, ban)) {
			ban_mark_gone(b);
			VSC_C_main->bans_dups++;
		}
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

	/* Notify stevedores */
	STV_BanInfo(BI_NEW, ban_magic->spec, ban_len(ban_magic->spec));

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
			INCOMPL();
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
			INCOMPL();
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
	unsigned tests, skipped;

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	CHECK_OBJ_ORNULL(req_http, HTTP_MAGIC);
	oc = o->objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(oc->ban, BAN_MAGIC);

	b0 = ban_start;
	CHECK_OBJ_NOTNULL(b0, BAN_MAGIC);

	if (b0 == oc->ban)
		return (0);

	/*
	 * This loop is safe without locks, because we know we hold
	 * a refcount on a ban somewhere in the list and we do not
	 * inspect the list past that ban.
	 */
	tests = 0;
	skipped = 0;
	for (b = b0; b != oc->ban; b = VTAILQ_NEXT(b, list)) {
		CHECK_OBJ_NOTNULL(b, BAN_MAGIC);
		if (b->flags & BAN_F_GONE)
			continue;
		if ((b->flags & BAN_F_LURK) &&
		    (b->flags & BAN_F_LURK) == (oc->flags & OC_F_LURK)) {
			AZ(b->flags & BAN_F_REQ);
			/* Lurker already tested this */
			continue;
		}
		if (req_http == NULL && (b->flags & BAN_F_REQ)) {
			/*
			 * We cannot test this one, but there might
			 * be other bans that match, so we soldier on
			 */
			skipped++;
		} else if (ban_evaluate(b->spec, o->http, req_http, &tests))
			break;
	}

	Lck_Lock(&ban_mtx);
	VSC_C_main->bans_tested++;
	VSC_C_main->bans_tests_tested += tests;

	if (b == oc->ban && skipped > 0) {
		AZ(req_http);
		Lck_Unlock(&ban_mtx);
		/*
		 * Not banned, but some tests were skipped, so we cannot know
		 * for certain that it cannot be, so we just have to give up.
		 */
		return (-1);
	}

	oc->ban->refcount--;
	VTAILQ_REMOVE(&oc->ban->objcore, oc, ban_list);
	if (b == oc->ban) {	/* not banned */
		b->flags &= ~BAN_F_LURK;
		VTAILQ_INSERT_TAIL(&b0->objcore, oc, ban_list);
		b0->refcount++;
	}
	Lck_Unlock(&ban_mtx);

	if (b == oc->ban) {	/* not banned */
		oc->ban = b0;
		oc_updatemeta(oc);
		return (0);
	} else {
		EXP_Clr(&o->exp);
		oc->ban = NULL;
		oc_updatemeta(oc);
		/* BAN also changed, but that is not important any more */
		/* XXX: no req in lurker */
		VSLb(vsl, SLT_ExpBan, "%u was banned", o->vxid);
		EXP_Rearm(o);
		return (1);
	}
}

int
BAN_CheckObject(struct object *o, struct req *req)
{

	return (ban_check_object(o, req->vsl, req->http) > 0);
}

static struct ban *
ban_CheckLast(void)
{
	struct ban *b;

	Lck_AssertHeld(&ban_mtx);
	b = VTAILQ_LAST(&ban_head, banhead_s);
	if (b != VTAILQ_FIRST(&ban_head) && b->refcount == 0) {
		if (b->flags & BAN_F_GONE)
			VSC_C_main->bans_gone--;
		if (b->flags & BAN_F_REQ)
			VSC_C_main->bans_req--;
		VSC_C_main->bans--;
		VSC_C_main->bans_deleted++;
		VTAILQ_REMOVE(&ban_head, b, list);
	} else {
		b = NULL;
	}
	return (b);
}

/*--------------------------------------------------------------------
 * Ban lurker thread
 */

static int
ban_lurker_work(struct worker *wrk, struct vsl_log *vsl)
{
	struct ban *b, *b0, *b2;
	struct objhead *oh;
	struct objcore *oc, *oc2;
	struct object *o;
	static unsigned pass = 1 << LURK_SHIFT;
	int i;

	AN(pass & BAN_F_LURK);
	AZ(pass & ~BAN_F_LURK);

	/* First route the last ban(s) */
	do {
		Lck_Lock(&ban_mtx);
		b2 = ban_CheckLast();
		if (b2 != NULL)
			/* Notify stevedores */
			STV_BanInfo(BI_DROP, b2->spec, ban_len(b2->spec));
		Lck_Unlock(&ban_mtx);
		if (b2 != NULL)
			BAN_Free(b2);
	} while (b2 != NULL);

	/*
	 * Find out if we have any bans we can do something about
	 * If we find any, tag them with our pass number.
	 */
	i = 0;
	b0 = NULL;
	VTAILQ_FOREACH(b, &ban_head, list) {
		if (b->flags & BAN_F_GONE)
			continue;
		if (b->flags & BAN_F_REQ)
			continue;
		if (b == VTAILQ_LAST(&ban_head, banhead_s))
			continue;
		if (b0 == NULL)
			b0 = b;
		i++;
		b->flags &= ~BAN_F_LURK;
		b->flags |= pass;
	}
	if (DO_DEBUG(DBG_LURKER))
		VSLb(vsl, SLT_Debug, "lurker: %d actionable bans", i);
	if (i == 0)
		return (0);

	VTAILQ_FOREACH_REVERSE(b, &ban_head, banhead_s, list) {
		if (DO_DEBUG(DBG_LURKER))
			VSLb(vsl, SLT_Debug, "lurker doing %f %d",
			    ban_time(b->spec), b->refcount);
		while (1) {
			Lck_Lock(&ban_mtx);
			oc = VTAILQ_FIRST(&b->objcore);
			if (oc == NULL)
				break;
			CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
			if (DO_DEBUG(DBG_LURKER))
				VSLb(vsl, SLT_Debug, "test: %p %u %u",
				    oc, oc->flags & OC_F_LURK, pass);
			if ((oc->flags & OC_F_LURK) == pass)
				break;
			oh = oc->objhead;
			CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
			if (Lck_Trylock(&oh->mtx)) {
				Lck_Unlock(&ban_mtx);
				VSL_Flush(vsl, 0);
				VTIM_sleep(cache_param->ban_lurker_sleep);
				continue;
			}
			/*
			 * See if the objcore is still on the objhead since
			 * we race against HSH_Deref() which comes in the
			 * opposite locking order.
			 */
			VTAILQ_FOREACH(oc2, &oh->objcs, list)
				if (oc == oc2)
					break;
			if (oc2 == NULL) {
				Lck_Unlock(&oh->mtx);
				Lck_Unlock(&ban_mtx);
				VTIM_sleep(cache_param->ban_lurker_sleep);
				continue;
			}
			/*
			 * If the object is busy, we can't touch
			 * it. Defer it to a later run.
			 */
			if (oc->flags & OC_F_BUSY) {
				oc->flags |= pass;
				VTAILQ_REMOVE(&b->objcore, oc, ban_list);
				VTAILQ_INSERT_TAIL(&b->objcore, oc, ban_list);
				Lck_Unlock(&oh->mtx);
				Lck_Unlock(&ban_mtx);
				continue;
			}
			/*
			 * Grab a reference to the OC and we can let go of
			 * the BAN mutex
			 */
			AN(oc->refcnt);
			oc->refcnt++;
			oc->flags &= ~OC_F_LURK;
			Lck_Unlock(&ban_mtx);
			/*
			 * Get the object and check it against all relevant bans
			 */
			o = oc_getobj(&wrk->stats, oc);
			i = ban_check_object(o, vsl, NULL);
			if (DO_DEBUG(DBG_LURKER))
				VSLb(vsl, SLT_Debug, "lurker got: %p %d",
				    oc, i);
			if (i == -1) {
				/* Not banned, not moved */
				oc->flags |= pass;
				Lck_Lock(&ban_mtx);
				VTAILQ_REMOVE(&b->objcore, oc, ban_list);
				VTAILQ_INSERT_TAIL(&b->objcore, oc, ban_list);
				Lck_Unlock(&ban_mtx);
			}
			Lck_Unlock(&oh->mtx);
			if (DO_DEBUG(DBG_LURKER))
				VSLb(vsl, SLT_Debug, "lurker done: %p %u %u",
				    oc, oc->flags & OC_F_LURK, pass);
			(void)HSH_Deref(&wrk->stats, NULL, &o);
			VTIM_sleep(cache_param->ban_lurker_sleep);
		}
		Lck_AssertHeld(&ban_mtx);
		if (!(b->flags & BAN_F_REQ)) {
			if (!(b->flags & BAN_F_GONE))
				ban_mark_gone(b);
			if (DO_DEBUG(DBG_LURKER))
				VSLb(vsl, SLT_Debug, "lurker BAN %f now gone",
				    ban_time(b->spec));
		}
		Lck_Unlock(&ban_mtx);
		VTIM_sleep(cache_param->ban_lurker_sleep);
		if (b == b0)
			break;
	}
	pass += (1 << LURK_SHIFT);
	pass &= BAN_F_LURK;
	if (pass == 0)
		pass += (1 << LURK_SHIFT);
	return (1);
}

static void * __match_proto__(bgthread_t)
ban_lurker(struct worker *wrk, void *priv)
{
	struct ban *bf;
	struct vsl_log vsl;

	int i = 0;
	VSL_Setup(&vsl, NULL, 0);

	(void)priv;
	while (1) {

		while (cache_param->ban_lurker_sleep == 0.0) {
			/*
			 * Ban-lurker is disabled:
			 * Clean the last ban, if possible, and sleep
			 */
			Lck_Lock(&ban_mtx);
			bf = ban_CheckLast();
			if (bf != NULL)
				/* Notify stevedores */
				STV_BanInfo(BI_DROP, bf->spec,
					    ban_len(bf->spec));
			Lck_Unlock(&ban_mtx);
			if (bf != NULL)
				BAN_Free(bf);
			else
				VTIM_sleep(1.0);
		}

		i = ban_lurker_work(wrk, &vsl);
		VSL_Flush(&vsl, 0);
		WRK_SumStat(wrk);
		if (i) {
			VTIM_sleep(cache_param->ban_lurker_sleep);
		} else {
			VTIM_sleep(1.0);
		}
	}
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
		if (BAN_AddTest(cli, b, av[i + 2], av[i + 3], av[i + 4])) {
			BAN_Free(b);
			return;
		}
	BAN_Insert(b);
}

static void
ccf_ban_url(struct cli *cli, const char * const *av, void *priv)
{
	const char *aav[6];

	(void)priv;
	aav[0] = NULL;
	aav[1] = "ban";
	aav[2] = "req.url";
	aav[3] = "~";
	aav[4] = av[2];
	aav[5] = NULL;
	ccf_ban(cli, aav, priv);
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
		default:
			INCOMPL();
		}
		switch (bt.oper) {
		case BANS_OPER_EQ:	VCLI_Out(cli, " == "); break;
		case BANS_OPER_NEQ:	VCLI_Out(cli, " != "); break;
		case BANS_OPER_MATCH:	VCLI_Out(cli, " ~ "); break;
		case BANS_OPER_NMATCH:	VCLI_Out(cli, " !~ "); break;
		default:
			INCOMPL();
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
		VCLI_Out(cli, "%10.6f %5u%s\t", ban_time(b->spec),
		    bl == b ? b->refcount - 1 : b->refcount,
		    b->flags & BAN_F_GONE ? "G" : " ");
		ban_render(cli, b->spec);
		VCLI_Out(cli, "\n");
		if (VCLI_Overflow(cli))
			break;
		if (DO_DEBUG(DBG_LURKER)) {
			Lck_Lock(&ban_mtx);
			struct objcore *oc;
			VTAILQ_FOREACH(oc, &b->objcore, ban_list)
				VCLI_Out(cli, "     %p\n", oc);
			Lck_Unlock(&ban_mtx);
		}
	}

	BAN_TailDeref(&bl);
}

static struct cli_proto ban_cmds[] = {
	{ CLI_BAN_URL,				"", ccf_ban_url },
	{ CLI_BAN,				"", ccf_ban },
	{ CLI_BAN_LIST,				"", ccf_ban_list },
	{ NULL }
};

void
BAN_Init(void)
{

	Lck_New(&ban_mtx, lck_ban);
	CLI_AddFuncs(ban_cmds);
	assert(BAN_F_LURK == OC_F_LURK);
	AN((1 << LURK_SHIFT) & BAN_F_LURK);
	AN((2 << LURK_SHIFT) & BAN_F_LURK);

	ban_magic = BAN_New();
	AN(ban_magic);
	ban_magic->flags |= BAN_F_GONE;
	VSC_C_main->bans_gone++;
	BAN_Insert(ban_magic);
}
