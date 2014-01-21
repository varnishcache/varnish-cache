/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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
 * The mechanics of handling the child process
 */

#include "config.h"

#include <sys/types.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "mgt/mgt.h"
#include "common/heritage.h"
#include "common/params.h"

#include "vbm.h"
#include "vcli.h"
#include "vcli_priv.h"
#include "vev.h"
#include "vlu.h"
#include "vss.h"
#include "vtim.h"

#include "mgt_cli.h"

pid_t			child_pid = -1;

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

static const char * const ch_state[] = {
	[CH_STOPPED] =	"stopped",
	[CH_STARTING] =	"starting",
	[CH_RUNNING] =	"running",
	[CH_STOPPING] =	"stopping",
	[CH_DIED] =	"died, (restarting)",
};

static struct vev	*ev_poker;
static struct vev	*ev_listen;
static struct vlu	*child_std_vlu;

static struct vsb *child_panic = NULL;
static double mgt_uptime_t0 = 0.;

/* XXX: Doesn't really belong here, but only place we use it */
static inline int
MGT_FEATURE(enum feature_bits x)
{
	return (mgt_param.feature_bits[(unsigned)x>>3] &
	    (0x80U >> ((unsigned)x & 7)));
}

static void mgt_reap_child(void);

/*---------------------------------------------------------------------
 * A handy little function
 */

static inline void
closex(int *fd)
{

	assert(*fd >= 0);
	AZ(close(*fd));
	*fd = -1;
}

/*=====================================================================
 * Panic string evacuation and handling
 */

static void
mgt_panic_record(pid_t r)
{
	char time_str[30];

	AN(heritage.panic_str[0]);
	REPORT(LOG_ERR, "Child (%jd) Panic message: %s",
	    (intmax_t)r, heritage.panic_str);

	if (child_panic != NULL)
		VSB_delete(child_panic);
	child_panic = VSB_new_auto();
	AN(child_panic);
	VTIM_format(VTIM_real(), time_str);
	VSB_printf(child_panic, "Last panic at: %s\n", time_str);
	VSB_cat(child_panic, heritage.panic_str);
	AZ(VSB_finish(child_panic));
}

static void
mgt_panic_clear(void)
{
	VSB_delete(child_panic);
	child_panic = NULL;
}

void __match_proto__(cli_func_t)
mcf_panic_show(struct cli *cli, const char * const *av, void *priv)
{
	(void)av;
	(void)priv;

	if (!child_panic) {
		VCLI_SetResult(cli, CLIS_CANT);
		VCLI_Out(cli,
		    "Child has not panicked or panic has been cleared");
		return;
	}

	VCLI_Out(cli, "%s\n", VSB_data(child_panic));
}

void __match_proto__(cli_func_t)
mcf_panic_clear(struct cli *cli, const char * const *av, void *priv)
{
	(void)av;
	(void)priv;

	if (child_panic == NULL) {
		VCLI_SetResult(cli, CLIS_CANT);
		VCLI_Out(cli, "No panic to clear");
		return;
	}
	mgt_panic_clear();
}

/*=====================================================================
 * Track the highest file descriptor the parent knows is being used.
 *
 * This allows the child process to clean/close only a small fraction
 * of the possible file descriptors after exec(2).
 *
 * This is likely to a bit on the low side, as libc and other libraries
 * has a tendency to cache file descriptors (syslog, resolver, etc.)
 * so we add a margin of 100 fds.
 */

static int		mgt_max_fd;

#define CLOSE_FD_UP_TO	(mgt_max_fd + 100)

void
mgt_got_fd(int fd)
{
	/*
	 * Assert > 0, to catch bogus opens, we know where stdin goes
	 * in the master process.
	 */
	assert(fd > 0);
	if (fd > mgt_max_fd)
		mgt_max_fd = fd;
}

/*--------------------------------------------------------------------
 * Keep track of which filedescriptors the child should inherit and
 * which should be closed after fork()
 */

void
mgt_child_inherit(int fd, const char *what)
{

	assert(fd >= 0);
	if (fd_map == NULL)
		fd_map = vbit_init(128);
	AN(fd_map);
	if (what != NULL)
		vbit_set(fd_map, fd);
	else
		vbit_clr(fd_map, fd);
}

/*=====================================================================
 * Open and close the accept sockets.
 *
 * (The child is priv-sep'ed, so it can't do it.)
 */

int
MGT_open_sockets(void)
{
	struct listen_sock *ls;
	int good = 0;

	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		if (ls->sock >= 0) {
			good++;
			continue;
		}
		ls->sock = VSS_bind(ls->addr);
		if (ls->sock < 0)
			continue;

		mgt_child_inherit(ls->sock, "sock");

		good++;
	}
	if (!good)
		return (1);
	return (0);
}

/*--------------------------------------------------------------------*/

void
MGT_close_sockets(void)
{
	struct listen_sock *ls;

	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		if (ls->sock < 0)
			continue;
		mgt_child_inherit(ls->sock, NULL);
		closex(&ls->sock);
	}
}

/*=====================================================================
 * Listen to stdout+stderr from the child
 */

static int
child_line(void *priv, const char *p)
{
	(void)priv;

	REPORT(LOG_NOTICE, "Child (%jd) said %s", (intmax_t)child_pid, p);
	return (0);
}

/*--------------------------------------------------------------------
 * NB: Notice cleanup call from mgt_reap_child()
 */

static int __match_proto__(vev_cb_f)
child_listener(const struct vev *e, int what)
{

	if ((what & ~EV_RD) || VLU_Fd(child_output, child_std_vlu)) {
		ev_listen = NULL;
		if (e != NULL)
			mgt_reap_child();
		return (1);
	}
	return (0);
}

/*=====================================================================
 * Periodically poke the child, to see that it still lives
 */

static int __match_proto__(vev_cb_f)
child_poker(const struct vev *e, int what)
{

	(void)e;
	(void)what;
	if (child_state != CH_RUNNING)
		return (1);
	if (child_pid < 0)
		return (0);
	if (!mgt_cli_askchild(NULL, NULL, "ping\n"))
		return (0);
	return (0);
}

/*=====================================================================
 * SIGSEGV handler for child process
 */

static void __match_proto__()
child_sigsegv_handler(int s, siginfo_t *si, void *c)
{
	char buf[1024];

	(void)s;
	(void)c;

	sprintf(buf, "Segmentation fault by instruction at %p", si->si_addr);
	VAS_Fail(__func__,
		 __FILE__,
		 __LINE__,
		 buf,
		 errno,
		 VAS_ASSERT);
}

/*=====================================================================
 * Launch the child process
 */

static void
mgt_launch_child(struct cli *cli)
{
	pid_t pid;
	unsigned u;
	char *p;
	struct vev *e;
	int i, cp[2];
	struct sigaction sa;

	if (child_state != CH_STOPPED && child_state != CH_DIED)
		return;

	if (MGT_open_sockets() != 0) {
		child_state = CH_STOPPED;
		if (cli != NULL) {
			VCLI_SetResult(cli, CLIS_CANT);
			VCLI_Out(cli, "Could not open sockets");
			return;
		}
		REPORT0(LOG_ERR,
		    "Child start failed: could not open sockets");
		return;
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

	AN(heritage.vsm);
	mgt_SHM_Size_Adjust();
	AN(heritage.vsm);
	AN(heritage.param);
	if ((pid = fork()) < 0) {
		/* XXX */
		perror("Could not fork child");
		exit(1);
	}
	if (pid == 0) {

		/* Redirect stdin/out/err */
		AZ(close(STDIN_FILENO));
		assert(open("/dev/null", O_RDONLY) == STDIN_FILENO);
		assert(dup2(heritage.std_fd, STDOUT_FILENO) == STDOUT_FILENO);
		assert(dup2(heritage.std_fd, STDERR_FILENO) == STDERR_FILENO);

		/* Close anything we shouldn't know about */
		closelog();

		for (i = STDERR_FILENO + 1; i < CLOSE_FD_UP_TO; i++) {
			if (vbit_test(fd_map, i))
				continue;
			(void)(close(i) == 0);
		}
#ifdef HAVE_SETPROCTITLE
		setproctitle("Varnish-Chld %s", heritage.name);
#endif

		if (mgt_param.sigsegv_handler) {
			memset(&sa, 0, sizeof sa);
			sa.sa_sigaction = child_sigsegv_handler;
			sa.sa_flags = SA_SIGINFO;
			(void)sigaction(SIGSEGV, &sa, NULL);
		}
		(void)signal(SIGINT, SIG_DFL);
		(void)signal(SIGTERM, SIG_DFL);

		mgt_sandbox(SANDBOX_WORKER);

		child_main();

		exit(1);
	}
	assert(pid > 1);
	REPORT(LOG_NOTICE, "child (%jd) Started", (intmax_t)pid);
	VSC_C_mgt->child_start = ++static_VSC_C_mgt.child_start;

	/* Close stuff the child got */
	closex(&heritage.std_fd);

	mgt_child_inherit(heritage.cli_in, NULL);
	closex(&heritage.cli_in);

	mgt_child_inherit(heritage.cli_out, NULL);
	closex(&heritage.cli_out);

	MGT_close_sockets();

	child_std_vlu = VLU_New(NULL, child_line, 0);
	AN(child_std_vlu);

	AZ(ev_listen);
	e = vev_new();
	XXXAN(e);
	e->fd = child_output;
	e->fd_flags = EV_RD;
	e->name = "Child listener";
	e->callback = child_listener;
	AZ(vev_add(mgt_evb, e));
	ev_listen = e;
	AZ(ev_poker);
	if (mgt_param.ping_interval > 0) {
		e = vev_new();
		XXXAN(e);
		e->timeout = mgt_param.ping_interval;
		e->callback = child_poker;
		e->name = "child poker";
		AZ(vev_add(mgt_evb, e));
		ev_poker = e;
	}

	mgt_cli_start_child(child_cli_in, child_cli_out);
	child_pid = pid;
	if (mgt_push_vcls_and_start(&u, &p)) {
		REPORT(LOG_ERR, "Pushing vcls failed:\n%s", p);
		free(p);
		child_state = CH_RUNNING;
		mgt_stop_child();
	} else
		child_state = CH_RUNNING;
}

/*=====================================================================
 * Cleanup when child dies.
 */

static void
mgt_reap_child(void)
{
	int i;
	int status;
	struct vsb *vsb;
	pid_t r;

	assert(child_pid != -1);

	/*
	 * Close the CLI connections
	 * This signals orderly shut down to child
	 */
	mgt_cli_stop_child();
	if (child_cli_out >= 0)
		closex(&child_cli_out);
	if (child_cli_in >= 0)
		closex(&child_cli_in);

	/* Stop the poker */
	if (ev_poker != NULL) {
		vev_del(mgt_evb, ev_poker);
		free(ev_poker);
	}
	ev_poker = NULL;

	/* Stop the listener */
	if (ev_listen != NULL) {
		vev_del(mgt_evb, ev_listen);
		free(ev_listen);
		ev_listen = NULL;
	}

	/* Wait 10 seconds for child to die */
	for (i = 0; i < 10; i++) {
		r = waitpid(child_pid, &status, WNOHANG);
		if (r == child_pid)
			break;
		(void)sleep(1);
	}
	if (r == 0) {
		/* Kick it Jim... */
		if (MGT_FEATURE(FEATURE_NO_COREDUMP))
			(void)kill(child_pid, SIGKILL);
		else
			(void)kill(child_pid, SIGQUIT);
		r = waitpid(child_pid, &status, 0);
	}
	if (r != child_pid)
		fprintf(stderr, "WAIT 0x%jx\n", (uintmax_t)r);
	assert(r == child_pid);

	/* Compose obituary */
	vsb = VSB_new_auto();
	XXXAN(vsb);
	VSB_printf(vsb, "Child (%ld) %s", (long)r, status ? "died" : "ended");
	if (WIFEXITED(status) && WEXITSTATUS(status)) {
		VSB_printf(vsb, " status=%d", WEXITSTATUS(status));
		exit_status |= 0x20;
		if (WEXITSTATUS(status) == 1)
			VSC_C_mgt->child_exit = ++static_VSC_C_mgt.child_exit;
		else
			VSC_C_mgt->child_stop = ++static_VSC_C_mgt.child_stop;
	}
	if (WIFSIGNALED(status)) {
		VSB_printf(vsb, " signal=%d", WTERMSIG(status));
		exit_status |= 0x40;
		VSC_C_mgt->child_died = ++static_VSC_C_mgt.child_died;
	}
#ifdef WCOREDUMP
	if (WCOREDUMP(status)) {
		VSB_printf(vsb, " (core dumped)");
		exit_status |= 0x80;
		VSC_C_mgt->child_dump = ++static_VSC_C_mgt.child_dump;
	}
#endif
	AZ(VSB_finish(vsb));
	REPORT(LOG_INFO, "%s", VSB_data(vsb));
	VSB_delete(vsb);

	/* Dispose of shared memory but evacuate panic messages first */
	if (heritage.panic_str[0] != '\0') {
		mgt_panic_record(r);
		mgt_SHM_Destroy(1);
		VSC_C_mgt->child_panic = ++static_VSC_C_mgt.child_panic;
	} else {
		mgt_SHM_Destroy(0);
	}
	mgt_SHM_Create();

	if (child_state == CH_RUNNING)
		child_state = CH_DIED;

	/* Pick up any stuff lingering on stdout/stderr */
	(void)child_listener(NULL, EV_RD);
	closex(&child_output);
	VLU_Destroy(child_std_vlu);

	child_pid = -1;

	REPORT0(LOG_DEBUG, "Child cleanup complete");

	if (child_state == CH_DIED && mgt_param.auto_restart)
		mgt_launch_child(NULL);
	else if (child_state == CH_DIED)
		child_state = CH_STOPPED;
	else if (child_state == CH_STOPPING)
		child_state = CH_STOPPED;
}

/*=====================================================================
 * If CLI communications with the child process fails, there is nothing
 * for us to do but to drag it behind the barn and get it over with.
 *
 * The typical case is where the child process fails to return a reply
 * before the cli_timeout expires.  This invalidates the CLI pipes for
 * all future use, as we don't know if the child was just slow and the
 * result gets piped later on, or if the child is catatonic.
 */

void
MGT_Child_Cli_Fail(void)
{

	if (child_state != CH_RUNNING)
		return;
	if (child_pid < 0)
		return;
	REPORT(LOG_ERR, "Child (%jd) not responding to CLI, killing it.",
	    (intmax_t)child_pid);
	if (MGT_FEATURE(FEATURE_NO_COREDUMP))
		(void)kill(child_pid, SIGKILL);
	else
		(void)kill(child_pid, SIGQUIT);
}

/*=====================================================================
 * Controlled stop of child process
 *
 * Reaping the child asks for orderly shutdown
 */

void
mgt_stop_child(void)
{

	if (child_state != CH_RUNNING)
		return;

	child_state = CH_STOPPING;

	REPORT0(LOG_DEBUG, "Stopping Child");

	mgt_reap_child();
}

/*=====================================================================
 * CLI command to start/stop child
 */

void __match_proto__(cli_func_t)
mcf_server_startstop(struct cli *cli, const char * const *av, void *priv)
{

	(void)av;
	if (priv != NULL && child_state == CH_RUNNING)
		mgt_stop_child();
	else if (priv == NULL && child_state == CH_STOPPED) {
		if (mgt_has_vcl()) {
			mgt_launch_child(cli);
		} else {
			VCLI_SetResult(cli, CLIS_CANT);
			VCLI_Out(cli, "No VCL available");
		}
	} else {
		VCLI_SetResult(cli, CLIS_CANT);
		VCLI_Out(cli, "Child in state %s", ch_state[child_state]);
	}
}

/*--------------------------------------------------------------------*/

void
mcf_server_status(struct cli *cli, const char * const *av, void *priv)
{
	(void)av;
	(void)priv;
	VCLI_Out(cli, "Child in state %s", ch_state[child_state]);
}

/*--------------------------------------------------------------------*/

static int __match_proto__(vev_cb_f)
mgt_sigint(const struct vev *e, int what)
{

	(void)e;
	(void)what;
	REPORT0(LOG_ERR, "Manager got SIGINT");
	(void)fflush(stdout);
	if (child_pid >= 0)
		mgt_stop_child();
	exit (2);
}

/*--------------------------------------------------------------------*/

static int __match_proto__(vev_cb_f)
mgt_uptime(const struct vev *e, int what)
{

	(void)e;
	(void)what;
	AN(VSC_C_mgt);
	VSC_C_mgt->uptime = static_VSC_C_mgt.uptime =
	    VTIM_real() - mgt_uptime_t0;
	if (heritage.vsm != NULL)
		VSM_common_ageupdate(heritage.vsm);
	return (0);
}

/*=====================================================================
 * This thread is the master thread in the management process.
 * The relatively simple task is to start and stop the child process
 * and to reincarnate it in case of trouble.
 */

void
MGT_Run(void)
{
	struct sigaction sac;
	struct vev *e;
	int i;

	mgt_uptime_t0 = VTIM_real();
	e = vev_new();
	XXXAN(e);
	e->callback = mgt_uptime;
	e->timeout = 1.0;
	e->name = "mgt_uptime";
	AZ(vev_add(mgt_evb, e));

	e = vev_new();
	XXXAN(e);
	e->sig = SIGTERM;
	e->callback = mgt_sigint;
	e->name = "mgt_sigterm";
	AZ(vev_add(mgt_evb, e));

	e = vev_new();
	XXXAN(e);
	e->sig = SIGINT;
	e->callback = mgt_sigint;
	e->name = "mgt_sigint";
	AZ(vev_add(mgt_evb, e));

#ifdef HAVE_SETPROCTITLE
	setproctitle("Varnish-Mgr %s", heritage.name);
#endif

	memset(&sac, 0, sizeof sac);
	sac.sa_handler = SIG_IGN;
	sac.sa_flags = SA_RESTART;

	AZ(sigaction(SIGPIPE, &sac, NULL));
	AZ(sigaction(SIGHUP, &sac, NULL));

	if (!d_flag && !mgt_has_vcl())
		REPORT0(LOG_ERR, "No VCL loaded yet");
	else if (!d_flag) {
		mgt_launch_child(NULL);
		if (child_state == CH_STOPPED) {
			exit_status = 2;
			return;
		}
	}

	i = vev_schedule(mgt_evb);
	if (i != 0)
		REPORT(LOG_ERR, "vev_schedule() = %d", i);

	REPORT0(LOG_ERR, "manager dies");
}
