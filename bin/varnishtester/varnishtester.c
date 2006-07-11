
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#include "libvarnish.h"
#include "event.h"


/*--------------------------------------------------------------------
 * stubs to survice linkage
 */
 
#include "cli_priv.h"

struct cli;

void cli_out(struct cli *cli, const char *fmt, ...) { (void)cli; (void)fmt; abort(); }
void cli_param(struct cli *cli) { (void)cli; abort(); }
void cli_result(struct cli *cli, unsigned res) { (void)cli; (void)res; abort(); }

/*--------------------------------------------------------------------*/

static struct event_base *eb;
static struct bufferevent *e_cmd;
static struct bufferevent *e_pipe2;
static int pipe1[2];
static int pipe2[2];
static pid_t child;

/*--------------------------------------------------------------------*/

static void
rd_pipe2(struct bufferevent *bev, void *arg)
{
	char *p;

	(void)arg;
	p = evbuffer_readline(bev->input);
	if (p == NULL)
		return;
	printf("V: <<%s>>\n", p);
}

static void
ex_pipe2(struct bufferevent *bev, short what, void *arg)
{
	int i, status;

	(void)arg;
	printf("%s(%p, 0x%x, %p)\n", __func__, bev, what, arg);
	bufferevent_disable(e_pipe2, EV_READ);

	i = wait4(child, &status, 0, NULL);
	printf("stopped i %d status 0x%x\n", i, status);
	child = 0;
	close(pipe1[1]);
	close(pipe2[0]);
}

/*--------------------------------------------------------------------*/

static void
cmd_start(char **av)
{

	printf("%s()\n", __func__);
	(void)av;
	assert(pipe(pipe1) == 0);
	assert(pipe(pipe2) == 0);

	e_pipe2 = bufferevent_new(pipe2[0], rd_pipe2, NULL, ex_pipe2, NULL);
	assert(e_pipe2 != NULL);
	bufferevent_base_set(eb, e_pipe2);
	bufferevent_enable(e_pipe2, EV_READ);

	child = fork();
	if (!child) {
		dup2(pipe1[0], 0);
		dup2(pipe2[1], 1);
		dup2(pipe2[1], 2);
		close(pipe1[0]);
		close(pipe1[1]);
		close(pipe2[0]);
		close(pipe2[1]);
		write(2, "Forked\n", 7);
		assert(chdir("../varnishd") == 0);
		execl(
		    "./varnishd",
		    "varnishd",
		    "-blocalhost:8081",
		    "-sfile,/tmp/,10m",
		    NULL);
		perror("execl");
		exit (2);
	}
	close(pipe1[0]);
	close(pipe2[1]);
}


/*--------------------------------------------------------------------*/

static void
cmd_stop(char **av)
{

	(void)av;
	if (child == 0) {
		fprintf(stderr, "No child running\n");
		exit (2);
	}
	write(pipe1[1], "exit\r", 5);
	/* XXX: arm timeout */
}

/*--------------------------------------------------------------------*/

static void
rd_cmd(struct bufferevent *bev, void *arg)
{
	char *p;
	char **av;

	(void)bev;
	(void)arg;
	p = evbuffer_readline(bev->input);
	if (p == NULL)
		return;
	av = ParseArgv(p, 0);
	if (av[0] != NULL) {
		fprintf(stderr, "%s\n", av[0]);
		exit (1);
	}
	if (av[1] == NULL)
		return;
	if (!strcmp(av[1], "start"))
		cmd_start(av + 2);
	else if (!strcmp(av[1], "stop"))
		cmd_stop(av + 2);
	else {
		fprintf(stderr, "Unknown command \"%s\"\n", av[1]);
		exit (2);
	}
	FreeArgv(av);
}

/*--------------------------------------------------------------------*/

int
main(int argc, char **argv)
{

	setbuf(stdout, NULL);
	(void)argc;
	(void)argv;

	eb = event_init();
	assert(eb != NULL);

	e_cmd = bufferevent_new(0, rd_cmd, NULL, NULL, NULL);
	assert(e_cmd != NULL);
	bufferevent_base_set(eb, e_cmd);
	bufferevent_enable(e_cmd, EV_READ);

	event_base_loop(eb, 0);
	return (2);
}
