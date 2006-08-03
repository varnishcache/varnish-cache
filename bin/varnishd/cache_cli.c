/*
 * $Id$
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <pthread.h>

#include "event.h"		/* XXX only as long as it takes */

#include "libvarnish.h"
#include "shmlog.h"
#include "cli.h"
#include "cli_priv.h"
#include "cli_event.h"
#include "cache.h"
#include "sbuf.h"
#include "heritage.h"

/*--------------------------------------------------------------------*/

static void
cli_func_ping(struct cli *cli, char **av, void *priv)
{
	time_t t;

	(void)priv;
#if 0
	arm_keepalive();
#endif
	if (av[2] != NULL) {
		/* XXX: check clock skew is pointless here */
	}
	t = time(NULL);
	cli_out(cli, "PONG %ld", t);
}

/*--------------------------------------------------------------------*/

struct cli_proto CLI_cmds[] = {
	{ CLI_PING,		cli_func_ping },
#if 0
	{ CLI_URL_QUERY,	cli_func_url_query },
#endif
	{ CLI_URL_PURGE,	cli_func_url_purge },
	{ CLI_CONFIG_LOAD,	cli_func_config_load },
	{ CLI_CONFIG_LIST,	cli_func_config_list },
	{ CLI_CONFIG_UNLOAD,	cli_func_config_unload },
	{ CLI_CONFIG_USE,	cli_func_config_use },
	{ NULL }
};

static int
cli_writes(const char *s, const char *r, const char *t)
{
	int i, l;
	struct iovec iov[3];

	iov[0].iov_base = (void*)(uintptr_t)s;
	iov[1].iov_base = (void*)(uintptr_t)r;
	iov[2].iov_base = (void*)(uintptr_t)t;
	for (l = i = 0; i < 3; i++) {
		iov[i].iov_len = strlen(iov[i].iov_base);
		l += iov[i].iov_len;
	}
	i = writev(heritage.fds[1], iov, 3);
	VSL(SLT_CLI, 0, "Wr %d %s %s", i != l, s, r);
	return (i != l);
}

void
CLI_Init(void)
{
	struct pollfd pfd[1];
	char *buf, *p;
	unsigned nbuf, lbuf;
	struct cli *cli, clis;
	int i;
	char res[30];

	cli = &clis;
	memset(cli, 0, sizeof *cli);
	
	cli->sb = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
	assert(cli->sb != NULL);
	lbuf = 4096;
	buf = malloc(lbuf);
	assert(buf != NULL);
	nbuf = 0;
	while (1) {
		pfd[0].fd = heritage.fds[2];
		pfd[0].events = POLLIN;
		i = poll(pfd, 1, 5000);
		if (i == 0)
			continue;
		if (nbuf == lbuf) {
			lbuf += lbuf;
			buf = realloc(buf, lbuf);
			assert(buf != NULL);
		}
		i = read(heritage.fds[2], buf + nbuf, lbuf - nbuf);
		if (i <= 0) {
			VSL(SLT_Error, 0, "CLI read %d (errno=%d)", i, errno);
			return;
		}
		nbuf += i;
		p = strchr(buf, '\n');
		if (p == NULL)
			continue;
		*p = '\0';
		VSL(SLT_CLI, 0, "Rd %s", buf);
		sbuf_clear(cli->sb);
		cli_dispatch(cli, CLI_cmds, buf);
		sbuf_finish(cli->sb);
		sprintf(res, "%d ", cli->result);
		if (cli_writes(res, sbuf_data(cli->sb), "\n")) {
			VSL(SLT_Error, 0, "CLI write failed (errno=%d)", errno);
			return;
		}
		i = ++p - buf; 
		assert(i <= nbuf);
		if (i < nbuf)
			memcpy(buf, p, nbuf - i);
		nbuf -= i;
	}
}
