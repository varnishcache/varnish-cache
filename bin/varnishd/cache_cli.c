/*
 * $Id$
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

#include "shmlog.h"
#include "cli.h"
#include "cli_priv.h"
#include "cli_common.h"
#include "cache.h"
#include "vsb.h"
#include "heritage.h"

/*--------------------------------------------------------------------*/

static void
cli_func_start(struct cli *cli, char **av, void *priv)
{

	(void)cli;
	(void)av;
	(void)priv;
	VCA_Init();
	return;
}


/*--------------------------------------------------------------------*/

struct cli_proto CLI_cmds[] = {
	{ CLI_PING,		cli_func_ping },
	{ CLI_SERVER_START,	cli_func_start },
#if 0
	{ CLI_URL_QUERY,	cli_func_url_query },
#endif
	{ CLI_URL_PURGE,	cli_func_url_purge },
	{ CLI_VCL_LOAD,		cli_func_config_load },
	{ CLI_VCL_LIST,		cli_func_config_list },
	{ CLI_VCL_DISCARD,	cli_func_config_discard },
	{ CLI_VCL_USE,		cli_func_config_use },

	/* Undocumented */
	{ "dump.pool", "dump.pool",
	    "\tDump the worker thread pool state\n",
	    0, 0, cli_func_dump_pool },
	{ NULL }
};

void
CLI_Init(void)
{
	struct pollfd pfd[1];
	char *buf, *p;
	unsigned nbuf, lbuf;
	struct cli *cli, clis;
	int i;

	cli = &clis;
	memset(cli, 0, sizeof *cli);
	
	cli->sb = vsb_new(NULL, NULL, 0, VSB_AUTOEXTEND);
	XXXAN(cli->sb);
	lbuf = 4096;
	buf = malloc(lbuf);
	XXXAN(buf);
	nbuf = 0;
	while (1) {
		pfd[0].fd = heritage.fds[2];
		pfd[0].events = POLLIN;
		i = poll(pfd, 1, 5000);
		if (i == 0)
			continue;
		if ((nbuf + 2) >= lbuf) {
			lbuf += lbuf;
			buf = realloc(buf, lbuf);
			XXXAN(buf);
		}
		i = read(heritage.fds[2], buf + nbuf, lbuf - nbuf);
		if (i <= 0) {
			VSL(SLT_Error, 0, "CLI read %d (errno=%d)", i, errno);
			free(buf);
			return;
		}
		nbuf += i;
		p = strchr(buf, '\n');
		if (p == NULL)
			continue;
		*p = '\0';
		VSL(SLT_CLI, 0, "Rd %s", buf);
		vsb_clear(cli->sb);
		cli_dispatch(cli, CLI_cmds, buf);
		vsb_finish(cli->sb);
		i = cli_writeres(heritage.fds[1], cli);
		if (i) {
			VSL(SLT_Error, 0, "CLI write failed (errno=%d)", errno);
			free(buf);
			return;
		}
		VSL(SLT_CLI, 0, "Wr %d %d %s",
		    i, cli->result, vsb_data(cli->sb));
		i = ++p - buf; 
		assert(i <= nbuf);
		if (i < nbuf)
			memcpy(buf, p, nbuf - i);
		nbuf -= i;
	}
}
