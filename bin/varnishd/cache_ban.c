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

#include "cli.h"
#include "cli_priv.h"
#include "cache.h"
#include "hash_slinger.h"
#include "vre.h"

#include "cache_ban.h"

static VTAILQ_HEAD(banhead,ban)	ban_head = VTAILQ_HEAD_INITIALIZER(ban_head);
static struct lock ban_mtx;
static struct ban *ban_magic;
static pthread_t ban_thread;

/*--------------------------------------------------------------------
 * Manipulation of bans
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
	VTAILQ_INIT(&b->tests);
	VTAILQ_INIT(&b->objcore);
	return (b);
}

static struct ban_test *
ban_add_test(struct ban *b)
{
	struct ban_test *bt;

	CHECK_OBJ_NOTNULL(b, BAN_MAGIC);
	ALLOC_OBJ(bt, BAN_TEST_MAGIC);
	if (bt == NULL)
		return (bt);
	VTAILQ_INSERT_TAIL(&b->tests, bt, list);
	return (bt);
}

void
BAN_Free(struct ban *b)
{
	struct ban_test *bt;

	CHECK_OBJ_NOTNULL(b, BAN_MAGIC);
	AZ(b->refcount);
	assert(VTAILQ_EMPTY(&b->objcore));

	if (b->vsb != NULL)
		vsb_delete(b->vsb);
	if (b->test != NULL)
		free(b->test);
	while (!VTAILQ_EMPTY(&b->tests)) {
		bt = VTAILQ_FIRST(&b->tests);
		VTAILQ_REMOVE(&b->tests, bt, list);
		if (bt->flags & BAN_T_REGEXP)
			VRE_free(&bt->re);
		if (bt->dst != NULL)
			free(bt->dst);
		if (bt->src != NULL)
			free(bt->src);
		FREE_OBJ(bt);
	}
	FREE_OBJ(b);
}

/*--------------------------------------------------------------------
 * Test functions -- return 0 if the test matches
 */

static int
ban_cond_str(const struct ban_test *bt, const char *p)
{
	int i;

	if (p == NULL)
		return(!(bt->flags & BAN_T_NOT));
	if (bt->flags & BAN_T_REGEXP) {
		i = VRE_exec(bt->re, p, strlen(p), 0, 0, NULL, 0);
		if (i >= 0)
			i = 0;
	} else {
		i = strcmp(bt->dst, p);
	}
	if (bt->flags & BAN_T_NOT)
		return (!i);
	return (i);
}

static int
ban_cond_url(const struct ban_test *bt, const struct object *o,
   const struct sess *sp)
{
	(void)o;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	return(ban_cond_str(bt, sp->http->hd[HTTP_HDR_URL].b));
}

static int
ban_cond_req_http(const struct ban_test *bt, const struct object *o,
   const struct sess *sp)
{
	char *s;

	(void)o;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	(void)http_GetHdr(sp->http, bt->src, &s);
	return (ban_cond_str(bt, s));
}

static int
ban_cond_obj_http(const struct ban_test *bt, const struct object *o,
   const struct sess *sp)
{
	char *s;

	(void)sp;
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	(void)http_GetHdr(o->http, bt->src, &s);
	return (ban_cond_str(bt, s));
}

/*--------------------------------------------------------------------
 * Parse and add a ban test specification
 */

static int
ban_parse_regexp(struct cli *cli, struct ban_test *bt, const char *a3)
{
	const char *error;
	int erroroffset;

	bt->re = VRE_compile(a3, 0, &error, &erroroffset);
	if (bt->re == NULL) {
		VSL(SLT_Debug, 0, "REGEX: <%s>", error);
		cli_out(cli, "%s", error);
		cli_result(cli, CLIS_PARAM);
		return (-1);
	}
	bt->flags |= BAN_T_REGEXP;
	return (0);
}

static void
ban_parse_http(struct ban_test *bt, const char *a1)
{
	int l;

	l = strlen(a1);
	assert(l < 127);
	bt->src = malloc(l + 3L);
	XXXAN(bt->src);
	bt->src[0] = (char)l + 1;
	memcpy(bt->src + 1, a1, l);
	bt->src[l + 1] = ':';
	bt->src[l + 2] = '\0';
}

static const struct pvar {
	const char		*name;
	unsigned		flag;
	ban_cond_f		*func;
} pvars[] = {
#define PVAR(a, b, c)	{ (a), (b), (c) },
#include "ban_vars.h"
#undef PVAR
	{ 0, 0, 0}
};

int
BAN_AddTest(struct cli *cli, struct ban *b, const char *a1, const char *a2,
    const char *a3)
{
	struct ban_test *bt;
	const struct pvar *pv;
	int i;

	CHECK_OBJ_NOTNULL(b, BAN_MAGIC);
	if (!VTAILQ_EMPTY(&b->tests))
		vsb_cat(b->vsb, " && ");
	bt = ban_add_test(b);
	if (bt == NULL) {
		cli_out(cli, "Out of Memory");
		cli_result(cli, CLIS_CANT);
		return (-1);
	}

	if (!strcmp(a2, "~")) {
		i = ban_parse_regexp(cli, bt, a3);
		if (i)
			return (i);
	} else if (!strcmp(a2, "!~")) {
		bt->flags |= BAN_T_NOT;
		i = ban_parse_regexp(cli, bt, a3);
		if (i)
			return (i);
	} else if (!strcmp(a2, "==")) {
		bt->dst = strdup(a3);
		XXXAN(bt->dst);
	} else if (!strcmp(a2, "!=")) {
		bt->flags |= BAN_T_NOT;
		bt->dst = strdup(a3);
		XXXAN(bt->dst);
	} else {
		cli_out(cli,
		    "expected conditional (~, !~, == or !=) got \"%s\"", a2);
		cli_result(cli, CLIS_PARAM);
		return (-1);
	}


	for (pv = pvars; pv->name != NULL; pv++) {
		if (strncmp(a1, pv->name, strlen(pv->name)))
			continue;
		bt->func = pv->func;
		if (pv->flag & PVAR_REQ)
			b->flags |= BAN_F_REQ;
		if (pv->flag & PVAR_HTTP)
			ban_parse_http(bt, a1 + strlen(pv->name));
		break;
	}
	if (pv->name == NULL) {
		cli_out(cli, "unknown or unsupported field \"%s\"", a1);
		cli_result(cli, CLIS_PARAM);
		return (-1);
	}

	vsb_printf(b->vsb, "%s %s ", a1, a2);
	vsb_quote(b->vsb, a3, -1, 0);
	return (0);
}

/*--------------------------------------------------------------------
 */


/*
 * We maintain ban_start as a pointer to the first element of the list
 * as a separate variable from the VTAILQ, to avoid depending on the
 * internals of the VTAILQ macros.  We tacitly assume that a pointer
 * write is always atomic in doing so.
 */
static struct ban * volatile ban_start;

void
BAN_Insert(struct ban *b)
{
	struct ban  *bi, *be;
	unsigned pcount;

	CHECK_OBJ_NOTNULL(b, BAN_MAGIC);
	b->t0 = TIM_real();

	AZ(vsb_finish(b->vsb));
	b->test = strdup(vsb_data(b->vsb));
	AN(b->test);
	vsb_delete(b->vsb);
	b->vsb = NULL;

	Lck_Lock(&ban_mtx);
	VTAILQ_INSERT_HEAD(&ban_head, b, list);
	ban_start = b;
	VSC_main->n_ban++;
	VSC_main->n_ban_add++;

	be = VTAILQ_LAST(&ban_head, banhead);
	if (params->ban_dups && be != b)
		be->refcount++;
	else
		be = NULL;

	SMP_NewBan(b->t0, b->test);
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
		if (strcmp(b->test, bi->test))
			continue;
		bi->flags |= BAN_F_GONE;
		pcount++;
	}
	Lck_Lock(&ban_mtx);
	be->refcount--;
	VSC_main->n_ban_dups += pcount;
	Lck_Unlock(&ban_mtx);
}

void
BAN_NewObj(struct object *o)
{
	struct objcore *oc;

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	oc = o->objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AZ(oc->ban);
	Lck_Lock(&ban_mtx);
	oc->ban = ban_start;
	ban_start->refcount++;
	VTAILQ_INSERT_TAIL(&ban_start->objcore, oc, ban_list);
	Lck_Unlock(&ban_mtx);
	/*
	 * XXX Should this happen here or in stevedore.c ?
	 * XXX Or even at all ?  -spersistent is only user
	 * XXX and has the field duplicated.
	 */
	o->ban_t = oc->ban->t0;
}

static struct ban *
BAN_CheckLast(void)
{
	struct ban *b;

	Lck_AssertHeld(&ban_mtx);
	b = VTAILQ_LAST(&ban_head, banhead);
	if (b != VTAILQ_FIRST(&ban_head) && b->refcount == 0) {
		VSC_main->n_ban--;
		VSC_main->n_ban_retire++;
		VTAILQ_REMOVE(&ban_head, b, list);
	} else {
		b = NULL;
	}
	return (b);
}

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
	b = BAN_CheckLast();
	Lck_Unlock(&ban_mtx);
	if (b != NULL)
		BAN_Free(b);
}


static int
ban_check_object(struct object *o, const struct sess *sp, int has_req)
{
	struct ban *b;
	struct objcore *oc;
	struct ban_test *bt;
	struct ban * volatile b0;
	unsigned tests;

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
	for (b = b0; b != oc->ban; b = VTAILQ_NEXT(b, list)) {
		CHECK_OBJ_NOTNULL(b, BAN_MAGIC);
		if (b->flags & BAN_F_GONE)
			continue;
		if (!has_req && (b->flags & BAN_F_REQ))
			return (0);
		VTAILQ_FOREACH(bt, &b->tests, list) {
			tests++;
			if (bt->func(bt, o, sp))
				break;
		}
		if (bt == NULL)
			break;
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
		o->ban_t = oc->ban->t0;
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

	return ban_check_object(o, sp, 1);
}

/*--------------------------------------------------------------------
 * Ban tail lurker thread
 */

static void * __match_proto__(bgthread_t)
ban_lurker(struct sess *sp, void *priv)
{
	struct ban *b, *bf;
	struct objcore *oc;
	struct object *o;
	int i;

	(void)priv;
	while (1) {
		WSL_Flush(sp->wrk, 0);
		WRK_SumStat(sp->wrk);
		if (params->ban_lurker_sleep == 0.0) {
			AZ(sleep(1));
			continue;
		}
		Lck_Lock(&ban_mtx);

		/* First try to route the last ban */
		bf = BAN_CheckLast();
		if (bf != NULL) {
			Lck_Unlock(&ban_mtx);
			BAN_Free(bf);
			TIM_sleep(params->ban_lurker_sleep);
			continue;
		}
		/* Then try to poke the first object on the last ban */
		oc = NULL;
		while (1) {
			b = VTAILQ_LAST(&ban_head, banhead);
			if (b == ban_start)
				break;
			oc = VTAILQ_FIRST(&b->objcore);
			if (oc == NULL)
				break;
			HSH_FindBan(sp, &oc);
			break;
		}
		Lck_Unlock(&ban_mtx);
		if (oc == NULL) {
			TIM_sleep(1.0);
			continue;
		}
		// AZ(oc->flags & OC_F_PERSISTENT);
		o = oc_getobj(sp->wrk, oc);
		i = ban_check_object(o, sp, 0);
		WSP(sp, SLT_Debug, "lurker: %p %g %d", oc, o->exp.ttl, i);
		(void)HSH_Deref(sp->wrk, NULL, &o);
		TIM_sleep(params->ban_lurker_sleep);
	}
	NEEDLESS_RETURN(NULL);
}


/*--------------------------------------------------------------------
 * Release a tail reference
 */

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
 * Get a reference to the oldest ban in the list
 */

struct ban *
BAN_TailRef(void)
{
	struct ban *b;

	ASSERT_CLI();
	b = VTAILQ_LAST(&ban_head, banhead);
	AN(b);
	b->refcount++;
	return (b);
}

/*--------------------------------------------------------------------
 * Find and/or Grab a reference to an objects ban based on timestamp
 */

struct ban *
BAN_RefBan(struct objcore *oc, double t0, const struct ban *tail)
{
	struct ban *b;

	VTAILQ_FOREACH(b, &ban_head, list) {
		if (b == tail)
			break;
		if (b->t0 <= t0)
			break;
	}
	AN(b);
	assert(b->t0 == t0);
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
 * If a newer ban has same condition, mark the new ban GONE, and
 * mark any older bans, with the same condition, GONE as well.
 */

void
BAN_Reload(double t0, unsigned flags, const char *ban)
{
	struct ban *b, *b2;
	int gone = 0;

	ASSERT_CLI();

	(void)flags;		/* for future use */

	VTAILQ_FOREACH(b, &ban_head, list) {
		if (!strcmp(b->test, ban)) {
			if (b->t0 > t0)
				gone |= BAN_F_GONE;
			else if (b->t0 == t0)
				return;
		} else if (b->t0 > t0)
			continue;
		if (b->t0 < t0)
			break;
	}

	VSC_main->n_ban++;
	VSC_main->n_ban_add++;

	b2 = BAN_New();
	AN(b2);
	b2->test = strdup(ban);
	AN(b2->test);
	b2->t0 = t0;
	b2->flags |= BAN_F_PENDING | gone;
	if (b == NULL)
		VTAILQ_INSERT_TAIL(&ban_head, b2, list);
	else
		VTAILQ_INSERT_BEFORE(b, b2, list);

	/* Hunt down older duplicates */
	for (b = VTAILQ_NEXT(b2, list); b != NULL; b = VTAILQ_NEXT(b, list)) {
		if (b->flags & BAN_F_GONE)
			continue;
		if (!strcmp(b->test, b2->test))
			b->flags |= BAN_F_GONE;
	}
}

/*--------------------------------------------------------------------
 * All silos have read their bans, now compile them.
 */

void
BAN_Compile(void)
{
	struct ban *b;
	char **av;
	int i;

	ASSERT_CLI();

	SMP_NewBan(ban_magic->t0, ban_magic->test);
	VTAILQ_FOREACH(b, &ban_head, list) {
		if (!(b->flags & BAN_F_PENDING))
			continue;
		b->flags &= ~BAN_F_PENDING;
		if (b->flags & BAN_F_GONE)
			continue;
		av = ParseArgv(b->test, 0);
		XXXAN(av);
		XXXAZ(av[0]);
		for (i = 1; av[i] != NULL; i += 3) {
			if (i != 1) {
				AZ(strcmp(av[i], "&&"));
				i++;
			}
			AZ(BAN_AddTest(NULL, b, av[i], av[i + 1], av[i + 2]));
		}
	}
	ban_start = VTAILQ_FIRST(&ban_head);
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
ccf_ban_list(struct cli *cli, const char * const *av, void *priv)
{
	struct ban *b, *bl = NULL;

	(void)av;
	(void)priv;

	do {
		/* Attempt to purge last ban entry */
		Lck_Lock(&ban_mtx);
		b = BAN_CheckLast();
		bl = VTAILQ_LAST(&ban_head, banhead);
		if (b == NULL)
			bl->refcount++;
		Lck_Unlock(&ban_mtx);
		if (b != NULL)
			BAN_Free(b);
	} while (b != NULL);
	AN(bl);

	VTAILQ_FOREACH(b, &ban_head, list) {
		// if (b->refcount == 0 && (b->flags & BAN_F_GONE))
		//	continue;
		cli_out(cli, "%p %10.6f %5u%s\t%s\n", b, b->t0,
		    bl == b ? b->refcount - 1 : b->refcount,
		    b->flags & BAN_F_GONE ? "G" : " ", b->test);
	}

	Lck_Lock(&ban_mtx);
	bl->refcount--;
	Lck_Unlock(&ban_mtx);
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
