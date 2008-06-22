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
 * The mechanics of handling the child process
 */

#include "config.h"

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>

#ifndef HAVE_SETPROCTITLE
#include "compat/setproctitle.h"
#endif

#include "heritage.h"
#include "mgt.h"
#include "cli.h"
#include "cli_priv.h"
#include "mgt_cli.h"
#include "mgt_event.h"
#include "vlu.h"
#include "vss.h"
#include "vbm.h"

pid_t		mgt_pid;
pid_t		child_pid = -1;

static struct vbitmap	*fd_map;

static int		child_cli_in = -1;
static int		child_cli_out = -1;
static int		child_output = -1;

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
static struct vlu	*vlu;

/*--------------------------------------------------------------------
 * Keep track of which filedescriptors the child should inherit and
 * which should be closed after fork()
 */

void
mgt_child_inherit(int fd, const char *what)
{

	printf("Inherit %d %s\n", fd, what);
	assert(fd >= 0);
	if(fd_map == NULL)
		fd_map = vbit_init(128);
	AN(fd_map);
	if (what != NULL)
		vbit_set(fd_map, fd);
	else
		vbit_clr(fd_map, fd);
}

/*--------------------------------------------------------------------*/

static int
child_line(void *priv, const char *p)
{
	(void)priv;

	fprintf(stderr, "Child said (%d, %d): <<%s>>\n",
	    child_state, child_pid, p);
	syslog(LOG_NOTICE, "Child (%d) said <<%s>>", child_pid, p);
	return (0);
}

/*--------------------------------------------------------------------*/

static int
child_listener(const struct ev *e, int what)
{

	(void)e;
	if ((what & ~EV_RD)) {
		ev_listen = NULL;
		return (1);
	}
	if (VLU_Fd(child_output, vlu)) {
		ev_listen = NULL;
		return (1);
	}
	return (0);
}

/*--------------------------------------------------------------------*/

static int
child_poker(const struct ev *e, int what)
{

	(void)e;
	(void)what;
	if (child_state != CH_RUNNING)
		return (1);
	if (child_pid > 0 && mgt_cli_askchild(NULL, NULL, "ping\n")) {
		fprintf(stderr, "Child not responding to ping\n");
		(void)kill(child_pid, SIGKILL);
	}
	return (0);
}

/*--------------------------------------------------------------------*/

static int
open_sockets(void)
{
	struct listen_sock *ls, *ls2;
	int good = 0;

	VTAILQ_FOREACH_SAFE(ls, &heritage.socks, list, ls2) {
		if (ls->sock >= 0) {
			good++;
			continue;
		}
		ls->sock = VSS_bind(ls->addr);
		if (ls->sock < 0) 
			continue;

		mgt_child_inherit(ls->sock, "sock");

		/*
		 * Set nonblocking mode to avoid a race where a client
		 * closes before we call accept(2) and nobody else are in
		 * the listen queue to release us.
		 */
		TCP_nonblocking(ls->sock);
		(void)TCP_filter_http(ls->sock);
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

	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		if (ls->sock < 0)
			continue;
		mgt_child_inherit(ls->sock, NULL);
		AZ(close(ls->sock));
		ls->sock = -1;
	}
}

/*--------------------------------------------------------------------*/

static void
start_child(void)
{
	pid_t pid;
	unsigned u;
	char *p;
	struct ev *e;
	int i, cp[2];

	if (child_state != CH_STOPPED && child_state != CH_DIED)
		return;

	if (open_sockets() != 0) {
		child_state = CH_STOPPED;
		return;	/* XXX ?? */
	}

	child_state = CH_STARTING;

	/* Open pipe for mgr->child CLI */
	AZ(pipe(cp));
	heritage.cli_in = cp[0];
	mgt_child_inherit(heritage.cli_in, "cli_in");
	child_cli_out = cp[1];

	/* Open pipe for child->mgr CLI */
	AZ(pipe(cp));
	heritage.cli_out = cp[1];
	mgt_child_inherit(heritage.cli_out, "cli_out");
	child_cli_in = cp[0];

	/*
	 * Open pipe for child stdout/err 
	 * NB: not inherited, because we dup2() it to stdout/stderr in child
	 */
	AZ(pipe(cp));
	heritage.std_fd = cp[1];
	child_output = cp[0];

	MCF_ParamSync();
	if ((pid = fork()) < 0) {
		perror("Could not fork child");
		exit(1);
	}
	if (pid == 0) {
		if (geteuid() == 0) {
			XXXAZ(setgid(params->gid));
			XXXAZ(setuid(params->uid));
		}

		/* Redirect stdin/out/err */
		AZ(close(STDIN_FILENO));
		assert(open("/dev/null", O_RDONLY) == STDIN_FILENO);
		assert(dup2(heritage.std_fd, STDOUT_FILENO) == STDOUT_FILENO);
		assert(dup2(heritage.std_fd, STDERR_FILENO) == STDERR_FILENO);

		/* Close anything we shouldn't know about */
		closelog();
		printf("Closed fds:");
		for (i = STDERR_FILENO + 1; i < getdtablesize(); i++) {
			if (vbit_test(fd_map, i))
				continue;
			if (close(i) == 0)
				printf(" %d", i);
		}
		printf("\n");

		setproctitle("Varnish-Chld %s", heritage.name);

		(void)signal(SIGINT, SIG_DFL);
		(void)signal(SIGTERM, SIG_DFL);
		child_main();

		exit(1);
	}
	fprintf(stderr, "start child pid %jd\n", (intmax_t)pid);

	/* Close stuff the child got */
	AZ(close(heritage.std_fd));
	heritage.std_fd = -1;

	mgt_child_inherit(heritage.cli_in, NULL);
	AZ(close(heritage.cli_in));
	heritage.cli_in = -1;

	mgt_child_inherit(heritage.cli_out, NULL);
	AZ(close(heritage.cli_out));
	heritage.cli_out = -1;

	close_sockets();

	vlu = VLU_New(NULL, child_line, 0);
	AN(vlu);

	AZ(ev_listen);
	e = ev_new();
	XXXAN(e);
	e->fd = child_output;
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

	mgt_cli_start_child(child_cli_in, child_cli_out);
	child_pid = pid;
	if (mgt_push_vcls_and_start(&u, &p)) {
		fprintf(stderr, "Pushing vcls failed:\n%s\n", p);
		free(p);
		/* Pick up any stuff lingering on stdout/stderr */
		(void)child_listener(NULL, EV_RD);
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

	child_state = CH_STOPPING;

	if (ev_poker != NULL) {
		ev_del(mgt_evb, ev_poker);
		free(ev_poker);
	}
	ev_poker = NULL;

	fprintf(stderr, "Clean child\n");
	mgt_cli_stop_child();

	/* We tell the child to die gracefully by closing the CLI */
	AZ(close(child_cli_out));
	child_cli_out= -1;
	AZ(close(child_cli_in));
	child_cli_in = -1;

	fprintf(stderr, "Child stopping\n");
}

/*--------------------------------------------------------------------*/

static int
mgt_sigchld(const struct ev *e, int what)
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

	r = wait4(child_pid, &status, WNOHANG, NULL);
	if (r == 0 || (r == -1 && errno == ECHILD))
		return (0);
	assert(r == child_pid);
	fprintf(stderr, "Cache child died pid=%d status=0x%x\n", r, status);
	child_pid = -1;

	/* Pick up any stuff lingering on stdout/stderr */
	(void)child_listener(NULL, EV_RD);

	if (child_state == CH_RUNNING) {
		child_state = CH_DIED;
		fprintf(stderr, "Clean child\n");
		mgt_cli_stop_child();

		/* We tell the child to die gracefully by closing the CLI */
		AZ(close(child_cli_out));
		child_cli_out = -1;
		AZ(close(child_cli_in));
		child_cli_in = -1;
	}

	if (ev_listen != NULL) {
		ev_del(mgt_evb, ev_listen);
		free(ev_listen);
	}
	ev_listen = NULL;

	AZ(close(child_output));
	child_output = -1;
	fprintf(stderr, "Child cleaned\n");

	if (child_state == CH_DIED && params->auto_restart)
		start_child();
	else if (child_state == CH_DIED) {
		child_state = CH_STOPPED;
	} else if (child_state == CH_STOPPING)
		child_state = CH_STOPPED;
	return (0);
}

/*--------------------------------------------------------------------*/

static int
mgt_sigint(const struct ev *e, int what)
{

	(void)e;
	(void)what;
	fprintf(stderr, "Manager got SIGINT\n");
	(void)fflush(stdout);
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
		mgt_cli_setup(0, 1, 1, "debug");

	if (T_arg)
		mgt_cli_telnet(dflag, T_arg);

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

	memset(&sac, 0, sizeof sac);
	sac.sa_handler = SIG_IGN;
	sac.sa_flags = SA_RESTART;

	AZ(sigaction(SIGPIPE, &sac, NULL));
	AZ(sigaction(SIGHUP, &sac, NULL));

	printf("rolling(1)...\n");
	fprintf(stderr, "rolling(2)...\n");
	if (!dflag && !mgt_has_vcl()) 
		fprintf(stderr, "No VCL loaded yet\n");
	else if (!dflag)
		start_child();
	else
		fprintf(stderr,
		    "Debugging mode, enter \"start\" to start child\n");

	i = ev_schedule(mgt_evb);
	fprintf(stderr, "ev_schedule = %d\n", i);

	fprintf(stderr, "manager dies\n");
	exit(2);
}

/*--------------------------------------------------------------------*/

void
mcf_server_startstop(struct cli *cli, const char * const *av, void *priv)
{

	(void)av;
	if (priv != NULL && child_state == CH_RUNNING)
		stop_child();
	else if (priv == NULL && child_state == CH_STOPPED) {
		if (mgt_has_vcl())
			start_child();
		else {
			cli_result(cli, CLIS_CANT);
			cli_out(cli, "No VCL available");
		}
	} else {
		cli_result(cli, CLIS_CANT);
		cli_out(cli, "Child in state %s", ch_state[child_state]);
	}
}

/*--------------------------------------------------------------------*/

void
mcf_server_status(struct cli *cli, const char * const *av, void *priv)
{
	(void)av;
	(void)priv;
	cli_out(cli, "Child in state %s", ch_state[child_state]);
}
