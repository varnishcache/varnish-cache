/*
 * $Id$
 */

#include <errno.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#include <sys/wait.h>

#include <event.h>
#include <sbuf.h>

#include <cli.h>
#include <cli_priv.h>
#include <libvarnish.h>

#include "heritage.h"
#include "cli_event.h"

void
cli_out(struct cli *cli, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	sbuf_vprintf(cli->sb, fmt, ap);
	va_end(ap);
}

void
cli_param(struct cli *cli)
{

	cli->result = CLIS_PARAM;
	cli_out(cli, "Parameter error, use \"help [command]\" for more info.\n");
}

void
cli_result(struct cli *cli, unsigned res)
{

	cli->result = res;
}

static void
encode_output(struct cli *cli)
{
	char *p, *q;

	if (cli->verbose) {
		if (cli->result != CLIS_OK)
			evbuffer_add_printf(cli->bev1->output, "ERROR %d ",
			    cli->result);
		evbuffer_add(cli->bev1->output,
		    sbuf_data(cli->sb), sbuf_len(cli->sb));
		if (cli->result == CLIS_OK)
			evbuffer_add_printf(cli->bev1->output, "OK\n");
		return;
	}
	evbuffer_add_printf(cli->bev1->output, "%d \"", cli->result);
	for (p = q = sbuf_data(cli->sb); *p != '\0'; p++) {
		if ((*p != '"' && *p != '\\' && isgraph(*p)) || *p == ' ')
			continue;
		if (p != q) 
			evbuffer_add(cli->bev1->output, q, p - q);
		if (*p == '\n')
			evbuffer_add_printf(cli->bev1->output, "\\n");
		else
			evbuffer_add_printf(cli->bev1->output, "\\x%02x", *p);
		q = p + 1;
	}
	if (p != q) 
		evbuffer_add(cli->bev1->output, q, p - q);
	evbuffer_add_printf(cli->bev1->output, "\"\n");
}

static void
rdcb(struct bufferevent *bev, void *arg)
{
	const char *p;
	struct cli *cli = arg;

	p = evbuffer_readline(bev->input);
	if (p == NULL)
		return;
	sbuf_clear(cli->sb);
	cli_dispatch(cli, cli->cli_proto, p);
	sbuf_finish(cli->sb);
	/* XXX: syslog results ? */
	encode_output(cli);
	bufferevent_enable(cli->bev1, EV_WRITE);
}

static void
wrcb(struct bufferevent *bev, void *arg)
{
	struct cli *cli = arg;

	bufferevent_disable(cli->bev1, EV_WRITE);
}

static void
excb(struct bufferevent *bev, short what, void *arg)
{
	printf("Exception\n");
}

struct cli *
cli_setup(int fdr, int fdw, int ver, struct cli_proto *cli_proto)
{
	struct cli	*cli;

	cli = calloc(sizeof *cli, 1);
	assert(cli != NULL);

	cli->bev0 = bufferevent_new(fdr, rdcb, wrcb, excb, cli);
	assert(cli->bev0 != NULL);
	if (fdr == fdw)
		cli->bev1 = cli->bev0;
	else 
		cli->bev1 = bufferevent_new(fdw, rdcb, wrcb, excb, cli);
	assert(cli->bev1 != NULL);
	cli->sb = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
	assert(cli->sb != NULL);

	cli->verbose = ver;
	cli->cli_proto = cli_proto;
	
	bufferevent_enable(cli->bev0, EV_READ);
	return (cli);
}
