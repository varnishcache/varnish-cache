/*
 * $Id$
 *
 * The mechanics of handling the child process
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <err.h>		/* XXX */

#ifndef HAVE_SETPROCTITLE
#include "compat/setproctitle.h"
#endif

#include "heritage.h"
#include "mgt.h"
#include "cli.h"
#include "cli_priv.h"
#include "mgt_cli.h"
#include "mgt_event.h"

pid_t		mgt_pid;
pid_t		child_pid = -1;

static int		child_fds[2];
static enum {
	CH_STOPPED = 0,
	CH_STARTING = 1,
	CH_RUNNING = 2,
	CH_STOPPING = 3,
	CH_DIED = 4
}			child_state = CH_STOPPED;

const char *ch_state[] = {
	[CH_STOPPED] =	"stopped",
	[CH_STARTING] =	"starting",
	[CH_RUNNING] =	"running",
	[CH_STOPPING] =	"stopping",
	[CH_DIED] =	"died, (restarting)",
};

struct evbase		*mgt_evb;
struct ev		*ev_poker;
struct ev		*ev_listen;

/*--------------------------------------------------------------------*/

static int
child_listener(struct ev *e, int what)
{
	int i;
	char buf[BUFSIZ];

	(void)e;
	if ((what & ~EV_RD)) {
		ev_listen = NULL;
		return (1);
	}
	i = read(child_fds[0], buf, sizeof buf - 1);
	if (i <= 0) {
		ev_listen = NULL;
		return (1);
	}
	buf[i] = '\0';
	fprintf(stderr, "Child said (%d, %d): <<%s>>\n", child_state, child_pid, buf);
	return (0);
}

/*--------------------------------------------------------------------*/

static int
child_poker(struct ev *e, int what)
{

	(void)e;
	(void)what;
	if (child_state != CH_RUNNING)
		return (1);
	if (child_pid > 0 && mgt_cli_askchild(NULL, NULL, "ping\n"))
		kill(child_pid, SIGKILL);
	return (0);
}

/*--------------------------------------------------------------------*/

static void
start_child(void)
{
	int i;
	char *p;
	struct ev *e;

	if (child_state != CH_STOPPED && child_state != CH_DIED)
		return;

	child_state = CH_STARTING;

	AZ(pipe(&heritage.fds[0]));
	AZ(pipe(&heritage.fds[2]));
	AZ(pipe(child_fds));
	i = fork();
	if (i < 0) 
		errx(1, "Could not fork child");
	if (i == 0) {
		/* Redirect stdin/out/err */
		AZ(close(0));
		i = open("/dev/null", O_RDONLY);
		assert(i == 0);
		assert(dup2(child_fds[1], 1) == 1);
		assert(dup2(child_fds[1], 2) == 2);
		AZ(close(child_fds[0]));
		AZ(close(child_fds[1]));

		AZ(close(heritage.fds[0]));
		AZ(close(heritage.fds[3]));

		setproctitle("Varnish-Chld");

		signal(SIGINT, SIG_DFL);
		signal(SIGTERM, SIG_DFL);
		child_main();

		exit (1);
	}

	fprintf(stderr, "start child pid %d\n", i);

	AZ(close(child_fds[1]));
	child_fds[1] = -1;

	assert(ev_listen == NULL);
	e = ev_new();
	assert(e != NULL);
	e->fd = child_fds[0];
	e->fd_flags = EV_RD;
	e->name = "Child listener";
	e->callback = child_listener;
	AZ(ev_add(mgt_evb, e));
	ev_listen = e;

	assert(ev_poker == NULL);
	e = ev_new();
	assert(e != NULL);
	e->timeout = 3.0;
	e->callback = child_poker;
	e->name = "child poker";
	AZ(ev_add(mgt_evb, e));
	ev_poker = e;

	mgt_cli_start_child(heritage.fds[0], heritage.fds[3]);
	AZ(close(heritage.fds[1]));
	heritage.fds[1] = -1;
	AZ(close(heritage.fds[2]));
	heritage.fds[2] = -1;
	child_pid = i;
	if (mgt_push_vcls_and_start(&i, &p)) {
		fprintf(stderr, "Pushing vcls failed:\n%s\n", p);
		free(p);
		exit (2);
	}
	child_state = CH_RUNNING;
}

/*--------------------------------------------------------------------*/

static void
stop_child(void)
{

	if (child_state != CH_RUNNING)
		return;

	child_state = CH_STOPPING;

	if (ev_poker != NULL) {
		ev_del(mgt_evb, ev_poker);
		free(ev_poker);
	}
	ev_poker = NULL;

	fprintf(stderr, "Clean child\n");
	mgt_cli_stop_child();

	/* We tell the child to die gracefully by closing the CLI */
	AZ(close(heritage.fds[0]));
	heritage.fds[0] = -1;
	AZ(close(heritage.fds[3]));
	heritage.fds[3] = -1;

	fprintf(stderr, "Child stopping\n");
}

/*--------------------------------------------------------------------*/

static int
mgt_sigchld(struct ev *e, int what)
{
	int status;
	pid_t r;

	(void)e;
	(void)what;

	if (ev_poker != NULL) {
		ev_del(mgt_evb, ev_poker);
		free(ev_poker);
	}
	ev_poker = NULL;

	r = wait4(-1, &status, WNOHANG, NULL);
	if (r != child_pid) {
		fprintf(stderr, "Unknown child died pid=%d status=0x%x\n",
		    r, status);
		return (0);
	}
	fprintf(stderr, "Cache child died pid=%d status=0x%x\n", r, status);
	child_pid = -1;

	if (child_state == CH_RUNNING) {
		child_state = CH_DIED;
		fprintf(stderr, "Clean child\n");
		mgt_cli_stop_child();

		/* We tell the child to die gracefully by closing the CLI */
		AZ(close(heritage.fds[0]));
		heritage.fds[0] = -1;
		AZ(close(heritage.fds[3]));
		heritage.fds[3] = -1;
	}

	if (ev_listen != NULL) {
		ev_del(mgt_evb, ev_listen);
		free(ev_listen);
	}
	ev_listen = NULL;

	AZ(close(child_fds[0]));
	child_fds[0] = -1;
	fprintf(stderr, "Child cleaned\n");

	if (child_state == CH_DIED)
		start_child();
	else if (child_state == CH_STOPPING)
		child_state = CH_STOPPED;
	return (0);
}

/*--------------------------------------------------------------------*/

static int
mgt_sigint(struct ev *e, int what)
{

	(void)e;
	(void)what;
	fprintf(stderr, "Manager got SIGINT\n");
	fflush(stdout);
	if (child_pid >= 0)
		stop_child();
	exit (2);
}

/*--------------------------------------------------------------------
 * This thread is the master thread in the management process.
 * The relatively simple task is to start and stop the child process
 * and to reincarnate it in case of trouble.
 */

void
mgt_run(int dflag)
{
	struct sigaction sac;
	struct ev *e;
	int i;

	mgt_pid = getpid();

	mgt_evb = ev_new_base();
	assert(mgt_evb != NULL);

	if (dflag)
		mgt_cli_setup(0, 1, 1);

	e = ev_new();
	assert(e != NULL);
	e->sig = SIGTERM;
	e->callback = mgt_sigint;
	e->name = "mgt_sigterm";
	AZ(ev_add(mgt_evb, e));

	e = ev_new();
	assert(e != NULL);
	e->sig = SIGINT;
	e->callback = mgt_sigint;
	e->name = "mgt_sigint";
	AZ(ev_add(mgt_evb, e));

	e = ev_new();
	assert(e != NULL);
	e->sig = SIGCHLD;
	e->sig_flags = SA_NOCLDSTOP;
	e->callback = mgt_sigchld;
	e->name = "mgt_sigchild";
	AZ(ev_add(mgt_evb, e));

	setproctitle("Varnish-Mgr");

	sac.sa_handler = SIG_IGN;
	sac.sa_flags = SA_RESTART;

	AZ(sigaction(SIGPIPE, &sac, NULL));
	AZ(sigaction(SIGHUP, &sac, NULL));

	printf("rolling(1)...\n");
	fprintf(stderr, "rolling(2)...\n");
	if (!dflag)
		start_child();

	i = ev_schedule(mgt_evb);
	fprintf(stderr, "ev_schedule = %d\n", i);

	fprintf(stderr, "manager dies\n");
	exit(2);
}

/*--------------------------------------------------------------------*/

void
mcf_server_startstop(struct cli *cli, char **av, void *priv)
{

	(void)cli;
	(void)av;
	if (priv != NULL && child_state == CH_RUNNING)
		stop_child();
	else if (priv == NULL && child_state == CH_STOPPED)
		start_child();
	else {
		cli_result(cli, CLIS_CANT);
		cli_out(cli, "Child in state %s", ch_state[child_state]);
	}
}
