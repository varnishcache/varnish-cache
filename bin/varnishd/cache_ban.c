/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Linpro AS
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
 */

#include "config.h"

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pcre.h>

#include "cli.h"
#include "vend.h"
#include "cli_priv.h"
#include "cache.h"
#include "hash_slinger.h"

struct ban {
	unsigned		magic;
#define BAN_MAGIC		0x700b08ea
	VTAILQ_ENTRY(ban)	list;
	unsigned		refcount;
	unsigned		flags;
#define BAN_F_GONE		(1 << 0)
#define BAN_F_REQ		(1 << 2)
	VTAILQ_HEAD(,objcore)	objcore;
	struct vsb		*vsb;
	uint8_t			*spec;
};

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

/*--------------------------------------------------------------------
 * BAN string magic markers
 */

#define	BAN_OPER_EQ	0x10
#define	BAN_OPER_NEQ	0x11
#define	BAN_OPER_MATCH	0x12
#define	BAN_OPER_NMATCH	0x13

#define BAN_ARG_URL	0x18
#define BAN_ARG_REQHTTP	0x19
#define BAN_ARG_OBJHTTP	0x1a

/*--------------------------------------------------------------------
 * Variables we can purge on
 */

static const struct pvar {
	const char		*name;
	unsigned		flag;
	uint8_t			tag;
} pvars[] = {
#define PVAR(a, b, c)	{ (a), (b), (c) },
#include "ban_vars.h"
#undef PVAR
	{ 0, 0, 0}
};

/*--------------------------------------------------------------------
 * Storage handling of bans
 */

struct ban *
BAN_New(void)
{
	struct ban *b;

	ALLOC_OBJ(b, BAN_MAGIC);
	if (b == NULL)
		return (b);
	b->vsb = vsb_new_auto();
	if (b->vsb == NULL) {
		FREE_OBJ(b);
		return (NULL);
	}
	VTAILQ_INIT(&b->objcore);
	return (b);
}

void
BAN_Free(struct ban *b)
{

	CHECK_OBJ_NOTNULL(b, BAN_MAGIC);
	AZ(b->refcount);
	assert(VTAILQ_EMPTY(&b->objcore));

	if (b->vsb != NULL)
		vsb_delete(b->vsb);
	if (b->spec != NULL)
		free(b->spec);
	FREE_OBJ(b);
}

static struct ban *
ban_CheckLast(void)
{
	struct ban *b;

	Lck_AssertHeld(&ban_mtx);
	b = VTAILQ_LAST(&ban_head, banhead_s);
	if (b != VTAILQ_FIRST(&ban_head) && b->refcount == 0) {
		VSC_main->n_ban--;
		VSC_main->n_ban_retire++;
		VTAILQ_REMOVE(&ban_head, b, list);
	} else {
		b = NULL;
	}
	return (b);
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

	assert(sizeof t == 8);
	memcpy(&t, banspec, sizeof t);
	return (t);
}

static unsigned
ban_len(const uint8_t *banspec)
{
	unsigned u;

	u = vbe32dec(banspec + 8);
	return (u);
}

/*--------------------------------------------------------------------
 * Access a lump of bytes in a ban test spec
 */

static void
ban_add_lump(const struct ban *b, const void *p, uint32_t len)
{
	uint8_t buf[sizeof len];

	vbe32enc(buf, len);
	vsb_bcat(b->vsb, buf, sizeof buf);
	vsb_bcat(b->vsb, p, len);
}

static const void *
ban_get_lump(const uint8_t **bs)
{
	void *r;
	unsigned ln;

	ln = vbe32dec(*bs);
	*bs += 4;
	r = (void*)*bs;
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
	if (bt->arg1 == BAN_ARG_REQHTTP || bt->arg1 == BAN_ARG_OBJHTTP) {
		bt->arg1_spec = (char *)*bs;
		(*bs) += (*bs)[0] + 2;
	}
	bt->arg2 = ban_get_lump(bs);
	bt->oper = *(*bs)++;
	if (bt->oper == BAN_OPER_MATCH || bt->oper == BAN_OPER_NMATCH) 
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
	vsb_putc(b->vsb, (char)l);
	vsb_cat(b->vsb, a1);
	vsb_putc(b->vsb, ':');
	vsb_putc(b->vsb, '\0');
}

/*--------------------------------------------------------------------
 * Parse and add a ban test specification
 */

static int
ban_parse_regexp(struct cli *cli, const struct ban *b, const char *a3)
{
	const char *error;
	int erroroffset, rc, sz;
	pcre *re;

	re = pcre_compile(a3, 0, &error, &erroroffset, NULL);
	if (re == NULL) {
		VSL(SLT_Debug, 0, "REGEX: <%s>", error);
		cli_out(cli, "%s", error);
		cli_result(cli, CLIS_PARAM);
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
		cli_out(cli, "unknown or unsupported field \"%s\"", a1);
		cli_result(cli, CLIS_PARAM);
		return (-1);
	}

	if (pv->flag & PVAR_REQ)
		b->flags |= BAN_F_REQ;

	vsb_putc(b->vsb, pv->tag);
	if (pv->flag & PVAR_HTTP)
		ban_parse_http(b, a1 + strlen(pv->name));

	ban_add_lump(b, a3, strlen(a3) + 1);
	if (!strcmp(a2, "~")) {
		vsb_putc(b->vsb, BAN_OPER_MATCH);
		i = ban_parse_regexp(cli, b, a3);
		if (i)
			return (i);
	} else if (!strcmp(a2, "!~")) {
		vsb_putc(b->vsb, BAN_OPER_NMATCH);
		i = ban_parse_regexp(cli, b, a3);
		if (i)
			return (i);
	} else if (!strcmp(a2, "==")) {
		vsb_putc(b->vsb, BAN_OPER_EQ);
	} else if (!strcmp(a2, "!=")) {
		vsb_putc(b->vsb, BAN_OPER_NEQ);
	} else {
		cli_out(cli,
		    "expected conditional (~, !~, == or !=) got \"%s\"", a2);
		cli_result(cli, CLIS_PARAM);
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
	unsigned pcount;
	ssize_t ln;
	double t0;

	CHECK_OBJ_NOTNULL(b, BAN_MAGIC);

	AZ(vsb_finish(b->vsb));
	ln = vsb_len(b->vsb);
	assert(ln >= 0);

	b->spec = malloc(ln + 13L);	/* XXX */
	XXXAN(b->spec);

	t0 = TIM_real();
	memcpy(b->spec, &t0, sizeof t0);
	b->spec[12] = (b->flags & BAN_F_REQ) ? 1 : 0;
	memcpy(b->spec + 13, vsb_data(b->vsb), ln);
	ln += 13;
	vbe32enc(b->spec + 8, ln);

	vsb_delete(b->vsb);
	b->vsb = NULL;

	Lck_Lock(&ban_mtx);
	VTAILQ_INSERT_HEAD(&ban_head, b, list);
	ban_start = b;
	VSC_main->n_ban++;
	VSC_main->n_ban_add++;

	be = VTAILQ_LAST(&ban_head, banhead_s);
	if (params->ban_dups && be != b)
		be->refcount++;
	else
		be = NULL;

	SMP_NewBan(b->spec, ln);
	Lck_Unlock(&ban_mtx);

	if (be == NULL)
		return;

	/* Hunt down duplicates, and mark them as gone */
	bi = b;
	pcount = 0;
	while(bi != be) {
		bi = VTAILQ_NEXT(bi, list);
		if (bi->flags & BAN_F_GONE)
			continue;
		/* Safe because the length is part of the fixed size hdr */
		if (memcmp(b->spec + 8, bi->spec + 8, ln - 8))
			continue;
		bi->flags |= BAN_F_GONE;
		pcount++;
	}
	Lck_Lock(&ban_mtx);
	be->refcount--;
	VSC_main->n_ban_dups += pcount;
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
	struct ban *b;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	if (oc->ban == NULL)
		return;
	CHECK_OBJ_NOTNULL(oc->ban, BAN_MAGIC);
	Lck_Lock(&ban_mtx);
	assert(oc->ban->refcount > 0);
	oc->ban->refcount--;
	VTAILQ_REMOVE(&oc->ban->objcore, oc, ban_list);
	oc->ban = NULL;

	/* Attempt to purge last ban entry */
	b = ban_CheckLast();
	Lck_Unlock(&ban_mtx);
	if (b != NULL)
		BAN_Free(b);
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
	VTAILQ_FOREACH(b, &ban_head, list) {
		t1 = ban_time(b->spec);
		assert (t1 < t2);
		t2 = t1;
		if (t1 == t0)
			return;
		if (t1 < t0)
			break;
		if (!memcmp(b->spec + 8, ban + 8, len - 8))
			gone |= BAN_F_GONE;
	}

	VSC_main->n_ban++;
	VSC_main->n_ban_add++;

	b2 = BAN_New();
	AN(b2);
	b2->spec = malloc(len);
	AN(b2->spec);
	memcpy(b2->spec, ban, len);
	b2->flags |= gone;
	if (ban[12])
		b2->flags |= BAN_F_REQ;
	if (b == NULL)
		VTAILQ_INSERT_TAIL(&ban_head, b2, list);
	else
		VTAILQ_INSERT_BEFORE(b, b2, list);

	/* Hunt down older duplicates */
	for (b = VTAILQ_NEXT(b2, list); b != NULL; b = VTAILQ_NEXT(b, list)) {
		if (b->flags & BAN_F_GONE)
			continue;
		if (!memcmp(b->spec + 8, ban + 8, len - 8))
			b->flags |= BAN_F_GONE;
	}
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

	SMP_NewBan(ban_magic->spec, ban_len(ban_magic->spec));
	ban_start = VTAILQ_FIRST(&ban_head);
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

	be = bs + ban_len(bs);
	bs += 13;
	while (bs < be) {
		(*tests)++;
		ban_iter(&bs, &bt);
		arg1 = NULL;
		switch (bt.arg1) {
		case BAN_ARG_URL:
			arg1 = reqhttp->hd[HTTP_HDR_URL].b;
			break;
		case BAN_ARG_REQHTTP:
			(void)http_GetHdr(reqhttp, bt.arg1_spec, &arg1);
			break;
		case BAN_ARG_OBJHTTP:
			(void)http_GetHdr(objhttp, bt.arg1_spec, &arg1);
			break;
		default:
			INCOMPL();
		}

		switch (bt.oper) {
		case BAN_OPER_EQ:
			if (arg1 == NULL || strcmp(arg1, bt.arg2))
				return (0);
			break;
		case BAN_OPER_NEQ:
			if (arg1 != NULL && !strcmp(arg1, bt.arg2))
				return (0);
			break;
		case BAN_OPER_MATCH:
			if (arg1 == NULL ||
			    pcre_exec(bt.arg2_spec, NULL, arg1, strlen(arg1),
			    0, 0, NULL, 0) < 0)
				return (0);
			break;
		case BAN_OPER_NMATCH:
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
 * Check an object any fresh bans
 */

static int
ban_check_object(struct object *o, const struct sess *sp, int has_req)
{
	struct ban *b;
	struct objcore *oc;
	struct ban * volatile b0;
	unsigned tests, skipped;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
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
		if (!has_req && (b->flags & BAN_F_REQ)) {
			/*
			 * We cannot test this one, but there might
			 * be other bans that match, so we soldier on
			 */
			skipped++;
		} else if (ban_evaluate(b->spec, o->http, sp->http, &tests))
			break;
	}

	if (b == oc->ban && skipped > 0) {
		/*
		 * Not banned, but some tests were skipped, so we cannot know
		 * for certain that it cannot be, so we just have to give up.
		 */
		return (0);
	}

	Lck_Lock(&ban_mtx);
	oc->ban->refcount--;
	VTAILQ_REMOVE(&oc->ban->objcore, oc, ban_list);
	if (b == oc->ban) {	/* not banned */
		VTAILQ_INSERT_TAIL(&b0->objcore, oc, ban_list);
		b0->refcount++;
	}
	VSC_main->n_ban_obj_test++;
	VSC_main->n_ban_re_test += tests;
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
		WSP(sp, SLT_ExpBan, "%u was banned", o->xid);
		EXP_Rearm(o);
		return (1);
	}
}

int
BAN_CheckObject(struct object *o, const struct sess *sp)
{

	return (ban_check_object(o, sp, 1));
}

/*--------------------------------------------------------------------
 * Ban tail lurker thread
 */

static void
ban_lurker_work(const struct sess *sp)
{
	struct ban *b, *bf;
	struct objhead *oh;
	struct objcore *oc, *oc2;
	struct object *o;
	int i;

	WSL_Flush(sp->wrk, 0);
	WRK_SumStat(sp->wrk);

	Lck_Lock(&ban_mtx);

	/* First try to route the last ban */
	bf = ban_CheckLast();
	if (bf != NULL) {
		Lck_Unlock(&ban_mtx);
		BAN_Free(bf);
		return;
	}

	/* Find the last ban give up, if we have only one */
	b = VTAILQ_LAST(&ban_head, banhead_s);
	if (b == ban_start) {
		Lck_Unlock(&ban_mtx);
		return;
	}

	/* Find the first object on it, if any */
	oc = VTAILQ_FIRST(&b->objcore);
	if (oc == NULL) {
		Lck_Unlock(&ban_mtx);
		return;
	}

	/* Try to lock the objhead */
	oh = oc->objhead;
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	if (Lck_Trylock(&oh->mtx)) {
		Lck_Unlock(&ban_mtx);
		return;
	}

	/*
	 * See if the objcore is still on the objhead since we race against
	 * HSH_Deref() which comes in the opposite locking order.
	 */
	VTAILQ_FOREACH(oc2, &oh->objcs, list)
		if (oc == oc2)
			break;
	if (oc2 == NULL) {
		Lck_Unlock(&oh->mtx);
		Lck_Unlock(&ban_mtx);
		return;
	}
	/*
	 * Grab a reference to the OC and we can let go of the BAN mutex
	 */
	AN(oc->refcnt);
	oc->refcnt++;
	Lck_Unlock(&ban_mtx);

	/*
	 * Get the object and check it against all relevant bans
	 */
	o = oc_getobj(sp->wrk, oc);
	i = ban_check_object(o, sp, 0);
	Lck_Unlock(&oh->mtx);
	WSP(sp, SLT_Debug, "lurker: %p %g %d", oc, o->exp.ttl, i);
	(void)HSH_Deref(sp->wrk, NULL, &o);
}

static void * __match_proto__(bgthread_t)
ban_lurker(struct sess *sp, void *priv)
{

	(void)priv;
	while (1) {
		if (params->ban_lurker_sleep == 0.0) {
			/* Lurker is disabled.  */
			TIM_sleep(1.0);
			continue;
		}
		TIM_sleep(params->ban_lurker_sleep);
		ban_lurker_work(sp);
		WSL_Flush(sp->wrk, 0);
		WRK_SumStat(sp->wrk);
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
		cli_out(cli, "Wrong number of arguments");
		cli_result(cli, CLIS_PARAM);
		return;
	}
	for (i = 3; i < narg; i += 4) {
		if (strcmp(av[i + 2], "&&")) {
			cli_out(cli, "Found \"%s\" expected &&", av[i + 2]);
			cli_result(cli, CLIS_PARAM);
			return;
		}
	}

	b = BAN_New();
	if (b == NULL) {
		cli_out(cli, "Out of Memory");
		cli_result(cli, CLIS_CANT);
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
	bs += 13;
	while (bs < be) {
		ban_iter(&bs, &bt);
		switch (bt.arg1) {
		case BAN_ARG_URL:
			cli_out(cli, "req.url");
			break;
		case BAN_ARG_REQHTTP:
			cli_out(cli, "req.http.%.*s",
			    bt.arg1_spec[0] - 1, bt.arg1_spec + 1);
			break;
		case BAN_ARG_OBJHTTP:
			cli_out(cli, "obj.http.%.*s",
			    bt.arg1_spec[0] - 1, bt.arg1_spec + 1);
			break;
		default:
			INCOMPL();
		}
		switch (bt.oper) {
		case BAN_OPER_EQ:	cli_out(cli, " == "); break;
		case BAN_OPER_NEQ:	cli_out(cli, " != "); break;
		case BAN_OPER_MATCH:	cli_out(cli, " ~ "); break;
		case BAN_OPER_NMATCH:	cli_out(cli, " !~ "); break;
		default:
			INCOMPL();
		}
		cli_out(cli, "%s", bt.arg2);
		if (bs < be)
			cli_out(cli, " && ");
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

	VTAILQ_FOREACH(b, &ban_head, list) {
		if (b == bl)
			break;
		cli_out(cli, "%p %10.6f %5u%s\t", b, ban_time(b->spec),
		    bl == b ? b->refcount - 1 : b->refcount,
		    b->flags & BAN_F_GONE ? "G" : " ");
		ban_render(cli, b->spec);
		cli_out(cli, "\n");
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

	ban_magic = BAN_New();
	AN(ban_magic);
	ban_magic->flags |= BAN_F_GONE;
	BAN_Insert(ban_magic);
	WRK_BgThread(&ban_thread, "ban-lurker", ban_lurker, NULL);
}
