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
	char			*ban;
	int			hash;
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
		FREE_OBJ(bt);
	}
	FREE_OBJ(b);
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
 */


/*
 * We maintain ban_start as a pointer to the first element of the list
 * as a separate variable from the VTAILQ, to avoid depending on the
 * internals of the VTAILQ macros.  We tacitly assume that a pointer
 * write is always atomic in doing so.
 */
static struct ban * volatile ban_start;

int
BAN_Add(struct cli *cli, const char *regexp, int hash)
{
	struct ban *b, *bi, *be;
	struct ban_test *bt;
	char buf[512];
	unsigned pcount;
	int i;

	b = ban_new_ban();
	if (b == NULL) {
		cli_out(cli, "Out of Memory");
		cli_result(cli, CLIS_CANT);
		return (-1);
	}

	bt = ban_add_test(b);
	if (bt == NULL) {
		cli_out(cli, "Out of Memory");
		cli_result(cli, CLIS_CANT);
		ban_free_ban(b);
		return (-1);
	}

	ban_sort_by_cost(b);

	if (hash)
		bt->func = ban_cond_hash_regexp;
	else
		bt->func = ban_cond_url_regexp;
		
	i = regcomp(&bt->re, regexp, REG_EXTENDED | REG_ICASE | REG_NOSUB);
	if (i) {
		(void)regerror(i, &bt->re, buf, sizeof buf);
		regfree(&bt->re);
		VSL(SLT_Debug, 0, "REGEX: <%s>", buf);
		cli_out(cli, "%s", buf);
		cli_result(cli, CLIS_PARAM);
		ban_free_ban(b);
		return (-1);
	}
	b->hash = hash;
	b->ban = strdup(regexp);
	AN(b->ban);
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
		return (0);

	/* Hunt down duplicates, and mark them as gone */
	bi = b;
	pcount = 0;
	while(bi != be) {
		bi = VTAILQ_NEXT(bi, list);
		if (bi->flags & BAN_F_GONE)
			continue;
		if (b->hash != bi->hash)
			continue;
		if (strcmp(b->ban, bi->ban))
			continue;
		bi->flags |= BAN_F_GONE;
		pcount++;
	}
	Lck_Lock(&ban_mtx);
	be->refcount--;
	/* XXX: We should check if the tail can be removed */
	VSL_stats->n_purge_dups += pcount;
	Lck_Unlock(&ban_mtx);

	return (0);
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

#if 0
static void
ccf_purge(struct cli *cli, const char * const *av, void *priv)
{

	(void)priv;
	(void)av;
	(void)cli;
}
#endif

static void
ccf_purge_url(struct cli *cli, const char * const *av, void *priv)
{

	(void)priv;
	(void)BAN_Add(cli, av[2], 0);
}

static void
ccf_purge_hash(struct cli *cli, const char * const *av, void *priv)
{

	(void)priv;
	(void)BAN_Add(cli, av[2], 1);
}

static void
ccf_purge_list(struct cli *cli, const char * const *av, void *priv)
{
	struct ban *b0;

	(void)av;
	(void)priv;
	/*
	 * XXX: Strictly speaking, this loop traversal is not lock-safe
	 * XXX: because we might inspect the last ban while it gets
	 * XXX: destroyed.  To properly fix this, we would need to either
	 * XXX: hold the lock over the entire loop, or grab refcounts
	 * XXX: under lock for each element of the list.
	 * XXX: We do neither, and hope for the best.
	 */
	for (b0 = ban_start; b0 != NULL; b0 = VTAILQ_NEXT(b0, list)) {
		if (b0->refcount == 0 && VTAILQ_NEXT(b0, list) == NULL)
			break;
		cli_out(cli, "%5u %d %s \"%s\"\n",
		    b0->refcount, b0->flags,
		    b0->hash ? "hash" : "url ",
		    b0->ban);
	}
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
#if 0
	{ CLI_PURGE,				ccf_purge },
#endif
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
