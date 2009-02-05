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

struct ban {
	unsigned		magic;
#define BAN_MAGIC		0x700b08ea
	VTAILQ_ENTRY(ban)	list;
	unsigned		refcount;
	int			flags;
#define BAN_F_GONE		(1 << 0)
	regex_t			regexp;
	char			*ban;
	int			hash;
};

static VTAILQ_HEAD(banhead,ban) ban_head = VTAILQ_HEAD_INITIALIZER(ban_head);
static MTX ban_mtx;

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
	char buf[512];
	unsigned pcount;
	int i;

	ALLOC_OBJ(b, BAN_MAGIC);
	if (b == NULL) {
		cli_out(cli, "Out of Memory");
		cli_result(cli, CLIS_CANT);
		return (-1);
	}

	i = regcomp(&b->regexp, regexp, REG_EXTENDED | REG_ICASE | REG_NOSUB);
	if (i) {
		(void)regerror(i, &b->regexp, buf, sizeof buf);
		regfree(&b->regexp);
		VSL(SLT_Debug, 0, "REGEX: <%s>", buf);
		cli_out(cli, "%s", buf);
		cli_result(cli, CLIS_PARAM);
		FREE_OBJ(b);
		return (-1);
	}
	b->hash = hash;
	b->ban = strdup(regexp);
	AN(b->ban);
	LOCK(&ban_mtx);
	VTAILQ_INSERT_HEAD(&ban_head, b, list);
	ban_start = b;
	VSL_stats->n_purge++;
	VSL_stats->n_purge_add++;

	if (params->purge_dups) {
		be = VTAILQ_LAST(&ban_head, banhead);
		be->refcount++;
	} else
		be = NULL;
	UNLOCK(&ban_mtx);

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
	LOCK(&ban_mtx);
	be->refcount--;
	/* XXX: We should check if the tail can be removed */
	VSL_stats->n_purge_dups += pcount;
	UNLOCK(&ban_mtx);

	return (0);
}

void
BAN_NewObj(struct object *o)
{

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	AZ(o->ban);
	LOCK(&ban_mtx);
	o->ban = ban_start;
	ban_start->refcount++;
	UNLOCK(&ban_mtx);
}

void
BAN_DestroyObj(struct object *o)
{
	struct ban *b;

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	if (o->ban == NULL)
		return;
	CHECK_OBJ_NOTNULL(o->ban, BAN_MAGIC);
	LOCK(&ban_mtx);
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
	UNLOCK(&ban_mtx);
	if (b != NULL) {
		free(b->ban);
		regfree(&b->regexp);
		FREE_OBJ(b);
	}

}

int
BAN_CheckObject(struct object *o, const char *url, const char *hash)
{
	struct ban *b;
	struct ban * volatile b0;
	unsigned tests;

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
		tests++;
		if (!(b->flags & BAN_F_GONE) &&
		    !regexec(&b->regexp, b->hash ? hash : url, 0, NULL, 0))
			break;
	}

	LOCK(&ban_mtx);
	o->ban->refcount--;
	if (b == o->ban)	/* not banned */
		b0->refcount++;
	VSL_stats->n_purge_obj_test++;
	VSL_stats->n_purge_re_test += tests;
	UNLOCK(&ban_mtx);

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
	{ CLI_PURGE_LIST,			ccf_purge_list },
	{ NULL }
};

void
BAN_Init(void)
{

	MTX_INIT(&ban_mtx);
	CLI_AddFuncs(PUBLIC_CLI, ban_cmds);
	/* Add an initial ban, since the list can never be empty */
	(void)BAN_Add(NULL, ".", 0);
}
