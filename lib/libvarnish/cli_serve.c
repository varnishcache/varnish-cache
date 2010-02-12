/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Redpill Linpro AS
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
SVNID("$Id: cli.c 4235 2009-09-11 13:06:15Z phk $")

#include <ctype.h>
#include <poll.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <vqueue.h>
#include <vsb.h>
#include <vlu.h>
#include <cli.h>
#include <cli_priv.h>
#include <cli_common.h>
#include <cli_serve.h>
#include <libvarnish.h>
#include <miniobj.h>

struct cls_func {
	unsigned			magic;
#define CLS_FUNC_MAGIC			0x7d280c9b
	VTAILQ_ENTRY(cls_func)		list;
	unsigned			auth;
	struct cli_proto		*clp;
};

struct cls_fd {
	unsigned			magic;
#define CLS_FD_MAGIC			0x010dbd1e
	VTAILQ_ENTRY(cls_fd)		list;
	int				fdi, fdo;
	struct cls			*cls;
	struct cli			*cli, clis;
	cls_cb_f			*closefunc;
	void				*priv;
};

struct cls {
	unsigned			magic;
#define CLS_MAGIC			0x60f044a3
	VTAILQ_HEAD(,cls_fd)		fds;
	unsigned			nfd;
	VTAILQ_HEAD(,cls_func)		funcs;
	cls_cbc_f			*before, *after;
	unsigned			maxlen;
};

/*--------------------------------------------------------------------*/

void
CLS_func_close(struct cli *cli, const char *const *av, void *priv)
{

	(void)av;
	(void)priv;
	cli_out(cli, "Closing CLI connection");
	cli_result(cli, CLIS_CLOSE);
}

/*--------------------------------------------------------------------*/

void
CLS_func_ping(struct cli *cli, const char * const *av, void *priv)
{
	time_t t;

	(void)priv;
	(void)av;
	t = time(NULL);
	cli_out(cli, "PONG %ld 1.0", t);
}

/*--------------------------------------------------------------------*/

void
CLS_func_help(struct cli *cli, const char * const *av, void *priv)
{
	struct cli_proto *cp;
	struct cls_func *cfn;
	unsigned all, debug, u, d, h, i, wc;
	struct cls *cs;

	(void)priv;
	cs = cli->cls;
	CHECK_OBJ_NOTNULL(cs, CLS_MAGIC);

	if (av[2] == NULL) {
		all = debug = 0;
	} else if (!strcmp(av[2], "-a")) {
		all = 1;
		debug = 0;
	} else if (!strcmp(av[2], "-d")) {
		all = 0;
		debug = 1;
	} else {
		VTAILQ_FOREACH(cfn, &cs->funcs, list) {
			for (cp = cfn->clp; cp->request != NULL; cp++) {
				if (!strcmp(cp->request, av[2])) {
					cli_out(cli, "%s\n%s\n",
					    cp->syntax, cp->help);
					return;
				}
				for (u = 0; u < sizeof cp->flags; u++) {
					if (cp->flags[u] == '*') {
						cp->func(cli,av,priv);
						return;
					}
				}
			}
		}
		cli_out(cli, "Unknown request.\nType 'help' for more info.\n");
		cli_result(cli, CLIS_UNKNOWN);
		return;
	}
	VTAILQ_FOREACH(cfn, &cs->funcs, list) {
		for (cp = cfn->clp; cp->request != NULL; cp++) {
			d = 0;
			h = 0;
			i = 0;
			wc = 0;
			for (u = 0; u < sizeof cp->flags; u++) {
				if (cp->flags[u] == '\0')
					continue;
				if (cp->flags[u] == 'd')
					d = 1;
				if (cp->flags[u] == 'h')
					h = 1;
				if (cp->flags[u] == 'i')
					i = 1;
				if (cp->flags[u] == '*')
					wc = 1;
			}
			if (i)
				continue;
			if (wc) {
				cp->func(cli, av, priv);
				continue;
			}
			if (debug != d)
				continue;
			if (h && !all)
				continue;
			if (cp->syntax != NULL)
				cli_out(cli, "%s\n", cp->syntax);
		}
	}
}

/*--------------------------------------------------------------------
 * Look for a CLI command to execute
 */

static int
cls_dispatch(struct cli *cli, struct cli_proto *clp, char * const * av,
    unsigned ac)
{
	struct cli_proto *cp;

	AN(av);
	for (cp = clp; cp->request != NULL; cp++) {
		if (!strcmp(av[1], cp->request))
			break;
		if (!strcmp("*", cp->request))
			break;
	}
	if (cp->request == NULL)
		return (0);

	if (cp->func == NULL) {
		cli_out(cli, "Unimplemented\n");
		cli_result(cli, CLIS_UNIMPL);
		return(1);
	}

	if (ac - 1 < cp->minarg) {
		cli_out(cli, "Too few parameters\n");
		cli_result(cli, CLIS_TOOFEW);
		return(1);
	}

	if (ac - 1> cp->maxarg) {
		cli_out(cli, "Too many parameters\n");
		cli_result(cli, CLIS_TOOMANY);
		return(1);
	}

	cli->result = CLIS_OK;
	vsb_clear(cli->sb);
	cp->func(cli, (const char * const *)av, cp->priv);
	return (1);
}

/*--------------------------------------------------------------------
 * We have collected a full cli line, parse it and execute, if possible.
 */

static int
cls_vlu(void *priv, const char *p)
{
	struct cls_fd *cfd;
	struct cls *cs;
	struct cls_func *cfn;
	struct cli *cli;
	char * * av;
	unsigned na;

	CAST_OBJ_NOTNULL(cfd, priv, CLS_FD_MAGIC);
	cs = cfd->cls;
	CHECK_OBJ_NOTNULL(cs, CLS_MAGIC);

	cli = cfd->cli;
	CHECK_OBJ_NOTNULL(cli, CLI_MAGIC);
	AZ(cli->cmd);

	/*
	 * Lines with only whitespace are simply ignored, in order to not
	 * complicate CLI-client side scripts and TELNET users
	 */
	for (; isspace(*p); p++)
		continue;
	if (*p == '\0')
		return (0);

	cli->cmd = p;
	cli->cls = cs;

	av = ParseArgv(p, 0);
	AN(av);

	cli->result = CLIS_UNKNOWN;
	vsb_clear(cli->sb);
	cli_out(cli, "Unknown request.\nType 'help' for more info.\n");

	if (cs->before != NULL)
		cs->before(cli);

	do {
		if (av[0] != NULL) {
			cli_out(cli, "Syntax Error: %s\n", av[0]);
			cli_result(cli, CLIS_SYNTAX);
			break;
		}

		if (isupper(av[1][0])) {
			cli_out(cli, "all commands are in lower-case.\n");
			cli_result(cli, CLIS_UNKNOWN);
			break;
		}

		if (!islower(av[1][0]))
			break;

		for (na = 0; av[na + 1] != NULL; na++)
			continue;

		VTAILQ_FOREACH(cfn, &cs->funcs, list) {
			if (cfn->auth > cli->auth)
				continue;
			if (cls_dispatch(cli, cfn->clp, av, na))
				break;
		}
	} while (0);

	vsb_finish(cli->sb);
	AZ(vsb_overflowed(cli->sb));

	if (cs->after != NULL)
		cs->after(cli);

	cli->cmd = NULL;
	cli->cls = NULL;
	FreeArgv(av);

	if (cli_writeres(cfd->fdo, cli) || cli->result == CLIS_CLOSE)
		return (1);

	return (0);
}

struct cls *
CLS_New(cls_cbc_f *before, cls_cbc_f *after, unsigned maxlen)
{
	struct cls *cs;

	ALLOC_OBJ(cs, CLS_MAGIC);
	AN(cs);
	VTAILQ_INIT(&cs->fds);
	VTAILQ_INIT(&cs->funcs);
	cs->before = before;
	cs->after = after;
	cs->maxlen = maxlen;
	return (cs);
}

struct cli *
CLS_AddFd(struct cls *cs, int fdi, int fdo, cls_cb_f *closefunc, void *priv)
{
	struct cls_fd *cfd;

	CHECK_OBJ_NOTNULL(cs, CLS_MAGIC);
	assert(fdi >= 0);
	assert(fdo >= 0);
	ALLOC_OBJ(cfd, CLS_FD_MAGIC);
	AN(cfd);
	cfd->cls = cs;
	cfd->fdi = fdi;
	cfd->fdo = fdo;
	cfd->cli = &cfd->clis;
	cfd->cli->magic = CLI_MAGIC;
	cfd->cli->vlu = VLU_New(cfd, cls_vlu, cs->maxlen);
	cfd->cli->sb = vsb_newauto();
	cfd->closefunc = closefunc;
	cfd->priv = priv;
	AN(cfd->cli->sb);
	VTAILQ_INSERT_TAIL(&cs->fds, cfd, list);
	cs->nfd++;
	return (cfd->cli);
}

static void
cls_close_fd(struct cls *cs, struct cls_fd *cfd)
{

	CHECK_OBJ_NOTNULL(cs, CLS_MAGIC);
	CHECK_OBJ_NOTNULL(cfd, CLS_FD_MAGIC);

	VTAILQ_REMOVE(&cs->fds, cfd, list);
	cs->nfd--;
	VLU_Destroy(cfd->cli->vlu);
	vsb_delete(cfd->cli->sb);
	if (cfd->closefunc == NULL) {
		(void)close(cfd->fdi);
		if (cfd->fdo != cfd->fdi)
			(void)close(cfd->fdo);
	} else {
		cfd->closefunc(cfd->priv);
	}
	if (cfd->cli->ident != NULL)
		free(cfd->cli->ident);
	FREE_OBJ(cfd);
}


int
CLS_AddFunc(struct cls *cs, unsigned auth, struct cli_proto *clp)
{
	struct cls_func *cfn;

	CHECK_OBJ_NOTNULL(cs, CLS_MAGIC);
	ALLOC_OBJ(cfn, CLS_FUNC_MAGIC);
	AN(cfn);
	cfn->clp = clp;
	cfn->auth = auth;
	VTAILQ_INSERT_TAIL(&cs->funcs, cfn, list);
	return (0);
}

int
CLS_PollFd(struct cls *cs, int fd, int timeout)
{
	struct cls_fd *cfd;
	struct pollfd pfd[1];
	int i, j, k;

	CHECK_OBJ_NOTNULL(cs, CLS_MAGIC);
	if (cs->nfd == 0) {
		errno = 0;
		return (-1);
	}
	assert(cs->nfd > 0);

	i = 0;
	VTAILQ_FOREACH(cfd, &cs->fds, list) {
		if (cfd->fdi != fd)
			continue;
		pfd[i].fd = cfd->fdi;
		pfd[i].events = POLLIN;
		pfd[i].revents = 0;
		i++;
		break;
	}
	assert(i == 1);
	CHECK_OBJ_NOTNULL(cfd, CLS_FD_MAGIC);

	j = poll(pfd, 1, timeout);
	if (j <= 0)
		return (j);
	if (pfd[0].revents & POLLHUP)
		k = 1;
	else
		k = VLU_Fd(cfd->fdi, cfd->cli->vlu);
	if (k)
		cls_close_fd(cs, cfd);
	return (k);
}

int
CLS_Poll(struct cls *cs, int timeout)
{
	struct cls_fd *cfd, *cfd2;
	int i, j, k;

	CHECK_OBJ_NOTNULL(cs, CLS_MAGIC);
	if (cs->nfd == 0) {
		errno = 0;
		return (-1);
	}
	assert(cs->nfd > 0);
	{
		struct pollfd pfd[cs->nfd];

		i = 0;
		VTAILQ_FOREACH(cfd, &cs->fds, list) {
			pfd[i].fd = cfd->fdi;
			pfd[i].events = POLLIN;
			pfd[i].revents = 0;
			i++;
		}
		assert(i == cs->nfd);

		j = poll(pfd, cs->nfd, timeout);
		if (j <= 0)
			return (j);
		i = 0;
		VTAILQ_FOREACH_SAFE(cfd, &cs->fds, list, cfd2) {
			assert(pfd[i].fd == cfd->fdi);
			if (pfd[i].revents & POLLHUP)
				k = 1;
			else
				k = VLU_Fd(cfd->fdi, cfd->cli->vlu);
			if (k)
				cls_close_fd(cs, cfd);
			i++;
		}
		assert(i == j);
	}
	return (j);
}

void
CLS_Destroy(struct cls **csp)
{
	struct cls *cs;
	struct cls_fd *cfd, *cfd2;
	struct cls_func *cfn;

	cs = *csp;
	*csp = NULL;
	CHECK_OBJ_NOTNULL(cs, CLS_MAGIC);
	VTAILQ_FOREACH_SAFE(cfd, &cs->fds, list, cfd2)
		cls_close_fd(cs, cfd);

	while (!VTAILQ_EMPTY(&cs->funcs)) {
		cfn = VTAILQ_FIRST(&cs->funcs);
		VTAILQ_REMOVE(&cs->funcs, cfn, list);
		FREE_OBJ(cfn);
	}
	FREE_OBJ(cs);
}

