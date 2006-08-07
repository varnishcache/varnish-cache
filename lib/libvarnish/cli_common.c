/*
 * $Id: cli_event.c 466 2006-07-12 23:30:49Z phk $
 */

#include <errno.h>
#include <assert.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/uio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>

#include <sys/wait.h>

#include "sbuf.h"

#include "cli.h"
#include "cli_priv.h"
#include "cli_common.h"

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
	char res[CLI_LINE0_LEN + 2];	/*
					 * NUL + one more so we can catch
					 * any misformats by snprintf
					 */

	i = snprintf(res, sizeof res,
	    "%-3d %-8d\n", cli->result, sbuf_len(cli->sb));
	assert(i == CLI_LINE0_LEN);
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

static int
read_tmo(int fd, void *ptr, unsigned len, double tmo)
{
	int i;
	struct pollfd pfd;

	pfd.fd = fd;
	pfd.events = POLLIN;
	i = poll(&pfd, 1, (int)(tmo * 1e3));
	if (i == 0) {
		errno = ETIMEDOUT;
		return (-1);
	}
	return (read(fd, ptr, len));
}

int
cli_readres(int fd, unsigned *status, char **ptr, double tmo)
{
	char res[CLI_LINE0_LEN];	/* For NUL */
	int i, j;
	unsigned u, v;
	char *p;

	i = read_tmo(fd, res, CLI_LINE0_LEN, tmo);
	if (i < 0)
		return (i);
	assert(i == CLI_LINE0_LEN);	/* XXX: handle */
	assert(res[3] == ' ');
	assert(res[CLI_LINE0_LEN - 1] == '\n');
	j = sscanf(res, "%u %u\n", &u, &v);
	assert(j == 2);
	if (status != NULL)
		*status = u;
	p = malloc(v + 1);
	assert(p != NULL);
	i = read_tmo(fd, p, v + 1, tmo);
	if (i < 0) {
		free(p);
		return (i);
	}
	assert(i == v + 1);
	assert(p[v] == '\n');
	p[v] = '\0';
	if (ptr == NULL)
		free(p);
	else
		*ptr = p;
	return (0);
}

/*--------------------------------------------------------------------*/

void
cli_func_ping(struct cli *cli, char **av, void *priv)
{
	time_t t;

	(void)priv;
	(void)av;
	t = time(NULL);
	cli_out(cli, "PONG %ld", t);
}
