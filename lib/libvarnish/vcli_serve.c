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
 * Stuff for handling the CLI protocol
 */

#include "config.h"

#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vdef.h"
#include "vas.h"
#include "vqueue.h"
#include "miniobj.h"

#include "vav.h"
#include "vcli_serve.h"
#include "vsb.h"
#include "vtim.h"

struct VCLS_fd {
	unsigned			magic;
#define VCLS_FD_MAGIC			0x010dbd1e
	VTAILQ_ENTRY(VCLS_fd)		list;
	int				fdi, fdo;
	struct VCLS			*cls;
	struct cli			*cli, clis;
	cls_cb_f			*closefunc;
	void				*priv;
	struct vsb			*last_arg;
	char				**argv;
	int				argc;
	char				*match;
};

struct VCLS {
	unsigned			magic;
#define VCLS_MAGIC			0x60f044a3
	VTAILQ_HEAD(,VCLS_fd)		fds;
	unsigned			nfd;
	VTAILQ_HEAD(,cli_proto)		funcs;
	cls_cbc_f			*before, *after;
	volatile unsigned		*limit;
	struct cli_proto		*wildcard;
};

/*--------------------------------------------------------------------*/

void v_matchproto_(cli_func_t)
VCLS_func_close(struct cli *cli, const char *const *av, void *priv)
{

	(void)av;
	(void)priv;
	VCLI_Out(cli, "Closing CLI connection");
	VCLI_SetResult(cli, CLIS_CLOSE);
}

/*--------------------------------------------------------------------*/

void v_matchproto_(cli_func_t)
VCLS_func_ping(struct cli *cli, const char * const *av, void *priv)
{
	time_t t;

	(void)av;
	(void)priv;
	t = time(NULL);
	VCLI_Out(cli, "PONG %jd 1.0", (intmax_t)t);
}

void v_matchproto_(cli_func_t)
VCLS_func_ping_json(struct cli *cli, const char * const *av, void *priv)
{
	(void)av;
	(void)priv;
	VCLI_JSON_begin(cli, 2, av);
	VCLI_Out(cli, ", \"PONG\"\n");
	VCLI_JSON_end(cli);
}

/*--------------------------------------------------------------------*/

void v_matchproto_(cli_func_t)
VCLS_func_help(struct cli *cli, const char * const *av, void *priv)
{
	struct cli_proto *clp;
	unsigned all, debug, d;
	struct VCLS *cs;

	(void)priv;
	cs = cli->cls;
	CHECK_OBJ_NOTNULL(cs, VCLS_MAGIC);

	if (av[2] == NULL) {
		all = debug = 0;
	} else if (!strcmp(av[2], "-a")) {
		all = 1;
		debug = 0;
	} else if (!strcmp(av[2], "-d")) {
		all = 0;
		debug = 1;
	} else {
		VTAILQ_FOREACH(clp, &cs->funcs, list) {
			if (clp->auth <= cli->auth &&
			    !strcmp(clp->desc->request, av[2])) {
				VCLI_Out(cli, "%s\n%s\n",
				    clp->desc->syntax, clp->desc->help);
				return;
			}
		}
		VCLI_Out(cli, "Unknown request.\nType 'help' for more info.\n");
		VCLI_SetResult(cli, CLIS_UNKNOWN);
		return;
	}
	VTAILQ_FOREACH(clp, &cs->funcs, list) {
		if (clp->auth > cli->auth)
			continue;
		d =  strchr(clp->flags, 'd') != NULL ? 1 : 0;
		if (d && (!all && !debug))
			continue;
		if (debug && !d)
			continue;
		if (clp->desc->syntax != NULL)
			VCLI_Out(cli, "%s\n", clp->desc->syntax);
	}
}

void v_matchproto_(cli_func_t)
VCLS_func_help_json(struct cli *cli, const char * const *av, void *priv)
{
	struct cli_proto *clp;
	struct VCLS *cs;

	(void)priv;
	cs = cli->cls;
	CHECK_OBJ_NOTNULL(cs, VCLS_MAGIC);

	VCLI_JSON_begin(cli, 2, av);
	VTAILQ_FOREACH(clp, &cs->funcs, list) {
		if (clp->auth > cli->auth)
			continue;
		VCLI_Out(cli, ",\n  {\n");
		VSB_indent(cli->sb, 2);
		VCLI_Out(cli, "\"request\": ");
		VCLI_JSON_str(cli, clp->desc->request);
		VCLI_Out(cli, ",\n");
		VCLI_Out(cli, "\"syntax\": ");
		VCLI_JSON_str(cli, clp->desc->syntax);
		VCLI_Out(cli, ",\n");
		VCLI_Out(cli, "\"help\": ");
		VCLI_JSON_str(cli, clp->desc->help);
		VCLI_Out(cli, ",\n");
		VCLI_Out(cli, "\"minarg\": %d", clp->desc->minarg);
		VCLI_Out(cli, ",\n");
		VCLI_Out(cli, "\"maxarg\": %d", clp->desc->maxarg);
		VCLI_Out(cli, ",\n");
		VCLI_Out(cli, "\"flags\": ");
		VCLI_JSON_str(cli, clp->flags);
		VCLI_Out(cli, ",\n");
		VCLI_Out(cli, "\"json\": %s",
		    clp->jsonfunc == NULL ? "false" : "true");
		VCLI_Out(cli, "\n");
		VSB_indent(cli->sb, -2);
		VCLI_Out(cli, "}");
	}
	VCLI_JSON_end(cli);
}

/*--------------------------------------------------------------------
 * Look for a CLI command to execute
 */

static void
cls_dispatch(struct cli *cli, const struct cli_proto *cp,
    char * const * av, unsigned ac)
{
	int json = 0;

	AN(av);

	VSB_clear(cli->sb);

	if (ac > 1 && !strcmp(av[2], "-j"))
		json = 1;

	if (cp->func == NULL && !json) {
		VCLI_Out(cli, "Unimplemented\n");
		VCLI_SetResult(cli, CLIS_UNIMPL);
		return;
	}
	if (cp->jsonfunc == NULL && json) {
		VCLI_Out(cli, "JSON unimplemented\n");
		VCLI_SetResult(cli, CLIS_UNIMPL);
		return;
	}

	if (ac - 1 < cp->desc->minarg + json) {
		VCLI_Out(cli, "Too few parameters\n");
		VCLI_SetResult(cli, CLIS_TOOFEW);
		return;
	}

	if (ac - 1 > cp->desc->maxarg + json) {
		VCLI_Out(cli, "Too many parameters\n");
		VCLI_SetResult(cli, CLIS_TOOMANY);
		return;
	}

	cli->result = CLIS_OK;
	if (json)
		cp->jsonfunc(cli, (const char * const *)av, cp->priv);
	else
		cp->func(cli, (const char * const *)av, cp->priv);
}

/*--------------------------------------------------------------------
 * We have collected a full cli line, parse it and execute, if possible.
 */

static int
cls_exec(struct VCLS_fd *cfd, char * const *av)
{
	struct VCLS *cs;
	struct cli_proto *clp;
	struct cli *cli;
	unsigned na;
	ssize_t len;
	char *s;
	unsigned lim;
	int retval = 0;
	const char *trunc = "!\n[response was truncated]\n";

	CHECK_OBJ_NOTNULL(cfd, VCLS_FD_MAGIC);
	cs = cfd->cls;
	CHECK_OBJ_NOTNULL(cs, VCLS_MAGIC);

	cli = cfd->cli;
	CHECK_OBJ_NOTNULL(cli, CLI_MAGIC);
	AN(cli->cmd);

	cli->cls = cs;

	cli->result = CLIS_UNKNOWN;
	VSB_clear(cli->sb);
	VCLI_Out(cli, "Unknown request.\nType 'help' for more info.\n");

	if (cs->before != NULL)
		cs->before(cli);

	do {
		if (av[0] != NULL) {
			VCLI_Out(cli, "Syntax Error: %s\n", av[0]);
			VCLI_SetResult(cli, CLIS_SYNTAX);
			break;
		}

		if (av[1] == NULL) {
			VCLI_Out(cli, "Empty CLI command.\n");
			VCLI_SetResult(cli, CLIS_SYNTAX);
			break;
		}

		if (isupper(av[1][0])) {
			VCLI_Out(cli, "all commands are in lower-case.\n");
			VCLI_SetResult(cli, CLIS_UNKNOWN);
			break;
		}

		if (!islower(av[1][0]))
			break;

		for (na = 0; av[na + 1] != NULL; na++)
			continue;

		VTAILQ_FOREACH(clp, &cs->funcs, list) {
			if (clp->auth > cli->auth)
				continue;
			if (!strcmp(clp->desc->request, av[1])) {
				cls_dispatch(cli, clp, av, na);
				break;
			}
		}
		if (clp == NULL &&
		    cs->wildcard && cs->wildcard->auth <= cli->auth)
			cls_dispatch(cli, cs->wildcard, av, na);

	} while (0);

	AZ(VSB_finish(cli->sb));

	if (cs->after != NULL)
		cs->after(cli);

	cli->cls = NULL;

	s = VSB_data(cli->sb);
	len = VSB_len(cli->sb);
	lim = *cs->limit;
	if (len > lim) {
		if (cli->result == CLIS_OK)
			cli->result = CLIS_TRUNCATED;
		strcpy(s + (lim - strlen(trunc)), trunc);
		assert(strlen(s) <= lim);
	}
	if (VCLI_WriteResult(cfd->fdo, cli->result, s) ||
	    cli->result == CLIS_CLOSE)
		retval = 1;

	/*
	 * In unauthenticated mode we are very intolerant, and close the
	 * connection at the least provocation.
	 */
	if (cli->auth == 0 && cli->result != CLIS_OK)
		retval = 1;

	return (retval);
}

static int
cls_feed(struct VCLS_fd *cfd, const char *p, const char *e)
{
	struct cli *cli;
	int i, retval = 0, ac;
	char **av, *q;

	CHECK_OBJ_NOTNULL(cfd, VCLS_FD_MAGIC);
	AN(p);
	assert(e > p);

	cli = cfd->cli;
	CHECK_OBJ_NOTNULL(cli, CLI_MAGIC);

	for (;p < e; p++) {
		if (cli->cmd == NULL && isspace(*p)) {
			/* Ignore all leading space before cmd */
			continue;
		}
		if (cfd->argv == NULL) {

			/* Collect first line up to \n or \r */
			if (cli->cmd == NULL) {
				cli->cmd = VSB_new_auto();
				AN(cli->cmd);
			}

			/* Until authenticated, limit length hard */
			if (*p != '\n' && *p != '\r' &&
			    (cli->auth > 0 || VSB_len(cli->cmd) < 80)) {
				VSB_putc(cli->cmd, *p);
				continue;
			}

			AZ(VSB_finish(cli->cmd));

			/* Ignore leading '-' */
			q = VSB_data(cli->cmd);
			if (*q == '-')
				q++;
			av = VAV_Parse(q, &ac, 0);
			AN(av);

			if (cli->auth > 0 &&
			    av[0] == NULL &&
			    ac >= 3 &&
			    !strcmp(av[ac-2], "<<") &&
			    *av[ac - 1] != '\0') {
				/* Go to "<< nonce" mode */
				cfd->argv = av;
				cfd->argc = ac;
				cfd->match = av[ac - 1];
				cfd->last_arg = VSB_new_auto();
				AN(cfd->last_arg);
			} else {
				/* Plain command */
				i = cls_exec(cfd, av);
				VAV_Free(av);
				VSB_destroy(&cli->cmd);
				if (i)
					return(i);
			}
		} else {
			/* "<< nonce" mode */
			AN(cfd->argv);
			AN(cfd->argc);
			AN(cfd->match);
			AN(cfd->last_arg);
			if (*cfd->match == '\0' && (*p == '\r' || *p == '\n')) {
				AZ(VSB_finish(cfd->last_arg));
				// NB: VAV lib internals trusted
				REPLACE(cfd->argv[cfd->argc - 1], NULL);
				REPLACE(cfd->argv[cfd->argc - 2], NULL);
				cfd->argv[cfd->argc - 2] =
				    VSB_data(cfd->last_arg);
				i = cls_exec(cfd, cfd->argv);
				cfd->argv[cfd->argc - 2] = NULL;
				VAV_Free(cfd->argv);
				cfd->argv = NULL;
				VSB_destroy(&cfd->last_arg);
				VSB_destroy(&cli->cmd);
				if (i)
					return (i);
			} else if (*p == *cfd->match) {
				cfd->match++;
			} else if (cfd->match != cfd->argv[cfd->argc - 1]) {
				q = cfd->argv[cfd->argc - 1];
				VSB_bcat(cfd->last_arg, q, cfd->match - q);
				cfd->match = q;
				VSB_putc(cfd->last_arg, *p);
			} else {
				VSB_putc(cfd->last_arg, *p);
			}
		}
	}
	return (retval);
}

struct VCLS *
VCLS_New(struct VCLS *model)
{
	struct VCLS *cs;

	CHECK_OBJ_ORNULL(model, VCLS_MAGIC);

	ALLOC_OBJ(cs, VCLS_MAGIC);
	AN(cs);
	VTAILQ_INIT(&cs->fds);
	VTAILQ_INIT(&cs->funcs);
	if (model != NULL)
		VTAILQ_CONCAT(&cs->funcs, &model->funcs, list);
	return (cs);
}

void
VCLS_SetLimit(struct VCLS *cs, volatile unsigned *limit)
{
	CHECK_OBJ_NOTNULL(cs, VCLS_MAGIC);
	cs->limit = limit;
}

void
VCLS_SetHooks(struct VCLS *cs, cls_cbc_f *before, cls_cbc_f *after)
{

	CHECK_OBJ_NOTNULL(cs, VCLS_MAGIC);
	cs->before = before;
	cs->after = after;
}

struct cli *
VCLS_AddFd(struct VCLS *cs, int fdi, int fdo, cls_cb_f *closefunc, void *priv)
{
	struct VCLS_fd *cfd;

	CHECK_OBJ_NOTNULL(cs, VCLS_MAGIC);
	assert(fdi >= 0);
	assert(fdo >= 0);
	ALLOC_OBJ(cfd, VCLS_FD_MAGIC);
	AN(cfd);
	cfd->cls = cs;
	cfd->fdi = fdi;
	cfd->fdo = fdo;
	cfd->cli = &cfd->clis;
	cfd->cli->magic = CLI_MAGIC;
	cfd->cli->sb = VSB_new_auto();
	AN(cfd->cli->sb);
	cfd->cli->limit = cs->limit;
	cfd->cli->priv = priv;
	cfd->closefunc = closefunc;
	cfd->priv = priv;
	VTAILQ_INSERT_TAIL(&cs->fds, cfd, list);
	cs->nfd++;
	return (cfd->cli);
}

static int
cls_close_fd(struct VCLS *cs, struct VCLS_fd *cfd)
{
	int retval = 0;

	CHECK_OBJ_NOTNULL(cs, VCLS_MAGIC);
	CHECK_OBJ_NOTNULL(cfd, VCLS_FD_MAGIC);

	VTAILQ_REMOVE(&cs->fds, cfd, list);
	cs->nfd--;
	VSB_destroy(&cfd->cli->sb);
	if (cfd->closefunc != NULL)
		retval = cfd->closefunc(cfd->priv);
	(void)close(cfd->fdi);
	if (cfd->fdo != cfd->fdi)
		(void)close(cfd->fdo);
	if (cfd->cli->ident != NULL)
		free(cfd->cli->ident);
	FREE_OBJ(cfd);
	return (retval);
}

void
VCLS_AddFunc(struct VCLS *cs, unsigned auth, struct cli_proto *clp)
{
	struct cli_proto *clp2;
	int i;

	CHECK_OBJ_NOTNULL(cs, VCLS_MAGIC);
	AN(clp);

	for (;clp->desc != NULL; clp++) {
		clp->auth = auth;
		if (!strcmp(clp->desc->request, "*")) {
			cs->wildcard = clp;
		} else {
			i = 0;
			VTAILQ_FOREACH(clp2, &cs->funcs, list) {
				i = strcmp(clp->desc->request,
				    clp2->desc->request);
				if (i <= 0)
					break;
			}
			if (clp2 != NULL && i == 0) {
				VTAILQ_INSERT_BEFORE(clp2, clp, list);
				VTAILQ_REMOVE(&cs->funcs, clp2, list);
			} else if (clp2 != NULL)
				VTAILQ_INSERT_BEFORE(clp2, clp, list);
			else
				VTAILQ_INSERT_TAIL(&cs->funcs, clp, list);
		}
	}
}

int
VCLS_Poll(struct VCLS *cs, const struct cli *cli, int timeout)
{
	struct VCLS_fd *cfd;
	struct pollfd pfd[1];
	int i, j, k;
	char buf[BUFSIZ];

	CHECK_OBJ_NOTNULL(cs, VCLS_MAGIC);
	if (cs->nfd == 0) {
		errno = 0;
		return (-1);
	}
	assert(cs->nfd > 0);

	i = 0;
	VTAILQ_FOREACH(cfd, &cs->fds, list) {
		if (cfd->cli != cli)
			continue;
		pfd[i].fd = cfd->fdi;
		pfd[i].events = POLLIN;
		pfd[i].revents = 0;
		i++;
		break;
	}
	assert(i == 1);
	CHECK_OBJ_NOTNULL(cfd, VCLS_FD_MAGIC);

	j = poll(pfd, 1, timeout);
	if (j <= 0)
		return (j);
	if (pfd[0].revents & POLLHUP)
		k = 1;
	else {
		i = read(cfd->fdi, buf, sizeof buf);
		if (i <= 0)
			k = 1;
		else
			k = cls_feed(cfd, buf, buf + i);
	}
	if (k) {
		i = cls_close_fd(cs, cfd);
		if (i < 0)
			k = i;
	}
	return (k);
}

void
VCLS_Destroy(struct VCLS **csp)
{
	struct VCLS *cs;
	struct VCLS_fd *cfd, *cfd2;
	struct cli_proto *clp;

	TAKE_OBJ_NOTNULL(cs, csp, VCLS_MAGIC);
	VTAILQ_FOREACH_SAFE(cfd, &cs->fds, list, cfd2)
		(void)cls_close_fd(cs, cfd);

	while (!VTAILQ_EMPTY(&cs->funcs)) {
		clp = VTAILQ_FIRST(&cs->funcs);
		VTAILQ_REMOVE(&cs->funcs, clp, list);
	}
	FREE_OBJ(cs);
}

/**********************************************************************
 * Utility functions for implementing CLI commands
 */

/*lint -e{818} cli could be const */
void
VCLI_Out(struct cli *cli, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (cli != NULL) {
		CHECK_OBJ_NOTNULL(cli, CLI_MAGIC);
		if (VSB_len(cli->sb) < *cli->limit)
			(void)VSB_vprintf(cli->sb, fmt, ap);
		else if (cli->result == CLIS_OK)
			cli->result = CLIS_TRUNCATED;
	} else {
		(void)vfprintf(stdout, fmt, ap);
	}
	va_end(ap);
}

/*lint -e{818} cli could be const */
int
VCLI_Overflow(struct cli *cli)
{
	CHECK_OBJ_NOTNULL(cli, CLI_MAGIC);
	if (cli->result == CLIS_TRUNCATED ||
	    VSB_len(cli->sb) >= *cli->limit)
		return (1);
	return (0);
}

/*lint -e{818} cli could be const */
void
VCLI_JSON_str(struct cli *cli, const char *s)
{

	CHECK_OBJ_NOTNULL(cli, CLI_MAGIC);
	VSB_putc(cli->sb, '"');
	VSB_quote(cli->sb, s, -1, VSB_QUOTE_JSON);
	VSB_putc(cli->sb, '"');
}

/*lint -e{818} cli could be const */
void
VCLI_JSON_begin(struct cli *cli, unsigned ver, const char * const * av)
{
	int i;

	CHECK_OBJ_NOTNULL(cli, CLI_MAGIC);
	VCLI_Out(cli, "[ %u, [", ver);
	for (i = 1; av[i] != NULL; i++) {
		VCLI_JSON_str(cli, av[i]);
		if (av[i + 1] != NULL)
			VCLI_Out(cli, ", ");
	}
	VCLI_Out(cli, "], %.3f", VTIM_real());
	VSB_indent(cli->sb, 2);
}

void
VCLI_JSON_end(struct cli *cli)
{
	VSB_indent(cli->sb, -2);
	VCLI_Out(cli, "\n");
	VCLI_Out(cli, "]\n");
}

/*lint -e{818} cli could be const */
void
VCLI_Quote(struct cli *cli, const char *s)
{

	CHECK_OBJ_NOTNULL(cli, CLI_MAGIC);
	VSB_quote(cli->sb, s, -1, 0);
}

void
VCLI_SetResult(struct cli *cli, unsigned res)
{

	if (cli != NULL) {
		CHECK_OBJ_NOTNULL(cli, CLI_MAGIC);
		if (cli->result != CLIS_TRUNCATED || res != CLIS_OK)
			cli->result = res;	/*lint !e64 type mismatch */
	} else {
		printf("CLI result = %u\n", res);
	}
}
