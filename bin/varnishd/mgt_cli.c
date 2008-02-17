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
 * The management process' CLI handling
 */

#include <sys/types.h>

#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#ifndef HAVE_VASPRINTF
#include "compat/vasprintf.h"
#endif

#include "cli_priv.h"
#include "cli.h"
#include "vsb.h"
#include "cli_common.h"
#include "heritage.h"
#include "mgt.h"
#include "mgt_cli.h"
#include "mgt_event.h"
#include "shmlog.h"

#include "vss.h"

static int		cli_i = -1, cli_o = -1;

/*--------------------------------------------------------------------*/

static void
mcf_stats(struct cli *cli, const char * const *av, void *priv)
{

	(void)av;
	(void)priv;

	AN(VSL_stats);
#define MAC_STAT(n, t, f, d) \
    cli_out(cli, "%12ju  %s\n", (VSL_stats->n), d);
#include "stat_field.h"
#undef MAC_STAT
}


/*--------------------------------------------------------------------
 * Passthru of cli commands.  It is more or less just undoing what
 * the cli parser did, but such is life...
 */

static void
mcf_passthru(struct cli *cli, const char * const *av, void *priv)
{
	struct vsb *sb;
	const char *p;
	char *q;
	unsigned u;
	int i;

	(void)priv;

	/* Request */
	if (cli_o <= 0) {
		cli_result(cli, CLIS_CANT);
		cli_out(cli, "Cache process not running");
		return;
	}
	sb = vsb_new(NULL, NULL, 64, VSB_AUTOEXTEND);
	XXXAN(sb);
	for (u = 1; av[u] != NULL; u++) {
		if (u > 1)
			vsb_putc(sb, ' ');
		vsb_putc(sb, '"');
		for (p = av[u]; *p; p++) {
			switch (*p) {
			case '\\':
				vsb_cat(sb, "\\\\");
				break;
			case '\n':
				vsb_cat(sb, "\\n");
				break;
			case '"':
				vsb_cat(sb, "\\\"");
				break;
			default:
				vsb_putc(sb, *p);
			}
		}
		vsb_putc(sb, '"');
	}
	vsb_putc(sb, '\n');
	xxxassert(!vsb_overflowed(sb));
	vsb_finish(sb);
	i = write(cli_o, vsb_data(sb), vsb_len(sb));
	xxxassert(i == vsb_len(sb));
	vsb_delete(sb);

	i = cli_readres(cli_i, &u, &q, params->cli_timeout);
	cli_result(cli, u);
	cli_out(cli, "%s", q);
	free(q);

}

/*--------------------------------------------------------------------*/

static struct cli_proto *cli_proto;

/* XXX: what order should this list be in ? */
static struct cli_proto mgt_cli_proto[] = {
	{ CLI_PING,		cli_func_ping },
	{ CLI_SERVER_STATUS,	mcf_server_status, NULL },
	{ CLI_SERVER_START,	mcf_server_startstop, NULL },
	{ CLI_SERVER_STOP,	mcf_server_startstop, &cli_proto },
	{ CLI_STATS,		mcf_stats, NULL },
	{ CLI_VCL_LOAD,		mcf_config_load, NULL },
	{ CLI_VCL_INLINE,	mcf_config_inline, NULL },
	{ CLI_VCL_USE,		mcf_config_use, NULL },
	{ CLI_VCL_DISCARD,	mcf_config_discard, NULL },
	{ CLI_VCL_LIST,		mcf_config_list, NULL },
	{ CLI_VCL_SHOW,		mcf_config_show, NULL },
	{ CLI_PARAM_SHOW,	mcf_param_show, NULL },
	{ CLI_PARAM_SET,	mcf_param_set, NULL },
	{ CLI_HELP,		cli_func_help, NULL },
#if 0
	{ CLI_SERVER_RESTART },
	{ CLI_ZERO },
	{ CLI_VERBOSE,		m_cli_func_verbose, NULL },
	{ CLI_EXIT, 		m_cli_func_exit, NULL},
	{ CLI_QUIT },
	{ CLI_BYE },
#endif
	{ NULL }
};


/*--------------------------------------------------------------------*/

void
mgt_cli_init(void)
{
	struct cli_proto *cp;
	unsigned u, v;

	/*
	 * Build the joint cli_proto by combining the manager process
	 * entries with with the cache process entries.  The latter
	 * get a "passthough" function in the joint list
	 */
	u = 0;
	for (cp = mgt_cli_proto; cp->request != NULL; cp++)
		u++;
	for (cp = CLI_cmds; cp->request != NULL; cp++)
		u++;
	cli_proto = calloc(sizeof *cli_proto, u + 1);
	XXXAN(cli_proto);
	u = 0;
	for (cp = mgt_cli_proto; cp->request != NULL; cp++)
		cli_proto[u++] = *cp;
	for (cp = CLI_cmds; cp->request != NULL; cp++) {
		/* Skip any cache commands we already have in the manager */
		for (v = 0; v < u; v++)
			if (!strcmp(cli_proto[v].request, cp->request))
				break;
		if (v < u)
			continue;
		cli_proto[u] = *cp;
		cli_proto[u].func = mcf_passthru;
		u++;
	}

	/* Fixup the entry for 'help' entry */
	for (u = 0; cli_proto[u].request != NULL; u++) {
		if (!strcmp(cli_proto[u].request, "help")) {
			cli_proto[u].priv = cli_proto;
			break;
		}
	}
}

/*--------------------------------------------------------------------
 * Ask the child something over CLI, return zero only if everything is
 * happy happy.
 */

int
mgt_cli_askchild(unsigned *status, char **resp, const char *fmt, ...)
{
	char *p;
	int i, j;
	va_list ap;
	unsigned u;

	if (resp != NULL)
		*resp = NULL;
	if (status != NULL)
		*status = 0;
	if (cli_i < 0|| cli_o < 0) {
		if (status != NULL)
			*status = CLIS_CANT;
		return (CLIS_CANT);
	}
	va_start(ap, fmt);
	i = vasprintf(&p, fmt, ap);
	va_end(ap);
	if (i < 0)
		return (i);
	assert(p[i - 1] == '\n');
	j = write(cli_o, p, i);
	free(p);
	if (j != i) {
		if (status != NULL)
			*status = CLIS_COMMS;
		if (resp != NULL)
			*resp = strdup("CLI communication error");
		return (CLIS_COMMS);
	}

	i = cli_readres(cli_i, &u, resp, params->cli_timeout);
	if (status != NULL)
		*status = u;
	return (u == CLIS_OK ? 0 : u);
}

/*--------------------------------------------------------------------*/

void
mgt_cli_start_child(int fdi, int fdo)
{

	cli_i = fdi;
	cli_o = fdo;
}

/*--------------------------------------------------------------------*/

void
mgt_cli_stop_child(void)
{

	cli_i = -1;
	cli_o = -1;
	/* XXX: kick any users */
}

/*--------------------------------------------------------------------*/

struct cli_port {
	unsigned		magic;
#define CLI_PORT_MAGIC		0x5791079f
	struct ev		*ev;
	int			fdi;
	int			fdo;
	int			verbose;
	char			*buf;
	int			nbuf; /* next free position in buf */
	int			lbuf; /* length of buf */
	struct cli		cli[1];
	char			name[30];
};

static int
mgt_cli_callback(const struct ev *e, int what)
{
	struct cli_port *cp;
	char *p, *q;
	int i;

	CAST_OBJ_NOTNULL(cp, e->priv, CLI_PORT_MAGIC);

	if (what & (EV_ERR | EV_HUP))
		goto cli_close;

	/* grow the buffer if it is full */
	if (cp->nbuf == cp->lbuf) {
		cp->lbuf += cp->lbuf;
		cp->buf = realloc(cp->buf, cp->lbuf);
		XXXAN(cp->buf);
	}

	/* read more data into the buffer */
	i = read(cp->fdi, cp->buf + cp->nbuf, cp->lbuf - cp->nbuf);
	if (i <= 0)
		goto cli_close;
	cp->nbuf += i;

	for (p = q = cp->buf; q < cp->buf + cp->nbuf; ++q) {
		if (*q != '\n')
			continue;
		/* got a newline == got a command */
		*q = '\0';
		vsb_clear(cp->cli->sb);
		cli_dispatch(cp->cli, cli_proto, p);
		vsb_finish(cp->cli->sb);

		/* send the result back */
		if (cli_writeres(cp->fdo, cp->cli))
			goto cli_close;

		/* ready for next command */
		p = q + 1;
	}

	/* see if there's any data left in the buffer */
	if (p != q) {
		assert(q == cp->buf + cp->nbuf);
		cp->nbuf -= (p - cp->buf);
		memmove(cp->buf, p, cp->nbuf);
	} else
		cp->nbuf = 0;
	return (0);

cli_close:
	vsb_delete(cp->cli->sb);
	free(cp->buf);
	(void)close(cp->fdi);
	if (cp->fdi == 0)
		assert(open("/dev/null", O_RDONLY) == 0);
	(void)close(cp->fdo);
	if (cp->fdo == 1) {
		assert(open("/dev/null", O_WRONLY) == 1);
		close(2);
		assert(open("/dev/null", O_WRONLY) == 2);
	}
	free(cp);
	return (1);
}

void
mgt_cli_setup(int fdi, int fdo, int verbose)
{
	struct cli_port *cp;

	cp = calloc(sizeof *cp, 1);
	XXXAN(cp);

	sprintf(cp->name, "cli %d->%d", fdi, fdo);
	cp->magic = CLI_PORT_MAGIC;

	cp->fdi = fdi;
	cp->fdo = fdo;
	cp->verbose = verbose;

	cp->lbuf = 4096;
	cp->buf = malloc(cp->lbuf);
	XXXAN(cp->buf);

	cp->cli->sb = vsb_new(NULL, NULL, 0, VSB_AUTOEXTEND);
	XXXAN(cp->cli->sb);

	cp->ev = calloc(sizeof *cp->ev, 1);
	cp->ev->name = cp->name;
	cp->ev->fd = fdi;
	cp->ev->fd_flags = EV_RD;
	cp->ev->callback = mgt_cli_callback;
	cp->ev->priv = cp;
	ev_add(mgt_evb, cp->ev);
}

static int
telnet_accept(const struct ev *ev, int what)
{
	struct sockaddr_storage addr;
	socklen_t addrlen;
	int i;

	(void)what;
	addrlen = sizeof addr;
	i = accept(ev->fd, (void *)&addr, &addrlen);
	if (i < 0)
		return (0);

	mgt_cli_setup(i, i, 0);
	return (0);
}

int
mgt_cli_telnet(const char *T_arg)
{
	struct vss_addr **ta;
	char *addr, *port;
	int i, n;

	XXXAZ(VSS_parse(T_arg, &addr, &port));
	XXXAN(n = VSS_resolve(addr, port, &ta));
	free(addr);
	free(port);
	if (n == 0) {
		fprintf(stderr, "Could not open management port\n");
		exit(2);
	}
	for (i = 0; i < n; ++i) {
		int sock = VSS_listen(ta[i], 1);
		struct ev *ev = ev_new();
		XXXAN(ev);
		ev->fd = sock;
		ev->fd_flags = POLLIN;
		ev->callback = telnet_accept;
		ev_add(mgt_evb, ev);
		free(ta[i]);
		ta[i] = NULL;
	}
	free(ta);
	return (0);
}
