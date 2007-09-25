/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2007 Linpro AS
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
 * Ban processing
 */

#include <sys/types.h>

#include <stdlib.h>
#include <string.h>
#include <regex.h>

#include "shmlog.h"
#include "cli_priv.h"
#include "cache.h"

struct ban {
	VTAILQ_ENTRY(ban)	list;
	unsigned		gen;
	regex_t			regexp;
	char			*ban;
	int			hash;
};

static VTAILQ_HEAD(,ban) ban_head = VTAILQ_HEAD_INITIALIZER(ban_head);
static unsigned ban_next;
static struct ban *ban_start;

void
AddBan(const char *regexp, int hash)
{
	struct ban *b;
	int i;

	b = calloc(sizeof *b, 1);
	XXXAN(b);

	i = regcomp(&b->regexp, regexp, REG_EXTENDED | REG_ICASE | REG_NOSUB);
	if (i) {
		char buf[512];

		(void)regerror(i, &b->regexp, buf, sizeof buf);
		VSL(SLT_Debug, 0, "REGEX: <%s>", buf);
	}
	b->hash = hash;
	b->gen = ++ban_next;
	b->ban = strdup(regexp);
	VTAILQ_INSERT_HEAD(&ban_head, b, list);
	ban_start = b;
}

void
BAN_NewObj(struct object *o)
{

	o->ban_seq = ban_next;
}

int
BAN_CheckObject(struct object *o, const char *url, const char *hash)
{
	struct ban *b, *b0;
	int i;

	b0 = ban_start;
	for (b = b0;
	    b != NULL && b->gen > o->ban_seq;
	    b = VTAILQ_NEXT(b, list)) {
		i = regexec(&b->regexp, b->hash ? hash : url, 0, NULL, 0);
		if (!i)
			return (1);
	}
	o->ban_seq = b0->gen;
	return (0);
}

void
cli_func_url_purge(struct cli *cli, char **av, void *priv)
{

	(void)priv;
	AddBan(av[2], 0);
	cli_out(cli, "PURGE %s\n", av[2]);
}

void
cli_func_hash_purge(struct cli *cli, char **av, void *priv)
{

	(void)priv;
	AddBan(av[2], 1);
	cli_out(cli, "PURGE %s\n", av[2]);
}

void
BAN_Init(void)
{

	AddBan("\001", 0);
}
