/*-
 * Copyright (c) 2008-2018 Varnish Software AS
 * All rights reserved.
 *
 * Author: Frédéric Lécaille <flecaille@haproxy.com>
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
 */

#include "config.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vtc.h"

#include "vav.h"
#include "vfil.h"
#include "vpf.h"
#include "vtcp.h"
#include "vtim.h"

#define HAPROXY_PROGRAM_ENV_VAR	"HAPROXY_PROGRAM"
#define HAPROXY_OPT_WORKER	"-W"
#define HAPROXY_OPT_DAEMON	"-D"
#define HAPROXY_OPT_CHECK_MODE	"-c"
#define HAPROXY_SIGNAL		SIGINT
#define HAPROXY_EXPECT_EXIT	(128 + HAPROXY_SIGNAL)

struct haproxy {
	unsigned		magic;
#define HAPROXY_MAGIC		0x8a45cf75
	char			*name;
	struct vtclog		*vl;
	VTAILQ_ENTRY(haproxy)	list;

	char			*filename;
	struct vsb		*args;
	int			opt_worker;
	int			opt_daemon;
	int			opt_check_mode;
	char			*pid_fn;
	pid_t			pid;
	pid_t			ppid;
	int			fds[4];
	char			*cfg_fn;

	pthread_t		tp;
	int			expect_exit;
	int			expect_signal;
	int			kill_status;
	int			kill_errno;

	const char		*cli_fn;

	char			*workdir;
};

static void haproxy_cleanup(struct haproxy *h, int is_haproxy_listener);

static VTAILQ_HEAD(, haproxy)	haproxies =
    VTAILQ_HEAD_INITIALIZER(haproxies);

/**********************************************************************
 *
 */

static void
wait_stopped(struct haproxy *h)
{
	vtc_log(h->vl, 3, "wait-stopped");
	while (1) {
		if (h->kill_status >= 0 || h->kill_errno == ESRCH)
			break;

		usleep(1000);
		h->kill_status = kill(h->pid, 0);
		h->kill_errno = errno;
	}
}

static void
wait_running(struct haproxy *h)
{
	char buf_err[1024] = {0};
	int usleep_time = 1000;
	double t0;
	pid_t pid;

	if (h->opt_check_mode)
		return;

	if (h->pid_fn == NULL) {
		/*
		 * If we use no pid-file, check that the process
		 * we started is still running.
		 */
		if (kill(h->pid, 0) < 0)
			vtc_fatal(h->vl, "Process %ld not there: %s",
			    (long)h->pid, strerror(errno));
		return;
	}

	vtc_log(h->vl, 3, "wait-running");
	for (t0 = VTIM_mono(); VTIM_mono() - t0 < 3;) {
		if (vtc_error)
			return;

		if (VPF_read(h->pid_fn, &pid) != 0) {
			bprintf(buf_err,
			    "Could not read PID file '%s'", h->pid_fn);
			usleep(usleep_time);
			continue;
		}

		if (!h->opt_daemon && pid != h->pid) {
			bprintf(buf_err,
			    "PID file has different PID (%ld != %lld)",
			    (long)pid, (long long)h->pid);
			usleep(usleep_time);
			continue;
		}

		if (kill(pid, 0) < 0) {
			bprintf(buf_err,
			    "Could not find PID %ld process", (long)pid);
			usleep(usleep_time);
			continue;
		}

		h->pid = pid;

		vtc_log(h->vl, 2, "haproxy PID %ld successfully started",
		    (long)pid);
		return;
	}
	vtc_fatal(h->vl, "haproxy %s PID file check failed:\n\t%s\n",
		  h->name, buf_err);
}

/**********************************************************************
 * Allocate and initialize a haproxy
 */

static struct haproxy *
haproxy_new(const char *name)
{
	struct haproxy *h;
	struct vsb *vsb;
	char buf[PATH_MAX];

	ALLOC_OBJ(h, HAPROXY_MAGIC);
	AN(h);
	REPLACE(h->name, name);

	h->args = VSB_new_auto();

	h->vl = vtc_logopen(name);
	AN(h->vl);

	h->filename = getenv(HAPROXY_PROGRAM_ENV_VAR);
	if (h->filename == NULL)
		REPLACE(h->filename, "haproxy");

	bprintf(buf, "${tmpdir}/%s", name);
	vsb = macro_expand(h->vl, buf);
	AN(vsb);
	h->workdir = strdup(VSB_data(vsb));
	AN(h->workdir);
	VSB_destroy(&vsb);

	bprintf(buf, "%s/stats.sock", h->workdir);
	h->cli_fn = strdup(buf);
	AN(h->cli_fn);

	bprintf(buf, "%s/cfg", h->workdir);
	h->cfg_fn = strdup(buf);
	AN(h->cfg_fn);

	bprintf(buf, "rm -rf %s ; mkdir -p %s", h->workdir, h->workdir);
	AZ(system(buf));

	VTAILQ_INSERT_TAIL(&haproxies, h, list);

	return (h);
}

/**********************************************************************
 * Delete a haproxy instance
 */

static void
haproxy_delete(struct haproxy *h)
{
	char buf[PATH_MAX];

	CHECK_OBJ_NOTNULL(h, HAPROXY_MAGIC);
	vtc_logclose(h->vl);

	if (!leave_temp) {
		bprintf(buf, "rm -rf %s", h->workdir);
		AZ(system(buf));
	}

	free(h->name);
	free(h->workdir);
	VSB_destroy(&h->args);

	/* XXX: MEMLEAK (?) */
	FREE_OBJ(h);
}

/**********************************************************************
 * HAProxy listener
 */

static void *
haproxy_thread(void *priv)
{
	struct haproxy *h;

	CAST_OBJ_NOTNULL(h, priv, HAPROXY_MAGIC);
	return (vtc_record(h->vl, h->fds[0], NULL));
}

/**********************************************************************
 * Start a HAProxy instance.
 */

static void
haproxy_start(struct haproxy *h)
{
	char **argv;
	int i;
	char buf[PATH_MAX];
	struct vsb *vsb;

	vtc_log(h->vl, 2, "%s", __func__);

	AZ(VSB_finish(h->args));
	argv = VAV_Parse(VSB_data(h->args), NULL, 0);
	AN(argv);
	if (argv[0] != NULL)
		vtc_fatal(h->vl, "Could not parse argv-string: %s", argv[0]);
	for (i = 1; argv[i] != NULL; i++) {
		/* Check if HAPROXY_OPT_WORKER option was provided. */
		if (!strcmp(argv[i], HAPROXY_OPT_WORKER)) {
			h->opt_worker = 1;
		} else if (!strcmp(argv[i], HAPROXY_OPT_DAEMON)) {
			h->opt_daemon = 1;
		} else if (!strcmp(argv[i], HAPROXY_OPT_CHECK_MODE)) {
			h->opt_check_mode = 1;
		}
	}
	VAV_Free(argv);
	vtc_log(h->vl, 4, "opt_worker %d opt_daemon %d opt_check_mode %d",
	    h->opt_worker, h->opt_daemon, h->opt_check_mode);

	vsb = VSB_new_auto();
	AN(vsb);
	VSB_printf(vsb, "exec %s %s", h->filename, VSB_data(h->args));

	VSB_printf(vsb, " -f %s ", h->cfg_fn);

	if (h->opt_worker || h->opt_daemon) {
		bprintf(buf, "%s/pid", h->workdir);
		h->pid_fn = strdup(buf);
		AN(h->pid_fn);
		VSB_printf(vsb, " -p %s", h->pid_fn);
	}

	AZ(VSB_finish(vsb));
	vtc_dump(h->vl, 4, "argv", VSB_data(vsb), -1);

	if (h->opt_worker && !h->opt_daemon) {
		/*
		 * HAProxy master process must exit with status 128 + <signum>
		 * if signaled by <signum> signal.
		 */
		h->expect_exit = HAPROXY_EXPECT_EXIT;
	} else {
		h->expect_exit = 0;
	}

	AZ(pipe(&h->fds[0]));
	AZ(pipe(&h->fds[2]));
	h->pid = h->ppid = fork();
	assert(h->pid >= 0);
	if (h->pid == 0) {
		AZ(chdir(h->name));
		AZ(dup2(h->fds[0], 0));
		assert(dup2(h->fds[3], 1) == 1);
		assert(dup2(1, 2) == 2);
		closefd(&h->fds[0]);
		closefd(&h->fds[1]);
		closefd(&h->fds[2]);
		closefd(&h->fds[3]);
		AZ(execl("/bin/sh", "/bin/sh", "-c", VSB_data(vsb), (char*)0));
		exit(1);
	}
	VSB_destroy(&vsb);

	vtc_log(h->vl, 3, "PID: %ld", (long)h->pid);
	macro_def(h->vl, h->name, "pid", "%ld", (long)h->pid);
	macro_def(h->vl, h->name, "name", "%s", h->workdir);

	closefd(&h->fds[0]);
	closefd(&h->fds[3]);
	h->fds[0] = h->fds[2];
	h->fds[2] = h->fds[3] = -1;

	AZ(pthread_create(&h->tp, NULL, haproxy_thread, h));

	wait_running(h);
}

/**********************************************************************
 * Stop a HAProxy
 */

static void
haproxy_stop(struct haproxy *h)
{
	if (h->opt_check_mode)
		return;

	if (h->pid < 0)
		haproxy_start(h);

	vtc_log(h->vl, 2, "Stop");
	h->kill_status = kill(h->pid, HAPROXY_SIGNAL);
	h->kill_errno = errno;
	h->expect_signal = -HAPROXY_SIGNAL;

	wait_stopped(h);
}

/**********************************************************************
 * Cleanup
 */

static void
haproxy_cleanup(struct haproxy *h, int is_haproxy_listener)
{
	void *p;

	vtc_log(h->vl, 2, "%s (%d)", __func__, is_haproxy_listener);
	if (!is_haproxy_listener) {
		/* Close the STDIN connection. */
		closefd(&h->fds[1]);

		/* Wait until STDOUT+STDERR closes */
		AZ(pthread_join(h->tp, &p));
		closefd(&h->fds[0]);
		if (h->opt_daemon)
			return;
	}

	if (h->ppid == -1)
		return;

	vtc_wait4(h->vl, h->ppid, h->expect_exit, h->expect_signal, 0);
	h->ppid = -1;
}

/**********************************************************************
 * Wait for a HAProxy instance.
 */

static void
haproxy_wait(struct haproxy *h)
{
	if (h->pid < 0)
		return;

	vtc_log(h->vl, 2, "Wait");

	haproxy_stop(h);

	haproxy_cleanup(h, 0);
}

#define HAPROXY_BE_FD_STR     "fd@${"
#define HAPROXY_BE_FD_STRLEN  strlen(HAPROXY_BE_FD_STR)

static int
haproxy_build_backends(const struct haproxy *h, const char *vsb_data)
{
	char *s, *p, *q;

	s = strdup(vsb_data);
	if (!s)
		return -1;

	p = s;
	while (1) {
		int sock;
		char buf[128], addr[128], port[128];
		const char *err;

		p = strstr(p, HAPROXY_BE_FD_STR);
		if (!p)
			break;

		q = p += HAPROXY_BE_FD_STRLEN;
		while (*q && *q != '}')
			q++;
		if (*q != '}')
			break;

		*q++ = '\0';
		sock = VTCP_listen_on(":0", NULL, 1, &err);
		if (err != NULL)
			vtc_fatal(h->vl,
			    "Create listen socket failed: %s", err);

		VTCP_myname(sock, addr, sizeof addr, port, sizeof port);
		bprintf(buf, "%s_%s", h->name, p);
		macro_def(h->vl, buf, "sock", "%s %s", addr, port);
		macro_def(h->vl, buf, "addr", "%s", addr);
		macro_def(h->vl, buf, "port", "%s", port);

		bprintf(buf, "%d", sock);
		vtc_log(h->vl, 4, "setenv(%s, %s)", p, buf);
		if (setenv(p, buf, 0) == -1)
			vtc_fatal(h->vl, "setenv() failed: %s (%d)",
				  strerror(errno), errno);
		p = q;
	}
	free(s);
	return (0);
}

/**********************************************************************
 * Write a configuration for <h> HAProxy instance.
 */

static void
haproxy_write_conf(const struct haproxy *h, const char *cfg, int auto_be)
{
	struct vsb *vsb, *vsb2;

	vsb = VSB_new_auto();
	AN(vsb);

	vsb2 = VSB_new_auto();
	AN(vsb2);

	VSB_printf(vsb, "    global\n\tstats socket %s "
		   "level admin mode 600\n", h->cli_fn);
	AZ(VSB_cat(vsb, cfg));

	if (auto_be)
		cmd_server_gen_haproxy_conf(vsb);

	AZ(VSB_finish(vsb));

	AZ(haproxy_build_backends(h, VSB_data(vsb)));

	if (VFIL_writefile(h->workdir, h->cfg_fn,
	    VSB_data(vsb), VSB_len(vsb)) != 0)
		vtc_fatal(h->vl,
		    "failed to write haproxy configuration file: %s (%d)",
		    strerror(errno), errno);

	vtc_dump(h->vl, 4, "conf", VSB_data(vsb), VSB_len(vsb));

	VSB_destroy(&vsb2);
	VSB_destroy(&vsb);
}

/* SECTION: haproxy haproxy
 *
 * Define and interact with haproxy instances.
 *
 * To define a haproxy server, you'll use this syntax::
 *
 *	haproxy hNAME [-arg STRING] [-conf[+vcl] STRING]
 *
 * The first ``haproxy hNAME`` invocation will start the haproxy master
 * process in the background, waiting for the ``-start`` switch to actually
 * start the child.
 *
 * Arguments:
 *
 * hNAME
 *	   Identify the HAProxy server with a string, it must starts with 'h'.
 *
 * \-arg STRING
 *         Pass an argument to haproxy, for example "-h simple_list".
 *
 * \-conf STRING
 *         Specify the configuration to be loaded by this HAProxy instance.
 *
 * \-conf+backend STRING
 *         Specify the configuration to be loaded by this HAProxy instance,
 *	   all server instances will be automatically appended
 *
 * You can decide to start the HAProxy instance and/or wait for several events::
 *
 *         haproxy hNAME [-start] [-wait-running] [-wait-stopped]
 *
 * \-start
 *         Start this HAProxy instance.
 *
 * \-stop
 *         Stop this HAProxy instance.
 *
 * \-wait-running
 *         Wait for that instance to terminate.
 *
 * \-wait-stopped
 *         Wait for that instance to terminate.
 *
 * \-cleanup
 *         Once HAProxy is stopped, clean everything after it. This is only used
 *         in very few tests and you should never need it.
 *
 * \-expectexit NUMBER
 *	   Expect haproxy to exit(3) with this value
 *
 */

void
cmd_haproxy(CMD_ARGS)
{
	struct haproxy *h, *h2;

	(void)priv;
	(void)cmd;

	if (av == NULL) {
		/* Reset and free */
		VTAILQ_FOREACH_SAFE(h, &haproxies, list, h2) {
			vtc_log(h->vl, 2,
			    "Reset and free %s haproxy %ld",
			    h->name, (long)h->pid);
			if (h->pid >= 0)
				haproxy_wait(h);
			VTAILQ_REMOVE(&haproxies, h, list);
			haproxy_delete(h);
		}
		return;
	}

	AZ(strcmp(av[0], "haproxy"));
	av++;

	VTC_CHECK_NAME(vl, av[0], "haproxy", 'h');
	VTAILQ_FOREACH(h, &haproxies, list)
		if (!strcmp(h->name, av[0]))
			break;
	if (h == NULL)
		h = haproxy_new(av[0]);
	av++;

	for (; *av != NULL; av++) {
		if (vtc_error)
			break;
		if (!strcmp(*av, "-arg")) {
			AN(av[1]);
			AZ(h->pid);
			VSB_cat(h->args, " ");
			VSB_cat(h->args, av[1]);
			av++;
			continue;
		}
		if (!strcmp(*av, "-conf")) {
			AN(av[1]);
			haproxy_write_conf(h, av[1], 0);
			av++;
			continue;
		}
		if (!strcmp(*av, "-conf+backend")) {
			AN(av[1]);
			haproxy_write_conf(h, av[1], 1);
			av++;
			continue;
		}
		if (!strcmp(*av, "-expectexit")) {
			h->expect_exit = strtoul(av[1], NULL, 0);
			av++;
			continue;
		}
		if (!strcmp(*av, "-start")) {
			haproxy_start(h);
			continue;
		}
		if (!strcmp(*av, "-wait")) {
			wait_stopped(h);
			continue;
		}
		vtc_fatal(h->vl, "Unknown haproxy argument: %s", *av);
	}
}
