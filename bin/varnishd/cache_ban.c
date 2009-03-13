/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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
	ban_cond_f		*func;
	int			flags;
#define BAN_T_REGEXP		(1 << 0)
#define BAN_T_NOT		(1 << 1)
	regex_t			re;
	char			*dst;
	char			*src;
};

struct ban {
	unsigned		magic;
#define BAN_MAGIC		0x700b08ea
	VTAILQ_ENTRY(ban)	list;
	unsigned		refcount;
	int			flags;
#define BAN_F_GONE		(1 << 0)
	VTAILQ_HEAD(,ban_test)	tests;
	double			t0;
	struct vsb		*vsb;
	char			*test;
};

static VTAILQ_HEAD(banhead,ban) ban_head = VTAILQ_HEAD_INITIALIZER(ban_head);
static struct lock ban_mtx;

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
	b->vsb = vsb_newauto();
	if (b->vsb == NULL) {
		FREE_OBJ(b);
		return (NULL);
	}
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

void
BAN_Free(struct ban *b)
{
	struct ban_test *bt;

	CHECK_OBJ_NOTNULL(b, BAN_MAGIC);
	if (b->vsb != NULL)
		vsb_delete(b->vsb);
	if (b->test != NULL)
		free(b->test);
	while (!VTAILQ_EMPTY(&b->tests)) {
		bt = VTAILQ_FIRST(&b->tests);
		VTAILQ_REMOVE(&b->tests, bt, list);
		if (bt->flags & BAN_T_REGEXP)
			regfree(&bt->re);
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
	if (bt->flags & BAN_T_REGEXP)
		i = regexec(&bt->re, p, 0, NULL, 0);
	else
		i = strcmp(bt->dst, p);
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
ban_cond_hash(const struct ban_test *bt, const struct object *o,
   const struct sess *sp)
{
	(void)sp;
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	CHECK_OBJ_NOTNULL(o->objhead, OBJHEAD_MAGIC);
	AN(o->objhead->hash);
	return(ban_cond_str(bt, o->objhead->hash));
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
	int i;
	char buf[512];

	i = regcomp(&bt->re, a3, REG_EXTENDED | REG_ICASE | REG_NOSUB);
	if (i) {
		(void)regerror(i, &bt->re, buf, sizeof buf);
		regfree(&bt->re);
		VSL(SLT_Debug, 0, "REGEX: <%s>", buf);
		cli_out(cli, "%s", buf);
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
	bt->src = malloc(l + 3);
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
#define PVAR(a, b, c)	{ a, b, c },
#include "purge_vars.h"
#undef PVAR
	{ 0, 0, 0}
};

int
BAN_AddTest(struct cli *cli, struct ban *b, const char *a1, const char *a2, const char *a3)
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
		if (pv->flag & 1) 
			ban_parse_http(bt, a1 + strlen(pv->name));
		break;
	}
	if (pv->name == NULL) {
		cli_out(cli, "unknown or unsupported field \"%s\"", a1);
		cli_result(cli, CLIS_PARAM);
		return (-1);
	}

	vsb_printf(b->vsb, "%s %s ", a1, a2);
	vsb_quote(b->vsb, a3, 0);
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

	vsb_finish(b->vsb);
	AZ(vsb_overflowed(b->vsb));
	b->test = strdup(vsb_data(b->vsb));
	AN(b->test);
	vsb_delete(b->vsb);
	b->vsb = NULL;

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
		if (strcmp(b->test, bi->test))
			continue;
		bi->flags |= BAN_F_GONE;
		pcount++;
	}
	SMP_NewBan(b->t0, b->test);
	Lck_Lock(&ban_mtx);
	be->refcount--;
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

static struct ban *
BAN_CheckLast(void)
{
	struct ban *b;

	Lck_AssertHeld(&ban_mtx);
	b = VTAILQ_LAST(&ban_head, banhead);
	if (b != VTAILQ_FIRST(&ban_head) && b->refcount == 0) {
		VSL_stats->n_purge--;
		VSL_stats->n_purge_retire++;
		VTAILQ_REMOVE(&ban_head, b, list);
	} else {
		b = NULL;
	}
	return (b);
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
	assert(o->ban->refcount > 0);
	o->ban->refcount--;
	o->ban = NULL;

	/* Attempt to purge last ban entry */
	b = BAN_CheckLast();
	Lck_Unlock(&ban_mtx);
	if (b != NULL)
		BAN_Free(b);
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
		if (bt == NULL)
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
		if (o->smp_object != NULL)
			SMP_BANchanged(o, b0->t0);
		return (0);
	} else {
		o->ttl = 0;
		o->ban = NULL;
		if (o->smp_object != NULL)
			SMP_TTLchanged(o);
		/* BAN also changed, but that is not important any more */
		WSP(sp, SLT_ExpBan, "%u was banned", o->xid);
		EXP_Rearm(o);
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
	if (!save_hash) {
		cli_out(cli,
		    "purge.hash not possible.\n"
		    "Set the \"purge_hash\" parameter to on\n"
		    "and restart the varnish worker process to enable.\n");
		cli_result(cli, CLIS_CANT);
		return;
	}
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

	(void)av;
	(void)priv;

	do {
		/* Attempt to purge last ban entry */
		Lck_Lock(&ban_mtx);
		b = BAN_CheckLast();
		if (b == NULL)
			VTAILQ_LAST(&ban_head, banhead)->refcount++;
		Lck_Unlock(&ban_mtx);
		if (b != NULL)
			BAN_Free(b);
	} while (b != NULL);

	VTAILQ_FOREACH(b, &ban_head, list) {
		if (b->refcount == 0 && (b->flags & BAN_F_GONE))
			continue;
		cli_out(cli, "%5u%s\t%s\n", b->refcount,
		    b->flags & BAN_F_GONE ? "G" : " ", b->test);
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
	const char *aav[6];

	Lck_New(&ban_mtx);
	CLI_AddFuncs(PUBLIC_CLI, ban_cmds);

	/* Add an initial ban, since the list can never be empty */
	aav[0] = NULL;
	aav[1] = "purge";
	aav[2] = "req.url";
	aav[3] = "~";
	aav[4] = ".";
	aav[5] = NULL;
	ccf_purge(NULL, aav, NULL);
}
