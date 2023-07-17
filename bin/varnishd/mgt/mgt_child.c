/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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

#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "mgt.h"

#include "vapi/vsig.h"

#include "vbm.h"
#include "vcli_serve.h"
#include "vev.h"
#include "vfil.h"
#include "vlu.h"
#include "vtim.h"

#include "common/heritage.h"

static pid_t		child_pid = -1;

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

static void mgt_reap_child(void);
static int kill_child(void);

/*=====================================================================
 * Panic string evacuation and handling
 */

static void
mgt_panic_record(pid_t r)
{
	char time_str[30];

	if (child_panic != NULL)
		VSB_destroy(&child_panic);
	child_panic = VSB_new_auto();
	AN(child_panic);
	VTIM_format(VTIM_real(), time_str);
	VSB_printf(child_panic, "Panic at: %s\n", time_str);
	VSB_quote(child_panic, heritage.panic_str,
	    strnlen(heritage.panic_str, heritage.panic_str_len),
	    VSB_QUOTE_NONL);
	AZ(VSB_finish(child_panic));
	MGT_Complain(C_ERR, "Child (%jd) %s",
	    (intmax_t)r, VSB_data(child_panic));
}

static void
mgt_panic_clear(void)
{
	VSB_destroy(&child_panic);
}

static void
cli_panic_show(struct cli *cli, const char * const *av, int json)
{
	if (!child_panic) {
		VCLI_SetResult(cli, CLIS_CANT);
		VCLI_Out(cli,
		    "Child has not panicked or panic has been cleared");
		return;
	}

	if (!json) {
		VCLI_Out(cli, "%s\n", VSB_data(child_panic));
		return;
	}

	VCLI_JSON_begin(cli, 2, av);
	VCLI_Out(cli, ",\n");
	VCLI_JSON_str(cli, VSB_data(child_panic));
	VCLI_JSON_end(cli);
}

static void v_matchproto_(cli_func_t)
mch_cli_panic_show(struct cli *cli, const char * const *av, void *priv)
{
	(void)priv;
	cli_panic_show(cli, av, 0);
}

static void v_matchproto_(cli_func_t)
mch_cli_panic_show_json(struct cli *cli, const char * const *av, void *priv)
{
	(void)priv;
	cli_panic_show(cli, av, 1);
}

static void v_matchproto_(cli_func_t)
mch_cli_panic_clear(struct cli *cli, const char * const *av, void *priv)
{
	(void)priv;

	if (av[2] != NULL && strcmp(av[2], "-z")) {
		VCLI_SetResult(cli, CLIS_PARAM);
		VCLI_Out(cli, "Unknown parameter \"%s\".", av[2]);
		return;
	} else if (av[2] != NULL) {
		VSC_C_mgt->child_panic = 0;
		if (child_panic == NULL)
			return;
	}
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
 * so we add a margin of 10 fds.
 *
 * For added safety, we check that we see no file descriptor open for
 * another margin above the limit for which we close by design
 */

static int		mgt_max_fd;

#define CLOSE_FD_UP_TO	(mgt_max_fd + 10)
#define CHECK_FD_UP_TO	(CLOSE_FD_UP_TO + 10)

void
MCH_TrackHighFd(int fd)
{
	/*
	 * Assert > 0, to catch bogus opens, we know where stdin goes
	 * in the master process.
	 */
	assert(fd > 0);
	mgt_max_fd = vmax(mgt_max_fd, fd);
}

/*--------------------------------------------------------------------
 * Keep track of which filedescriptors the child should inherit and
 * which should be closed after fork()
 */

void
MCH_Fd_Inherit(int fd, const char *what)
{

	assert(fd >= 0);
	// XXX why?
	if (fd > 0)
		MCH_TrackHighFd(fd);
	if (fd_map == NULL)
		fd_map = vbit_new(128);
	AN(fd_map);
	if (what != NULL)
		vbit_set(fd_map, fd);
	else
		vbit_clr(fd_map, fd);
}

/*=====================================================================
 * Listen to stdout+stderr from the child
 */

static const char *whining_child = C_ERR;

static int v_matchproto_(vlu_f)
child_line(void *priv, const char *p)
{
	(void)priv;

	MGT_Complain(whining_child, "Child (%jd) said %s", (intmax_t)child_pid, p);
	return (0);
}

/*--------------------------------------------------------------------
 * NB: Notice cleanup call from mgt_reap_child()
 */

static int v_matchproto_(vev_cb_f)
child_listener(const struct vev *e, int what)
{

	if ((what & ~VEV__RD) || VLU_Fd(child_std_vlu, child_output)) {
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

static int v_matchproto_(vev_cb_f)
child_poker(const struct vev *e, int what)
{
	char *r = NULL;
	unsigned status;

	(void)e;
	(void)what;
	if (child_state != CH_RUNNING)
		return (1);
	if (child_pid < 0)
		return (0);
	if (mgt_cli_askchild(&status, &r, "ping\n") || strncmp("PONG ", r, 5)) {
		MGT_Complain(C_ERR, "Unexpected reply from ping: %u %s",
		    status, r);
		if (status != CLIS_COMMS)
			MCH_Cli_Fail();
	}
	free(r);
	return (0);
}

/*=====================================================================
 * Launch the child process
 */

#define mgt_launch_err(cli, status, ...) do {		\
		MGT_Complain(C_ERR, __VA_ARGS__);	\
		if (cli == NULL)			\
			break;				\
		VCLI_Out(cli, __VA_ARGS__);		\
		VCLI_SetResult(cli, status);		\
	} while (0)

static void
mgt_launch_child(struct cli *cli)
{
	pid_t pid;
	unsigned u;
	char *p;
	struct vev *e;
	int i, cp[2];
	struct rlimit rl[1];
	vtim_dur dstart;
	int bstart;
	vtim_mono t0;

	if (child_state != CH_STOPPED && child_state != CH_DIED)
		return;

	child_state = CH_STARTING;

	/* Open pipe for mgt->child CLI */
	AZ(pipe(cp));
	heritage.cli_in = cp[0];
	assert(cp[0] > STDERR_FILENO);	// See #2782
	assert(cp[1] > STDERR_FILENO);
	MCH_Fd_Inherit(heritage.cli_in, "cli_in");
	child_cli_out = cp[1];

	/* Open pipe for child->mgt CLI */
	AZ(pipe(cp));
	heritage.cli_out = cp[1];
	MCH_Fd_Inherit(heritage.cli_out, "cli_out");
	child_cli_in = cp[0];

	/* Create socketpair for posting fd's to the child */
	heritage.fence = mgt_SMUG_Init();
	MCH_Fd_Inherit(heritage.fence, "smug-fence");

	/*
	 * Open pipe for child stdout/err
	 * NB: not inherited, because we dup2() it to stdout/stderr in child
	 */
	AZ(pipe(cp));
	heritage.std_fd = cp[1];
	child_output = cp[0];

	mgt_SHM_ChildNew();

	AN(heritage.param);
	AN(heritage.panic_str);
	VJ_master(JAIL_MASTER_SYSTEM);
	if ((pid = fork()) < 0) {
		VJ_master(JAIL_MASTER_LOW);
		perror("Could not fork child");
		exit(1);		// XXX Harsh ?
	}
	if (pid == 0) {
		if (MGT_FEATURE(FEATURE_NO_COREDUMP)) {
			memset(rl, 0, sizeof *rl);
			rl->rlim_cur = 0;
			AZ(setrlimit(RLIMIT_CORE, rl));
		}

		/* Redirect stdin/out/err */
		VFIL_null_fd(STDIN_FILENO);
		assert(dup2(heritage.std_fd, STDOUT_FILENO) == STDOUT_FILENO);
		assert(dup2(heritage.std_fd, STDERR_FILENO) == STDERR_FILENO);

		setbuf(stdout, NULL);
		setbuf(stderr, NULL);
		printf("Child starts\n");

		/*
		 * Close all FDs the child shouldn't know about
		 *
		 * We cannot just close these filedescriptors, some random
		 * library routine might miss it later on and wantonly close
		 * a FD we use at that point in time. (See bug #1841).
		 * We close the FD and replace it with /dev/null instead,
		 * That prevents security leakage, and gives the library
		 * code a valid FD to close when it discovers the changed
		 * circumstances.
		 */
		closelog();

		/* Closing manually to clear file descriptors */
		MAC_close_sockets();

		for (i = STDERR_FILENO + 1; i <= CLOSE_FD_UP_TO; i++) {
			if (vbit_test(fd_map, i))
				continue;
			if (close(i) == 0)
				VFIL_null_fd(i);
		}
		for (i = CLOSE_FD_UP_TO + 1; i <= CHECK_FD_UP_TO; i++) {
			assert(close(i) == -1);
			assert(errno == EBADF);
		}

		mgt_ProcTitle("Child");

		heritage.cls = mgt_cls;
		heritage.ident = VSB_data(vident) + 1;

		vext_load();

		STV_Init();

		VJ_subproc(JAIL_SUBPROC_WORKER);

		/*
		 * We pass these two params because child_main needs them
		 * well before it has found its own param struct.
		 */
		child_main(mgt_param.sigsegv_handler,
		    mgt_param.wthread_stacksize);

		/*
		 * It would be natural to clean VSMW up here, but it is apt
		 * to fail in some scenarios because of the fall-back
		 * "rm -rf" in mgt_SHM_ChildDestroy() which is there to
		 * catch the cases were we don't get here.
		 */
		// VSMW_Destroy(&heritage.proc_vsmw);

		exit(0);
	}
	VJ_master(JAIL_MASTER_LOW);
	assert(pid > 1);
	MGT_Complain(C_DEBUG, "Child (%jd) Started", (intmax_t)pid);
	VSC_C_mgt->child_start++;

	/* Close stuff the child got */
	closefd(&heritage.std_fd);

	MCH_Fd_Inherit(heritage.cli_in, NULL);
	closefd(&heritage.cli_in);

	MCH_Fd_Inherit(heritage.cli_out, NULL);
	closefd(&heritage.cli_out);

	MCH_Fd_Inherit(heritage.fence, NULL);
	closefd(&heritage.fence);

	child_std_vlu = VLU_New(child_line, NULL, 0);
	AN(child_std_vlu);

	/* Wait for cache/cache_cli.c::CLI_Run() to check in */
	bstart = mgt_param.startup_timeout >= mgt_param.cli_timeout;
	dstart = bstart ? mgt_param.startup_timeout : mgt_param.cli_timeout;
	t0 = VTIM_mono();
	if (VCLI_ReadResult(child_cli_in, &u, NULL, dstart)) {
		assert(u == CLIS_COMMS);
		if (VTIM_mono() - t0 < dstart)
			mgt_launch_err(cli, u, "Child failed on launch ");
		else
			mgt_launch_err(cli, u, "Child failed on launch "
			    "within %s_timeout=%.2fs%s",
			    bstart ? "startup" : "cli", dstart,
			    bstart ? "" : " (tip: set startup_timeout)");
		child_pid = pid;
		(void)kill_child();
		mgt_reap_child();
		child_state = CH_STOPPED;
		return;
	} else {
		assert(u == CLIS_OK);
		fprintf(stderr, "Child launched OK\n");
	}
	whining_child = C_INFO;

	AZ(ev_listen);
	e = VEV_Alloc();
	XXXAN(e);
	e->fd = child_output;
	e->fd_flags = VEV__RD;
	e->name = "Child listener";
	e->callback = child_listener;
	AZ(VEV_Start(mgt_evb, e));
	ev_listen = e;
	AZ(ev_poker);
	if (mgt_param.ping_interval > 0) {
		e = VEV_Alloc();
		XXXAN(e);
		e->timeout = mgt_param.ping_interval;
		e->callback = child_poker;
		e->name = "child poker";
		AZ(VEV_Start(mgt_evb, e));
		ev_poker = e;
	}

	mgt_cli_start_child(child_cli_in, child_cli_out);
	child_pid = pid;

	if (mgt_push_vcls(cli, &u, &p)) {
		mgt_launch_err(cli, u, "Child (%jd) Pushing vcls failed:\n%s",
		    (intmax_t)child_pid, p);
		free(p);
		MCH_Stop_Child();
		return;
	}

	if (mgt_param.accept_traffic && MAC_smuggle_sockets()) {
		VCLI_SetResult(cli, CLIS_CANT);
		MGT_Complain(C_ERR,
		    "Child (%jd) Pushing listen sockets failed\n",
		    (intmax_t)child_pid);
		MCH_Stop_Child();
		return;
	}

	if (mgt_cli_askchild(&u, &p, "start\n")) {
		mgt_launch_err(cli, u, "Child (%jd) Acceptor start failed:\n%s",
		    (intmax_t)child_pid, p);
		free(p);
		MCH_Stop_Child();
		return;
	}

	free(p);
	child_state = CH_RUNNING;
}

/*=====================================================================
 * Cleanup when child dies.
 */

static int
kill_child(void)
{
	int i, error;

	VJ_master(JAIL_MASTER_KILL);
	i = kill(child_pid, SIGQUIT);
	error = errno;
	VJ_master(JAIL_MASTER_LOW);
	errno = error;
	return (i);
}

static void
mgt_reap_child(void)
{
	int i;
	int status = 0xffff;
	struct vsb *vsb;
	pid_t r = 0;

	assert(child_pid != -1);

	/*
	 * Close the CLI connections
	 * This signals orderly shut down to child
	 */
	mgt_cli_stop_child();
	if (child_cli_out >= 0)
		closefd(&child_cli_out);
	if (child_cli_in >= 0)
		closefd(&child_cli_in);

	/* Stop the poker */
	if (ev_poker != NULL) {
		VEV_Stop(mgt_evb, ev_poker);
		free(ev_poker);
		ev_poker = NULL;
	}

	/* Stop the listener */
	if (ev_listen != NULL) {
		VEV_Stop(mgt_evb, ev_listen);
		free(ev_listen);
		ev_listen = NULL;
	}

	/* Compose obituary */
	vsb = VSB_new_auto();
	XXXAN(vsb);

	(void)VFIL_nonblocking(child_output);
	/* Wait for child to die */
	for (i = 0; i < mgt_param.cli_timeout * 10; i++) {
		(void)child_listener(NULL, VEV__RD);
		r = waitpid(child_pid, &status, WNOHANG);
		if (r == child_pid)
			break;
		(void)usleep(100000);
	}
	if (r == 0) {
		VSB_printf(vsb, "Child (%jd) not dying (waitpid = %jd),"
		    " killing\n", (intmax_t)child_pid, (intmax_t)r);

		/* Kick it Jim... */
		(void)kill_child();
		r = waitpid(child_pid, &status, 0);
	}
	if (r != child_pid)
		fprintf(stderr, "WAIT 0x%jd\n", (intmax_t)r);
	assert(r == child_pid);

	VSB_printf(vsb, "Child (%jd) %s", (intmax_t)r,
	    status ? "died" : "ended");
	if (WIFEXITED(status) && WEXITSTATUS(status)) {
		VSB_printf(vsb, " status=%d", WEXITSTATUS(status));
		exit_status |= 0x20;
		if (WEXITSTATUS(status) == 1)
			VSC_C_mgt->child_exit++;
		else
			VSC_C_mgt->child_stop++;
	}
	if (WIFSIGNALED(status)) {
		VSB_printf(vsb, " signal=%d", WTERMSIG(status));
		exit_status |= 0x40;
		VSC_C_mgt->child_died++;
	}
#ifdef WCOREDUMP
	if (WCOREDUMP(status)) {
		VSB_cat(vsb, " (core dumped)");
		if (!MGT_FEATURE(FEATURE_NO_COREDUMP))
			exit_status |= 0x80;
		VSC_C_mgt->child_dump++;
	}
#endif
	AZ(VSB_finish(vsb));
	MGT_Complain(status ? C_ERR : C_INFO, "%s", VSB_data(vsb));
	VSB_destroy(&vsb);

	/* Dispose of shared memory but evacuate panic messages first */
	if (heritage.panic_str[0] != '\0') {
		mgt_panic_record(r);
		VSC_C_mgt->child_panic++;
	}

	mgt_SHM_ChildDestroy();

	if (child_state == CH_RUNNING)
		child_state = CH_DIED;

	/* Pick up any stuff lingering on stdout/stderr */
	(void)child_listener(NULL, VEV__RD);
	closefd(&child_output);
	VLU_Destroy(&child_std_vlu);

	/* Notify smug that the child has been reaped */
	mgt_SMUG_Fini();

	child_pid = -1;

	MGT_Complain(C_DEBUG, "Child cleanup complete");

	/* XXX number of retries? interval? */
	for (i = 0; i < 3; i++) {
		if (MAC_reopen_sockets() == 0)
			break;
		/* error already logged */
		(void)sleep(1);
	}
	if (i == 3) {
		/* We failed to reopen our listening sockets. No choice
		 * but to exit. */
		MGT_Complain(C_ERR,
		    "Could not reopen listening sockets. Exiting.");
		exit(1);
	}

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
MCH_Cli_Fail(void)
{

	if (child_state != CH_RUNNING && child_state != CH_STARTING)
		return;
	if (child_pid < 0)
		return;
	if (kill_child() == 0)
		MGT_Complain(C_ERR, "Child (%jd) not responding to CLI,"
		    " killed it.", (intmax_t)child_pid);
	else
		MGT_Complain(C_ERR, "Failed to kill child with PID %jd: %s",
		    (intmax_t)child_pid, VAS_errtxt(errno));
}

/*=====================================================================
 * Controlled stop of child process
 *
 * Reaping the child asks for orderly shutdown
 */

void
MCH_Stop_Child(void)
{

	if (child_state != CH_RUNNING && child_state != CH_STARTING)
		return;

	child_state = CH_STOPPING;

	MGT_Complain(C_DEBUG, "Stopping Child");

	mgt_reap_child();
}

/*=====================================================================
 */

int
MCH_Start_Child(void)
{
	mgt_launch_child(NULL);
	if (child_state != CH_RUNNING)
		return (2);
	return (0);
}

/*====================================================================
 * Query if the child is running
 */

int
MCH_Running(void)
{

	return (child_pid > 0);
}

/*=====================================================================
 * CLI commands
 */

static void v_matchproto_(cli_func_t)
mch_pid(struct cli *cli, const char * const *av, void *priv)
{

	(void)av;
	(void)priv;
	VCLI_Out(cli, "Master: %10jd\n", (intmax_t)getpid());
	if (!MCH_Running())
		return;
	VCLI_Out(cli, "Worker: %10jd\n", (intmax_t)child_pid);
}

static void v_matchproto_(cli_func_t)
mch_pid_json(struct cli *cli, const char * const *av, void *priv)
{

	(void)priv;
	VCLI_JSON_begin(cli, 2, av);
	VCLI_Out(cli, ",\n  {\"master\": %jd", (intmax_t)getpid());
	if (MCH_Running())
		VCLI_Out(cli, ", \"worker\": %jd", (intmax_t)child_pid);
	VCLI_Out(cli, "}");
	VCLI_JSON_end(cli);
}

static void v_matchproto_(cli_func_t)
mch_cli_server_start(struct cli *cli, const char * const *av, void *priv)
{
	const char *err;

	(void)av;
	(void)priv;
	if (child_state == CH_STOPPED) {
		err = mgt_has_vcl();
		if (err == NULL) {
			mgt_launch_child(cli);
		} else {
			VCLI_SetResult(cli, CLIS_CANT);
			VCLI_Out(cli, "%s", err);
		}
	} else {
		VCLI_SetResult(cli, CLIS_CANT);
		VCLI_Out(cli, "Child in state %s", ch_state[child_state]);
	}
}

static void v_matchproto_(cli_func_t)
mch_cli_server_stop(struct cli *cli, const char * const *av, void *priv)
{

	(void)av;
	(void)priv;
	if (child_state == CH_RUNNING) {
		MCH_Stop_Child();
	} else {
		VCLI_SetResult(cli, CLIS_CANT);
		VCLI_Out(cli, "Child in state %s", ch_state[child_state]);
	}
}

static void v_matchproto_(cli_func_t)
mch_cli_server_status(struct cli *cli, const char * const *av, void *priv)
{
	(void)av;
	(void)priv;
	VCLI_Out(cli, "Child in state %s", ch_state[child_state]);
}

static void v_matchproto_(cli_func_t)
mch_cli_server_status_json(struct cli *cli, const char * const *av, void *priv)
{
	(void)priv;
	VCLI_JSON_begin(cli, 2, av);
	VCLI_Out(cli, ", ");
	VCLI_JSON_str(cli, ch_state[child_state]);
	VCLI_JSON_end(cli);
}

static struct cli_proto cli_mch[] = {
	{ CLICMD_SERVER_STATUS,		"", mch_cli_server_status,
	  mch_cli_server_status_json },
	{ CLICMD_SERVER_START,		"", mch_cli_server_start },
	{ CLICMD_SERVER_STOP,		"", mch_cli_server_stop },
	{ CLICMD_PANIC_SHOW,		"", mch_cli_panic_show,
	  mch_cli_panic_show_json },
	{ CLICMD_PANIC_CLEAR,		"", mch_cli_panic_clear },
	{ CLICMD_PID,			"", mch_pid, mch_pid_json },
	{ NULL }
};

/*=====================================================================
 * This thread is the master thread in the management process.
 * The relatively simple task is to start and stop the child process
 * and to reincarnate it in case of trouble.
 */

void
MCH_Init(void)
{

	VCLS_AddFunc(mgt_cls, MCF_AUTH, cli_mch);
}
