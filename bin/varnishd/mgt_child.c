/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2007 Linpro AS
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
#include "vss.h"

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

static const char *ch_state[] = {
	[CH_STOPPED] =	"stopped",
	[CH_STARTING] =	"starting",
	[CH_RUNNING] =	"running",
	[CH_STOPPING] =	"stopping",
	[CH_DIED] =	"died, (restarting)",
};

struct evbase		*mgt_evb;
static struct ev	*ev_poker;
static struct ev	*ev_listen;

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
	fprintf(stderr, "Child said (%d, %d): <<%s>>\n",
	    child_state, child_pid, buf);
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
	if (child_pid > 0 && mgt_cli_askchild(NULL, NULL, "ping\n")) {
		fprintf(stderr, "Child not responding to ping\n");
		kill(child_pid, SIGKILL);
	}
	return (0);
}

/*--------------------------------------------------------------------*/

static int
open_sockets(void)
{
	struct listen_sock *ls, *ls2;
	int good = 0;

	TAILQ_FOREACH_SAFE(ls, &heritage.socks, list, ls2) {
		if (ls->sock >= 0)
			continue;
		ls->sock = VSS_listen(ls->addr, params->listen_depth);
		if (ls->sock < 0) {
			TAILQ_REMOVE(&heritage.socks, ls, list);
			free(ls);
			continue;
		}
		TCP_filter_http(ls->sock);
		good++;
	}
	if (!good)
		return (1);
	return (0);
}

/*--------------------------------------------------------------------*/

static void
close_sockets(void)
{
	struct listen_sock *ls;

	TAILQ_FOREACH(ls, &heritage.socks, list) {
		if (ls->sock < 0)
			continue;
		close(ls->sock);
		ls->sock = -1;
	}
}

/*--------------------------------------------------------------------*/

static void
start_child(void)
{
	int i;
	unsigned u;
	char *p;
	struct ev *e;

	if (child_state != CH_STOPPED && child_state != CH_DIED)
		return;

	if (open_sockets())
		return;	/* XXX ?? */

	child_state = CH_STARTING;

	AZ(pipe(&heritage.fds[0]));
	AZ(pipe(&heritage.fds[2]));
	AZ(pipe(child_fds));
	MCF_ParamSync();
	i = fork();
	if (i < 0)
		errx(1, "Could not fork child");
	if (i == 0) {
		if (geteuid() == 0) {
			XXXAZ(setgid(params->gid));
			XXXAZ(setuid(params->uid));
		}

		/* Redirect stdin/out/err */
		AZ(close(0));
		i = open("/dev/null", O_RDONLY);
		xxxassert(i == 0);
		assert(dup2(child_fds[1], 1) == 1);
		assert(dup2(child_fds[1], 2) == 2);
		AZ(close(child_fds[0]));
		AZ(close(child_fds[1]));

		AZ(close(heritage.fds[0]));
		AZ(close(heritage.fds[3]));

		setproctitle("Varnish-Chld %s", heritage.name);

		signal(SIGINT, SIG_DFL);
		signal(SIGTERM, SIG_DFL);
		child_main();

		exit (1);
	}

	fprintf(stderr, "start child pid %d\n", i);

	AZ(close(child_fds[1]));
	child_fds[1] = -1;

	AZ(ev_listen);
	e = ev_new();
	XXXAN(e);
	e->fd = child_fds[0];
	e->fd_flags = EV_RD;
	e->name = "Child listener";
	e->callback = child_listener;
	AZ(ev_add(mgt_evb, e));
	ev_listen = e;

	AZ(ev_poker);
	if (params->ping_interval > 0) {
		e = ev_new();
		XXXAN(e);
		e->timeout = params->ping_interval;
		e->callback = child_poker;
		e->name = "child poker";
		AZ(ev_add(mgt_evb, e));
		ev_poker = e;
	}

	mgt_cli_start_child(heritage.fds[0], heritage.fds[3]);
	AZ(close(heritage.fds[1]));
	heritage.fds[1] = -1;
	AZ(close(heritage.fds[2]));
	heritage.fds[2] = -1;
	child_pid = i;
	if (mgt_push_vcls_and_start(&u, &p)) {
		fprintf(stderr, "Pushing vcls failed:\n%s\n", p);
		free(p);
		/* Pick up any stuff lingering on stdout/stderr */
		child_listener(NULL, EV_RD);
		exit(2);
	}
	child_state = CH_RUNNING;
}

/*--------------------------------------------------------------------*/

static void
stop_child(void)
{

	if (child_state != CH_RUNNING)
		return;

	close_sockets();
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
	if (r != child_pid || r == -1) {
		fprintf(stderr, "Unknown child died pid=%d status=0x%x\n",
		    r, status);
		return (0);
	}
	fprintf(stderr, "Cache child died pid=%d status=0x%x\n", r, status);
	child_pid = -1;

	/* Pick up any stuff lingering on stdout/stderr */
	child_listener(NULL, EV_RD);

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

	if (child_state == CH_DIED && params->auto_restart)
		start_child();
	else if (child_state == CH_DIED) {
		close_sockets();
		child_state = CH_STOPPED;
	}
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
mgt_run(int dflag, const char *T_arg)
{
	struct sigaction sac;
	struct ev *e;
	int i;

	mgt_pid = getpid();

	mgt_evb = ev_new_base();
	XXXAN(mgt_evb);

	if (dflag)
		mgt_cli_setup(0, 1, 1);

	if (T_arg)
		mgt_cli_telnet(T_arg);

	e = ev_new();
	XXXAN(e);
	e->sig = SIGTERM;
	e->callback = mgt_sigint;
	e->name = "mgt_sigterm";
	AZ(ev_add(mgt_evb, e));

	e = ev_new();
	XXXAN(e);
	e->sig = SIGINT;
	e->callback = mgt_sigint;
	e->name = "mgt_sigint";
	AZ(ev_add(mgt_evb, e));

	e = ev_new();
	XXXAN(e);
	e->sig = SIGCHLD;
	e->sig_flags = SA_NOCLDSTOP;
	e->callback = mgt_sigchld;
	e->name = "mgt_sigchild";
	AZ(ev_add(mgt_evb, e));

	setproctitle("Varnish-Mgr %s", heritage.name);

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

/*--------------------------------------------------------------------*/

void
mcf_server_status(struct cli *cli, char **av, void *priv)
{
	(void)av;
	(void)priv;
	cli_out(cli, "Child is state %s", ch_state[child_state]);
}
