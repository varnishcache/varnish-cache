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
 * Stuff for handling the CLI protocol
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <cli.h>
#include <cli_priv.h>
#include <libvarnish.h>

/*
 * Generic help function.
 *
 * priv must point to cli_proto array
 */

void
cli_func_help(struct cli *cli, const char * const *av, void *priv)
{
	struct cli_proto *cp;

	if (av[2] == NULL || *av[2] == '-') {
		for (cp = priv; cp->request != NULL; cp++)
			if (cp->syntax != NULL)
				cli_out(cli, "%s\n", cp->syntax);
		return;
	}
	for (cp = priv; cp->request != NULL; cp++) {
		if (cp->syntax == NULL)
			continue;
		if (!strcmp(cp->request, av[2])) {
			cli_out(cli, "%s\n%s\n", cp->syntax, cp->help);
			return;
		}
	}
	cli_out(cli, "Unknown request.\nType 'help' for more info.\n");
	cli_result(cli, CLIS_UNKNOWN);
}

void
cli_dispatch(struct cli *cli, struct cli_proto *clp, const char *line)
{
	char **av;
	unsigned u;
	struct cli_proto *cp;

	cli_result(cli, CLIS_OK);
	/* XXX: syslog commands */
	av = ParseArgv(line, 0);
	AN(av);
	do {
		if (av[0] != NULL) {
			cli_out(cli, "Syntax Error: %s\n", av[0]);
			cli_result(cli, CLIS_SYNTAX);
			break;
		}
		if (av[1] == NULL)
			break;
		if (isupper(av[1][0])) {
			cli_out(cli,
			    "all commands are in lower-case.\n");
			cli_result(cli, CLIS_UNKNOWN);
			break;
		}
		for (cp = clp; cp->request != NULL; cp++) {
			if (!strcmp(av[1], cp->request))
				break;
			if (!strcmp("*", cp->request))
				break;
		}
		if (cp->request == NULL) {
			cli_out(cli,
			    "Unknown request.\nType 'help' for more info.\n");
			cli_result(cli, CLIS_UNKNOWN);
			break;
		}

		if (cp->func == NULL) {
			cli_out(cli, "Unimplemented\n");
			cli_result(cli, CLIS_UNIMPL);
			break;
		}

		for (u = 0; u <= cp->minarg; u++) {
			if (av[u + 1] != NULL)
				continue;
			cli_out(cli, "Too few parameters\n");
			cli_result(cli, CLIS_TOOFEW);
			break;
		}
		if (u <= cp->minarg)
			break;
		for (; u <= cp->maxarg; u++)
			if (av[u + 1] == NULL)
				break;
		if (av[u + 1] != NULL) {
			cli_out(cli, "Too many parameters\n");
			cli_result(cli, CLIS_TOOMANY);
			break;
		}

		cp->func(cli, (const char * const *)av, cp->priv);

	} while (0);
	FreeArgv(av);
}

struct cli_proto *
cli_concat(struct cli_proto *c1, struct cli_proto *c2)
{
	struct cli_proto *c;
	int i1, i2;

	i1 = 0;
	for(c = c1; c != NULL && c->request != NULL; c++)
		i1++;
	i2 = 0;
	for(c = c2; c != NULL && c->request != NULL; c++)
		i2++;

	c = malloc(sizeof(*c) * (1L + i1 + i2));
	if (c == NULL)
		return (c);
	if (c1 != NULL)
		memcpy(c, c1, sizeof(*c1) * i1);
	if (c2 != NULL)
		memcpy(c + i1, c2, sizeof(*c2) * i2);
	memset(c + i1 + i2, 0, sizeof(*c));
	return (c);
}

