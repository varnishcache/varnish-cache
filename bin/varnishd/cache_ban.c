/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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
 * $Id$
 *
 * Ban ("purge") processing
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
#include <regex.h>

#include "shmlog.h"
#include "cli.h"
#include "cli_priv.h"
#include "cache.h"
#include "hash_slinger.h"

struct ban_test;

/* A ban-testing function */
typedef int ban_cond_f(const struct ban_test *bt, const struct object *o, const struct sess *sp);

/* Each individual test to be performed on a ban */
struct ban_test {
	unsigned		magic;
#define BAN_TEST_MAGIC		0x54feec67
	VTAILQ_ENTRY(ban_test)	list;
	int			cost;
	char			*test;
	ban_cond_f		*func;
	regex_t			re;
};

struct ban {
	unsigned		magic;
#define BAN_MAGIC		0x700b08ea
	VTAILQ_ENTRY(ban)	list;
	unsigned		refcount;
	int			flags;
#define BAN_F_GONE		(1 << 0)
	VTAILQ_HEAD(,ban_test)	tests;
};

static VTAILQ_HEAD(banhead,ban) ban_head = VTAILQ_HEAD_INITIALIZER(ban_head);
static struct lock ban_mtx;

/*--------------------------------------------------------------------
 * Manipulation of bans
 */

static struct ban *
ban_new_ban(void)
{
	struct ban *b;
	ALLOC_OBJ(b, BAN_MAGIC);
	if (b == NULL)
		return (b);
	VTAILQ_INIT(&b->tests);
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

static void
ban_sort_by_cost(struct ban *b)
{
	struct ban_test *bt, *btn;
	int i;

	CHECK_OBJ_NOTNULL(b, BAN_MAGIC);

	do {
		i = 0;
		VTAILQ_FOREACH(bt, &b->tests, list) {
			btn = VTAILQ_NEXT(bt, list);
			if (btn != NULL && btn->cost < bt->cost) {
				VTAILQ_REMOVE(&b->tests, bt, list);
				VTAILQ_INSERT_AFTER(&b->tests, btn, bt, list);
				i++;
			}
		}
	} while (i);
}

static void
ban_free_ban(struct ban *b)
{
	struct ban_test *bt;

	CHECK_OBJ_NOTNULL(b, BAN_MAGIC);
	while (!VTAILQ_EMPTY(&b->tests)) {
		bt = VTAILQ_FIRST(&b->tests);
		VTAILQ_REMOVE(&b->tests, bt, list);
		free(bt->test);
		regfree(&bt->re);
		FREE_OBJ(bt);
	}
	FREE_OBJ(b);
}

/*
 * Return zero of the two bans have the same components
 *
 * XXX: Looks too expensive for my taste.
 */

static int
ban_compare(const struct ban *b1, const struct ban *b2)
{
	struct ban_test *bt1, *bt2;
	int n, m;

	CHECK_OBJ_NOTNULL(b1, BAN_MAGIC);
	CHECK_OBJ_NOTNULL(b2, BAN_MAGIC);

	n = 0;
	VTAILQ_FOREACH(bt1, &b1->tests, list) {
		n++;
		VTAILQ_FOREACH(bt2, &b2->tests, list)
			if (!strcmp(bt1->test, bt2->test))
				break;
		if (bt2 == NULL)
			return (1);
	}
	m = 0;
	VTAILQ_FOREACH(bt2, &b2->tests, list)
		m++;
	if (m != n)
		return (1);
	return (0);
}

/*--------------------------------------------------------------------
 * Test functions -- return 0 if the test does not match
 */

static int
ban_cond_url_regexp(const struct ban_test *bt, const struct object *o,
   const struct sess *sp)
{
	(void)o;
	return (!regexec(&bt->re, sp->http->hd[HTTP_HDR_URL].b, 0, NULL, 0));
}

static int
ban_cond_hash_regexp(const struct ban_test *bt, const struct object *o,
   const struct sess *sp)
{
	(void)sp;
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	CHECK_OBJ_NOTNULL(o->objhead, OBJHEAD_MAGIC);
	AN(o->objhead->hash);
	return (!regexec(&bt->re, o->objhead->hash, 0, NULL, 0));
}

/*--------------------------------------------------------------------
 * Parse and add a ban test specification
 */

static int
ban_parse_test(struct cli *cli, struct ban *b, const char *a1, const char *a2, const char *a3)
{
	struct ban_test *bt;
	char buf[512];
	struct vsb *sb;
	int i;

	CHECK_OBJ_NOTNULL(b, BAN_MAGIC);
	bt = ban_add_test(b);
	if (bt == NULL) {
		cli_out(cli, "Out of Memory");
		cli_result(cli, CLIS_CANT);
		return (-1);
	}

	if (strcmp(a2, "~")) {
		/* XXX: Add more conditionals */
		cli_out(cli, "expected \"~\" got \"%s\"", a2);
		cli_result(cli, CLIS_PARAM);
		return (-1);
	}

	i = regcomp(&bt->re, a3, REG_EXTENDED | REG_ICASE | REG_NOSUB);
	if (i) {
		(void)regerror(i, &bt->re, buf, sizeof buf);
		regfree(&bt->re);
		VSL(SLT_Debug, 0, "REGEX: <%s>", buf);
		cli_out(cli, "%s", buf);
		cli_result(cli, CLIS_PARAM);
		return (-1);
	}

	if (!strcmp(a1, "req.url"))
		bt->func = ban_cond_url_regexp;
	else if (!strcmp(a1, "obj.hash"))
		bt->func = ban_cond_hash_regexp;
	else {
		cli_out(cli, "unknown or unsupported field \"%s\"", a1);
		cli_result(cli, CLIS_PARAM);
		return (-1);
	}

	 /* XXX: proper quoting */
	sb = vsb_newauto();
	XXXAN(sb);
	vsb_printf(sb, "%s %s ", a1, a2);
	vsb_quote(sb, a3, 0);
	vsb_finish(sb);
	AZ(vsb_overflowed(sb));
	bt->test = strdup(vsb_data(sb));
	XXXAN(bt->test);
	vsb_delete(sb);
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

static void
BAN_Insert(struct ban *b)
{
	struct ban  *bi, *be;
	unsigned pcount;

	CHECK_OBJ_NOTNULL(b, BAN_MAGIC);
	ban_sort_by_cost(b);

	Lck_Lock(&ban_mtx);
	VTAILQ_INSERT_HEAD(&ban_head, b, list);
	ban_start = b;
	VSL_stats->n_purge++;
	VSL_stats->n_purge_add++;

	if (params->purge_dups) {
		be = VTAILQ_LAST(&ban_head, banhead);
		be->refcount++;
	} else
		be = NULL;
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
		if (ban_compare(b, bi))
			continue;
		bi->flags |= BAN_F_GONE;
		pcount++;
	}
	Lck_Lock(&ban_mtx);
	be->refcount--;
	/* XXX: We should check if the tail can be removed */
	VSL_stats->n_purge_dups += pcount;
	Lck_Unlock(&ban_mtx);
}

void
BAN_NewObj(struct object *o)
{

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	AZ(o->ban);
	Lck_Lock(&ban_mtx);
	o->ban = ban_start;
	ban_start->refcount++;
	Lck_Unlock(&ban_mtx);
}

void
BAN_DestroyObj(struct object *o)
{
	struct ban *b;

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	if (o->ban == NULL)
		return;
	CHECK_OBJ_NOTNULL(o->ban, BAN_MAGIC);
	Lck_Lock(&ban_mtx);
	o->ban->refcount--;
	o->ban = NULL;

	/* Check if we can purge the last ban entry */
	b = VTAILQ_LAST(&ban_head, banhead);
	if (b != VTAILQ_FIRST(&ban_head) && b->refcount == 0) {
		VSL_stats->n_purge--;
		VSL_stats->n_purge_retire++;
		VTAILQ_REMOVE(&ban_head, b, list);
	} else {
		b = NULL;
	}
	Lck_Unlock(&ban_mtx);
	if (b != NULL)
		ban_free_ban(b);

}

int
BAN_CheckObject(struct object *o, const struct sess *sp)
{
	struct ban *b;
	struct ban_test *bt;
	struct ban * volatile b0;
	unsigned tests;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	CHECK_OBJ_NOTNULL(o->ban, BAN_MAGIC);

	b0 = ban_start;

	if (b0 == o->ban)
		return (0);

	/*
	 * This loop is safe without locks, because we know we hold
	 * a refcount on a ban somewhere in the list and we do not
	 * inspect the list past that ban.
	 */
	tests = 0;
	for (b = b0; b != o->ban; b = VTAILQ_NEXT(b, list)) {
		if (b->flags & BAN_F_GONE)
			continue;
		VTAILQ_FOREACH(bt, &b->tests, list) {
			tests++;
			if (bt->func(bt, o, sp))
				break;
		}
		if (bt != NULL)
			break;
	}

	Lck_Lock(&ban_mtx);
	o->ban->refcount--;
	if (b == o->ban)	/* not banned */
		b0->refcount++;
	VSL_stats->n_purge_obj_test++;
	VSL_stats->n_purge_re_test += tests;
	Lck_Unlock(&ban_mtx);

	if (b == o->ban) {	/* not banned */
		o->ban = b0;
		return (0);
	} else {
		o->ban = NULL;
		return (1);
	}
}

/*--------------------------------------------------------------------
 * CLI functions to add bans
 */

static void
ccf_purge(struct cli *cli, const char * const *av, void *priv)
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

	b = ban_new_ban();
	if (b == NULL) {
		cli_out(cli, "Out of Memory");
		cli_result(cli, CLIS_CANT);
		return;
	}
	for (i = 0; i < narg; i += 4)
		if (ban_parse_test(cli, b, av[i + 2], av[i + 3], av[i + 4])) {
			ban_free_ban(b);
			return;
		}
	BAN_Insert(b);
}

int
BAN_Add(struct cli *cli, const char *regexp, int hash)
{
	const char *aav[6];

	aav[0] = NULL;
	aav[1] = "purge";
	if (hash)
		aav[2] = "obj.hash";
	else
		aav[2] = "req.url";
	aav[3] = "~";
	aav[4] = regexp;
	aav[5] = NULL;
	ccf_purge(cli, aav, NULL);
	return (0);
}

static void
ccf_purge_url(struct cli *cli, const char * const *av, void *priv)
{
	const char *aav[6];

	(void)priv;
	aav[0] = NULL;
	aav[1] = "purge";
	aav[2] = "req.url";
	aav[3] = "~";
	aav[4] = av[2];
	aav[5] = NULL;
	ccf_purge(cli, aav, priv);
}

static void
ccf_purge_hash(struct cli *cli, const char * const *av, void *priv)
{
	const char *aav[6];

	(void)priv;
	aav[0] = NULL;
	aav[1] = "purge";
	aav[2] = "obj.hash";
	aav[3] = "~";
	aav[4] = av[2];
	aav[5] = NULL;
	ccf_purge(cli, aav, priv);
}

static void
ccf_purge_list(struct cli *cli, const char * const *av, void *priv)
{
	struct ban *b;
	struct ban_test *bt;

	(void)av;
	(void)priv;

	Lck_Lock(&ban_mtx);
	VTAILQ_LAST(&ban_head, banhead)->refcount++;
	Lck_Unlock(&ban_mtx);

	VTAILQ_FOREACH(b, &ban_head, list) {
		bt = VTAILQ_FIRST(&b->tests);
		cli_out(cli, "%5u %4s\t%s\n",
		    b->refcount, b->flags ? "Gone" : "", bt->test);
		do {
			bt = VTAILQ_NEXT(bt, list);
			if (bt != NULL)
				cli_out(cli, "\t\t%s\n", bt->test);
		} while (bt != NULL);
	}

	Lck_Lock(&ban_mtx);
	VTAILQ_LAST(&ban_head, banhead)->refcount--;
	Lck_Unlock(&ban_mtx);
}

static struct cli_proto ban_cmds[] = {
	/*
	 * XXX: COMPAT: Retain these two entries for entire 2.x series
	 * XXX: COMPAT: to stay compatible with 1.x series syntax.
	 */
	{ CLI_HIDDEN("url.purge", 1, 1)		ccf_purge_url },
	{ CLI_HIDDEN("hash.purge", 1, 1)	ccf_purge_hash },

	{ CLI_PURGE_URL,			ccf_purge_url },
	{ CLI_PURGE_HASH,			ccf_purge_hash },
	{ CLI_PURGE,				ccf_purge },
	{ CLI_PURGE_LIST,			ccf_purge_list },
	{ NULL }
};

void
BAN_Init(void)
{

	Lck_New(&ban_mtx);
	CLI_AddFuncs(PUBLIC_CLI, ban_cmds);
	/* Add an initial ban, since the list can never be empty */
	(void)BAN_Add(NULL, ".", 0);
}
