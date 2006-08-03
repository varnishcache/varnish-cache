/*
 * $Id: cli_event.c 466 2006-07-12 23:30:49Z phk $
 */

#include <errno.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/uio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#include <sys/wait.h>

#include "sbuf.h"

#include "cli.h"
#include "cli_priv.h"
#include "common_cli.h"
#include "libvarnish.h"

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

int
cli_writeres(int fd, struct cli *cli)
{
	int i, l;
	struct iovec iov[3];
	char res[32];

	sprintf(res, "%d %d\n", cli->result, sbuf_len(cli->sb));
	iov[0].iov_base = (void*)(uintptr_t)res;
	iov[1].iov_base = (void*)(uintptr_t)sbuf_data(cli->sb);
	iov[2].iov_base = (void*)(uintptr_t)"\n";
	for (l = i = 0; i < 3; i++) {
		iov[i].iov_len = strlen(iov[i].iov_base);
		l += iov[i].iov_len;
	}
	i = writev(fd, iov, 3);
	return (i != l);
}
