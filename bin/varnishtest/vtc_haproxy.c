/*-
 * Copyright (c) 2008-2018 Varnish Software AS
 * All rights reserved.
 *
 * Author: Frédéric Lécaille <flecaille@haproxy.com>
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
 */

#include "config.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> /* for MUSL (mode_t) */
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "vtc.h"

#include "vfil.h"
#include "vpf.h"
#include "vre.h"
#include "vtcp.h"
#include "vsa.h"
#include "vtim.h"

#define HAPROXY_PROGRAM_ENV_VAR	"HAPROXY_PROGRAM"
#define HAPROXY_ARGS_ENV_VAR	"HAPROXY_ARGS"
#define HAPROXY_OPT_WORKER	"-W"
#define HAPROXY_OPT_MCLI	"-S"
#define HAPROXY_OPT_DAEMON	"-D"
#define HAPROXY_SIGNAL		SIGINT
#define HAPROXY_EXPECT_EXIT	(128 + HAPROXY_SIGNAL)

struct envar {
	VTAILQ_ENTRY(envar) list;
	char *name;
	char *value;
};

struct haproxy {
	unsigned		magic;
#define HAPROXY_MAGIC		0x8a45cf75
	char			*name;
	struct vtclog		*vl;
	VTAILQ_ENTRY(haproxy)	list;

	const char		*filename;
	struct vsb		*args;
	int			opt_worker;
	int			opt_mcli;
	int			opt_daemon;
	int			opt_check_mode;
	char			*pid_fn;
	pid_t			pid;
	pid_t			ppid;
	int			fds[4];
	char			*cfg_fn;
	struct vsb		*cfg_vsb;

	pthread_t		tp;
	int			expect_exit;
	int			expect_signal;
	int			its_dead_jim;

	/* UNIX socket CLI. */
	char			*cli_fn;
	/* TCP socket CLI. */
	struct haproxy_cli *cli;

	/* master CLI */
	struct haproxy_cli *mcli;

	char			*workdir;
	struct vsb		*msgs;
	char			closed_sock[256]; /* Closed TCP socket */
	VTAILQ_HEAD(,envar) envars;
};

static VTAILQ_HEAD(, haproxy)	haproxies =
    VTAILQ_HEAD_INITIALIZER(haproxies);

struct haproxy_cli {
	unsigned		magic;
#define HAPROXY_CLI_MAGIC	0xb09a4ed8
	struct vtclog		*vl;
	char			running;

	char			*spec;

	int			sock;
	char			connect[256];

	pthread_t		tp;
	size_t			txbuf_sz;
	char			*txbuf;
	size_t			rxbuf_sz;
	char			*rxbuf;

	double			timeout;
};

static void haproxy_write_conf(struct haproxy *h);

static void
haproxy_add_envar(struct haproxy *h,
		  const char *name, const char *value)
{
	struct envar *e;

	e = malloc(sizeof *e);
	AN(e);
	e->name = strdup(name);
	e->value = strdup(value);
	AN(e->name);
	AN(e->value);
	VTAILQ_INSERT_TAIL(&h->envars, e, list);
}

static void
haproxy_delete_envars(struct haproxy *h)
{
	struct envar *e, *e2;
	VTAILQ_FOREACH_SAFE(e, &h->envars, list, e2) {
		VTAILQ_REMOVE(&h->envars, e, list);
		free(e->name);
		free(e->value);
		free(e);
	}
}

static void
haproxy_build_env(const struct haproxy *h)
{
	struct envar *e;

	VTAILQ_FOREACH(e, &h->envars, list) {
		if (setenv(e->name, e->value, 0) == -1)
			vtc_fatal(h->vl, "setenv() failed: %s (%d)",
				  strerror(errno), errno);
	}
}

/**********************************************************************
 * Socket connect (same as client_tcp_connect()).
 */

static int
haproxy_cli_tcp_connect(struct vtclog *vl, const char *addr, double tmo,
    const char **errp)
{
	int fd;
	char mabuf[VTCP_ADDRBUFSIZE], mpbuf[VTCP_PORTBUFSIZE];

	AN(addr);
	AN(errp);
	fd = VTCP_open(addr, NULL, tmo, errp);
	if (fd < 0)
		return (fd);
	VTCP_myname(fd, mabuf, sizeof mabuf, mpbuf, sizeof mpbuf);
	vtc_log(vl, 3,
	    "CLI connected fd %d from %s %s to %s", fd, mabuf, mpbuf, addr);
	return (fd);
}

/*
 * SECTION: haproxy.cli haproxy CLI Specification
 * SECTION: haproxy.cli.send
 * send STRING
 *         Push STRING on the CLI connection. STRING will be terminated by an
 *         end of line character (\n).
 */
static void v_matchproto_(cmd_f)
cmd_haproxy_cli_send(CMD_ARGS)
{
	struct vsb *vsb;
	struct haproxy_cli *hc;
	int j;

	(void)vl;
	CAST_OBJ_NOTNULL(hc, priv, HAPROXY_CLI_MAGIC);
	AZ(strcmp(av[0], "send"));
	AN(av[1]);
	AZ(av[2]);

	vsb = VSB_new_auto();
	AN(vsb);
	AZ(VSB_cat(vsb, av[1]));
	AZ(VSB_cat(vsb, "\n"));
	AZ(VSB_finish(vsb));
	if (hc->sock == -1) {
		int fd;
		const char *err;
		struct vsb *vsb_connect;

		vsb_connect = macro_expand(hc->vl, hc->connect);
		AN(vsb_connect);
		fd = haproxy_cli_tcp_connect(hc->vl,
		    VSB_data(vsb_connect), 10., &err);
		if (fd < 0)
			vtc_fatal(hc->vl,
			    "CLI failed to open %s: %s", VSB_data(vsb), err);
		VSB_destroy(&vsb_connect);
		hc->sock = fd;
	}
	vtc_dump(hc->vl, 4, "CLI send", VSB_data(vsb), -1);

	if (VSB_tofile(vsb, hc->sock))
		vtc_fatal(hc->vl,
		    "CLI fd %d send error %s", hc->sock, strerror(errno));

	/* a CLI command must be followed by a SHUT_WR if we want HAProxy to
	 * close after the response */
	j = shutdown(hc->sock, SHUT_WR);
	vtc_log(hc->vl, 3, "CLI shutting fd %d", hc->sock);
	if (!VTCP_Check(j))
		vtc_fatal(hc->vl, "Shutdown failed: %s", strerror(errno));

	VSB_destroy(&vsb);
}

#define HAPROXY_CLI_RECV_LEN (1 << 14)
static void
haproxy_cli_recv(struct haproxy_cli *hc)
{
	ssize_t ret;
	size_t rdz, left, off;

	rdz = ret = off = 0;
	/* We want to null terminate this buffer. */
	left = hc->rxbuf_sz - 1;
	while (!vtc_error && left > 0) {
		VTCP_set_read_timeout(hc->sock, hc->timeout);

		ret = recv(hc->sock, hc->rxbuf + off, HAPROXY_CLI_RECV_LEN, 0);
		if (ret < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;

			vtc_fatal(hc->vl,
			    "CLI fd %d recv() failed (%s)",
			    hc->sock, strerror(errno));
		}
		/* Connection closed. */
		if (ret == 0) {
			if (rdz > 0 && hc->rxbuf[rdz - 1] != '\n')
				vtc_fatal(hc->vl,
				    "CLI rx timeout (fd: %d %.3fs ret: %zd)",
				    hc->sock, hc->timeout, ret);

			vtc_log(hc->vl, 4, "CLI connection normally closed");
			vtc_log(hc->vl, 3, "CLI closing fd %d", hc->sock);
			VTCP_close(&hc->sock);
			break;
		}

		rdz += ret;
		left -= ret;
		off  += ret;
	}
	hc->rxbuf[rdz] = '\0';
	vtc_dump(hc->vl, 4, "CLI recv", hc->rxbuf, rdz);
}

/*
 * SECTION: haproxy.cli.expect
 * expect OP STRING
 *         Regex match the CLI reception buffer with STRING
 *         if OP is ~ or, on the contrary, if OP is !~ check that there is
 *         no regex match.
 */
static void v_matchproto_(cmd_f)
cmd_haproxy_cli_expect(CMD_ARGS)
{
	struct haproxy_cli *hc;
	struct vsb vsb[1];
	vre_t *vre;
	int error, erroroffset, i, ret;
	char *cmp, *spec, errbuf[VRE_ERROR_LEN];

	(void)vl;
	CAST_OBJ_NOTNULL(hc, priv, HAPROXY_CLI_MAGIC);
	AZ(strcmp(av[0], "expect"));
	av++;

	cmp = av[0];
	spec = av[1];
	AN(cmp);
	AN(spec);
	AZ(av[2]);

	assert(!strcmp(cmp, "~") || !strcmp(cmp, "!~"));

	haproxy_cli_recv(hc);

	vre = VRE_compile(spec, 0, &error, &erroroffset, 1);
	if (vre == NULL) {
		AN(VSB_init(vsb, errbuf, sizeof errbuf));
		AZ(VRE_error(vsb, error));
		AZ(VSB_finish(vsb));
		VSB_fini(vsb);
		vtc_fatal(hc->vl, "CLI regexp error: '%s' (@%d) (%s)",
		    errbuf, erroroffset, spec);
	}

	i = VRE_match(vre, hc->rxbuf, 0, 0, NULL);

	VRE_free(&vre);

	ret = (i >= 0 && *cmp == '~') || (i < 0 && *cmp == '!');
	if (!ret)
		vtc_fatal(hc->vl, "CLI expect failed %s \"%s\"", cmp, spec);
	else
		vtc_log(hc->vl, 4, "CLI expect match %s \"%s\"", cmp, spec);
}

static const struct cmds haproxy_cli_cmds[] = {
#define CMD_HAPROXY_CLI(n) { #n, cmd_haproxy_cli_##n },
	CMD_HAPROXY_CLI(send)
	CMD_HAPROXY_CLI(expect)
#undef CMD_HAPROXY_CLI
	{ NULL, NULL }
};

/**********************************************************************
 * HAProxy CLI client thread
 */

static void *
haproxy_cli_thread(void *priv)
{
	struct haproxy_cli *hc;
	struct vsb *vsb;
	int fd;
	const char *err;

	CAST_OBJ_NOTNULL(hc, priv, HAPROXY_CLI_MAGIC);
	AN(*hc->connect);

	vsb = macro_expand(hc->vl, hc->connect);
	AN(vsb);

	fd = haproxy_cli_tcp_connect(hc->vl, VSB_data(vsb), 10., &err);
	if (fd < 0)
		vtc_fatal(hc->vl,
		    "CLI failed to open %s: %s", VSB_data(vsb), err);
	VTCP_blocking(fd);
	hc->sock = fd;
	parse_string(hc->vl, hc, hc->spec);
	vtc_log(hc->vl, 2, "CLI ending");
	VSB_destroy(&vsb);
	return (NULL);
}

/**********************************************************************
 * Wait for the CLI client thread to stop
 */

static void
haproxy_cli_wait(struct haproxy_cli *hc)
{
	void *res;

	CHECK_OBJ_NOTNULL(hc, HAPROXY_CLI_MAGIC);
	vtc_log(hc->vl, 2, "CLI waiting");
	PTOK(pthread_join(hc->tp, &res));
	if (res != NULL)
		vtc_fatal(hc->vl, "CLI returned \"%s\"", (char *)res);
	REPLACE(hc->spec, NULL);
	hc->tp = 0;
	hc->running = 0;
}

/**********************************************************************
 * Start the CLI client thread
 */

static void
haproxy_cli_start(struct haproxy_cli *hc)
{
	CHECK_OBJ_NOTNULL(hc, HAPROXY_CLI_MAGIC);
	vtc_log(hc->vl, 2, "CLI starting");
	PTOK(pthread_create(&hc->tp, NULL, haproxy_cli_thread, hc));
	hc->running = 1;

}

/**********************************************************************
 * Run the CLI client thread
 */

static void
haproxy_cli_run(struct haproxy_cli *hc)
{
	haproxy_cli_start(hc);
	haproxy_cli_wait(hc);
}

/**********************************************************************
 *
 */

static void
haproxy_wait_pidfile(struct haproxy *h)
{
	char buf_err[1024] = {0};
	int usleep_time = 1000;
	double t0;
	pid_t pid;

	vtc_log(h->vl, 3, "wait-pid-file");
	for (t0 = VTIM_mono(); VTIM_mono() - t0 < 3;) {
		if (vtc_error)
			return;

		if (VPF_Read(h->pid_fn, &pid) != 0) {
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
 * Allocate and initialize a CLI client
 */

static struct haproxy_cli *
haproxy_cli_new(struct haproxy *h)
{
	struct haproxy_cli *hc;

	ALLOC_OBJ(hc, HAPROXY_CLI_MAGIC);
	AN(hc);

	hc->vl = h->vl;
	vtc_log_set_cmd(hc->vl, haproxy_cli_cmds);
	hc->sock = -1;
	bprintf(hc->connect, "${%s_cli_sock}", h->name);

	hc->txbuf_sz = hc->rxbuf_sz = 2048 * 1024;
	hc->txbuf = malloc(hc->txbuf_sz);
	AN(hc->txbuf);
	hc->rxbuf = malloc(hc->rxbuf_sz);
	AN(hc->rxbuf);

	return (hc);
}

/* creates a master CLI client (-mcli) */
static struct haproxy_cli *
haproxy_mcli_new(struct haproxy *h)
{
	struct haproxy_cli *hc;

	ALLOC_OBJ(hc, HAPROXY_CLI_MAGIC);
	AN(hc);

	hc->vl = h->vl;
	vtc_log_set_cmd(hc->vl, haproxy_cli_cmds);
	hc->sock = -1;
	bprintf(hc->connect, "${%s_mcli_sock}", h->name);

	hc->txbuf_sz = hc->rxbuf_sz = 2048 * 1024;
	hc->txbuf = malloc(hc->txbuf_sz);
	AN(hc->txbuf);
	hc->rxbuf = malloc(hc->rxbuf_sz);
	AN(hc->rxbuf);

	return (hc);
}

/* Bind an address/port for the master CLI (-mcli) */
static int
haproxy_create_mcli(struct haproxy *h)
{
	int sock;
	const char *err;
	char buf[128], addr[128], port[128];
	char vsabuf[vsa_suckaddr_len];
	const struct suckaddr *sua;

	sock = VTCP_listen_on(default_listen_addr, NULL, 100, &err);
	if (err != NULL)
		vtc_fatal(h->vl,
			  "Create listen socket failed: %s", err);
	assert(sock > 0);
	sua = VSA_getsockname(sock, vsabuf, sizeof vsabuf);
	AN(sua);

	VTCP_name(sua, addr, sizeof addr, port, sizeof port);
	bprintf(buf, "%s_mcli", h->name);
	if (VSA_Get_Proto(sua) == AF_INET)
		macro_def(h->vl, buf, "sock", "%s:%s", addr, port);
	else
		macro_def(h->vl, buf, "sock", "[%s]:%s", addr, port);
	macro_def(h->vl, buf, "addr", "%s", addr);
	macro_def(h->vl, buf, "port", "%s", port);

	return (sock);
}

static void
haproxy_cli_delete(struct haproxy_cli *hc)
{
	CHECK_OBJ_NOTNULL(hc, HAPROXY_CLI_MAGIC);
	REPLACE(hc->spec, NULL);
	REPLACE(hc->txbuf, NULL);
	REPLACE(hc->rxbuf, NULL);
	FREE_OBJ(hc);
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
	int closed_sock;
	char addr[128], port[128];
	const char *err;
	const char *env_args;
	char vsabuf[vsa_suckaddr_len];
	const struct suckaddr *sua;

	ALLOC_OBJ(h, HAPROXY_MAGIC);
	AN(h);
	REPLACE(h->name, name);

	h->args = VSB_new_auto();
	env_args = getenv(HAPROXY_ARGS_ENV_VAR);
	if (env_args) {
		VSB_cat(h->args, env_args);
		VSB_cat(h->args, " ");
	}

	h->vl = vtc_logopen("%s", name);
	vtc_log_set_cmd(h->vl, haproxy_cli_cmds);
	AN(h->vl);

	h->filename = getenv(HAPROXY_PROGRAM_ENV_VAR);
	if (h->filename == NULL)
		h->filename = "haproxy";

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

	/* Create a new TCP socket to reserve an IP:port and close it asap.
	 * May be useful to simulate an unreachable server.
	 */
	bprintf(h->closed_sock, "%s_closed", h->name);
	closed_sock = VTCP_listen_on("127.0.0.1:0", NULL, 100, &err);
	if (err != NULL)
		vtc_fatal(h->vl,
			"Create listen socket failed: %s", err);
	assert(closed_sock > 0);
	sua = VSA_getsockname(closed_sock, vsabuf, sizeof vsabuf);
	AN(sua);
	VTCP_name(sua, addr, sizeof addr, port, sizeof port);
	if (VSA_Get_Proto(sua) == AF_INET)
		macro_def(h->vl, h->closed_sock, "sock", "%s:%s", addr, port);
	else
		macro_def(h->vl, h->closed_sock, "sock", "[%s]:%s", addr, port);
	macro_def(h->vl, h->closed_sock, "addr", "%s", addr);
	macro_def(h->vl, h->closed_sock, "port", "%s", port);
	VTCP_close(&closed_sock);

	h->cli = haproxy_cli_new(h);
	AN(h->cli);

	h->mcli = haproxy_mcli_new(h);
	AN(h->mcli);

	bprintf(buf, "rm -rf \"%s\" ; mkdir -p \"%s\"", h->workdir, h->workdir);
	AZ(system(buf));

	VTAILQ_INIT(&h->envars);
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
		bprintf(buf, "rm -rf \"%s\"", h->workdir);
		AZ(system(buf));
	}

	free(h->name);
	free(h->workdir);
	free(h->cli_fn);
	free(h->cfg_fn);
	free(h->pid_fn);
	VSB_destroy(&h->args);
	haproxy_cli_delete(h->cli);
	haproxy_cli_delete(h->mcli);

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
	(void)vtc_record(h->vl, h->fds[0], h->msgs);
	h->its_dead_jim = 1;
	return (NULL);
}


/**********************************************************************
 * Start a HAProxy instance.
 */

static void
haproxy_start(struct haproxy *h)
{
	char buf[PATH_MAX];
	struct vsb *vsb;

	vtc_log(h->vl, 2, "%s", __func__);

	AZ(VSB_finish(h->args));
	vtc_log(h->vl, 4, "opt_worker %d opt_daemon %d opt_check_mode %d opt_mcli %d",
	    h->opt_worker, h->opt_daemon, h->opt_check_mode, h->opt_mcli);

	vsb = VSB_new_auto();
	AN(vsb);

	VSB_printf(vsb, "exec \"%s\"", h->filename);
	if (h->opt_check_mode)
		VSB_cat(vsb, " -c");
	else if (h->opt_daemon)
		VSB_cat(vsb, " -D");
	else
		VSB_cat(vsb, " -d");

	if (h->opt_worker) {
		VSB_cat(vsb, " -W");
		if (h->opt_mcli) {
			int sock;
			sock = haproxy_create_mcli(h);
			VSB_printf(vsb, " -S \"fd@%d\"", sock);
		}
	}

	VSB_printf(vsb, " %s", VSB_data(h->args));

	VSB_printf(vsb, " -f \"%s\" ", h->cfg_fn);

	if (h->opt_worker || h->opt_daemon) {
		bprintf(buf, "%s/pid", h->workdir);
		h->pid_fn = strdup(buf);
		AN(h->pid_fn);
		VSB_printf(vsb, " -p \"%s\"", h->pid_fn);
	}

	AZ(VSB_finish(vsb));
	vtc_dump(h->vl, 4, "argv", VSB_data(vsb), -1);

	if (h->opt_worker && !h->opt_daemon) {
		/*
		 * HAProxy master process must exit with status 128 + <signum>
		 * if signaled by <signum> signal.
		 */
		h->expect_exit = HAPROXY_EXPECT_EXIT;
	}

	haproxy_write_conf(h);

	AZ(pipe(&h->fds[0]));
	vtc_log(h->vl, 4, "XXX %d @%d", h->fds[1], __LINE__);
	AZ(pipe(&h->fds[2]));
	h->pid = h->ppid = fork();
	assert(h->pid >= 0);
	if (h->pid == 0) {
		haproxy_build_env(h);
		haproxy_delete_envars(h);
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

	PTOK(pthread_create(&h->tp, NULL, haproxy_thread, h));

	if (h->pid_fn != NULL)
		haproxy_wait_pidfile(h);
}


/**********************************************************************
 * Wait for a HAProxy instance.
 */

static void
haproxy_wait(struct haproxy *h)
{
	void *p;
	int i, n, sig;

	vtc_log(h->vl, 2, "Wait");

	if (h->pid < 0)
		haproxy_start(h);

	if (h->cli->spec)
		haproxy_cli_run(h->cli);

	if (h->mcli->spec)
		haproxy_cli_run(h->mcli);

	closefd(&h->fds[1]);

	sig = SIGINT;
	n = 0;
	vtc_log(h->vl, 2, "Stop HAproxy pid=%ld", (long)h->pid);
	while (h->opt_daemon || (!h->opt_check_mode && !h->its_dead_jim)) {
		assert(h->pid > 0);
		if (n == 0) {
			i = kill(h->pid, sig);
			if (i == 0)
				h->expect_signal = -sig;
			if (i && errno == ESRCH)
				break;
			vtc_log(h->vl, 4,
			    "Kill(%d)=%d: %s", sig, i, strerror(errno));
		}
		usleep(100000);
		if (++n == 20) {
			switch (sig) {
			case SIGINT:	sig = SIGTERM ; break;
			case SIGTERM:	sig = SIGKILL ; break;
			default:	break;
			}
			n = 0;
		}
	}

	PTOK(pthread_join(h->tp, &p));
	AZ(p);
	closefd(&h->fds[0]);
	if (!h->opt_daemon) {
		vtc_wait4(h->vl, h->ppid, h->expect_exit, h->expect_signal, 0);
		h->ppid = -1;
	}
	h->pid = -1;
}

#define HAPROXY_BE_FD_STR     "fd@${"
#define HAPROXY_BE_FD_STRLEN  strlen(HAPROXY_BE_FD_STR)

static int
haproxy_build_backends(struct haproxy *h, const char *vsb_data)
{
	char *s, *p, *q;

	s = strdup(vsb_data);
	if (!s)
		return (-1);

	p = s;
	while (1) {
		int sock;
		char buf[128], addr[128], port[128];
		const char *err;
		char vsabuf[vsa_suckaddr_len];
		const struct suckaddr *sua;

		p = strstr(p, HAPROXY_BE_FD_STR);
		if (!p)
			break;

		q = p += HAPROXY_BE_FD_STRLEN;
		while (*q && *q != '}')
			q++;
		if (*q != '}')
			break;

		*q++ = '\0';
		sock = VTCP_listen_on("127.0.0.1:0", NULL, 100, &err);
		if (err != NULL)
			vtc_fatal(h->vl,
			    "Create listen socket failed: %s", err);
		assert(sock > 0);
		sua = VSA_getsockname(sock, vsabuf, sizeof vsabuf);
		AN(sua);

		VTCP_name(sua, addr, sizeof addr, port, sizeof port);
		bprintf(buf, "%s_%s", h->name, p);
		if (VSA_Get_Proto(sua) == AF_INET)
			macro_def(h->vl, buf, "sock", "%s:%s", addr, port);
		else
			macro_def(h->vl, buf, "sock", "[%s]:%s", addr, port);
		macro_def(h->vl, buf, "addr", "%s", addr);
		macro_def(h->vl, buf, "port", "%s", port);

		bprintf(buf, "%d", sock);
		vtc_log(h->vl, 4, "setenv(%s, %s)", p, buf);
		haproxy_add_envar(h, p, buf);
		p = q;
	}
	free(s);
	return (0);
}

static void
haproxy_check_conf(struct haproxy *h, const char *expect)
{

	h->msgs = VSB_new_auto();
	AN(h->msgs);
	h->opt_check_mode = 1;
	haproxy_start(h);
	haproxy_wait(h);
	AZ(VSB_finish(h->msgs));
	if (strstr(VSB_data(h->msgs), expect) == NULL)
		vtc_fatal(h->vl, "Did not find expected string '%s'", expect);
	vtc_log(h->vl, 2, "Found expected '%s'", expect);
	VSB_destroy(&h->msgs);
}

/**********************************************************************
 * Write a configuration for <h> HAProxy instance.
 */

static void
haproxy_store_conf(struct haproxy *h, const char *cfg, int auto_be)
{
	struct vsb *vsb, *vsb2;

	vsb = VSB_new_auto();
	AN(vsb);

	vsb2 = VSB_new_auto();
	AN(vsb2);

	VSB_printf(vsb, "    global\n\tstats socket \"%s\" "
		   "level admin mode 600\n", h->cli_fn);
	VSB_cat(vsb, "    stats socket \"fd@${cli}\" level admin\n");
	AZ(VSB_cat(vsb, cfg));

	if (auto_be)
		cmd_server_gen_haproxy_conf(vsb);

	AZ(VSB_finish(vsb));

	AZ(haproxy_build_backends(h, VSB_data(vsb)));

	h->cfg_vsb = macro_expand(h->vl, VSB_data(vsb));
	AN(h->cfg_vsb);

	VSB_destroy(&vsb2);
	VSB_destroy(&vsb);
}

static void
haproxy_write_conf(struct haproxy *h)
{
	struct vsb *vsb;

	vsb = macro_expand(h->vl, VSB_data(h->cfg_vsb));
	AN(vsb);
	assert(VSB_len(vsb) >= 0);

	vtc_dump(h->vl, 4, "conf", VSB_data(vsb), VSB_len(vsb));
	if (VFIL_writefile(h->workdir, h->cfg_fn,
	    VSB_data(vsb), VSB_len(vsb)) != 0)
		vtc_fatal(h->vl,
		    "failed to write haproxy configuration file: %s (%d)",
		    strerror(errno), errno);

	VSB_destroy(&vsb);
}

/* SECTION: haproxy haproxy
 *
 * Define and interact with haproxy instances.
 *
 * To define a haproxy server, you'll use this syntax::
 *
 *	haproxy hNAME -conf-OK CONFIG
 *	haproxy hNAME -conf-BAD ERROR CONFIG
 *	haproxy hNAME [-D] [-W] [-arg STRING] [-conf[+vcl] STRING]
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
 * \-conf-OK CONFIG
 *         Run haproxy in '-c' mode to check config is OK
 *	   stdout/stderr should contain 'Configuration file is valid'
 *	   The exit code should be 0.
 *
 * \-conf-BAD ERROR CONFIG
 *         Run haproxy in '-c' mode to check config is BAD.
 *	   "ERROR" should be part of the diagnostics on stdout/stderr.
 *	   The exit code should be 1.
 *
 * \-D
 *         Run HAproxy in daemon mode.  If not given '-d' mode used.
 *
 * \-W
 *         Enable HAproxy in Worker mode.
 *
 * \-S
 *         Enable HAproxy Master CLI in Worker mode
 *
 * \-arg STRING
 *         Pass an argument to haproxy, for example "-h simple_list".
 *
 * \-cli STRING
 *         Specify the spec to be run by the command line interface (CLI).
 *
 * \-mcli STRING
 *         Specify the spec to be run by the command line interface (CLI)
 *         of the Master process.
 *
 * \-conf STRING
 *         Specify the configuration to be loaded by this HAProxy instance.
 *
 * \-conf+backend STRING
 *         Specify the configuration to be loaded by this HAProxy instance,
 *	   all server instances will be automatically appended
 *
 * \-start
 *         Start this HAProxy instance.
 *
 * \-wait
 *         Stop this HAProxy instance.
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

		if (!strcmp(*av, "-conf-OK")) {
			AN(av[1]);
			haproxy_store_conf(h, av[1], 0);
			h->expect_exit = 0;
			haproxy_check_conf(h, "");
			av++;
			continue;
		}
		if (!strcmp(*av, "-conf-BAD")) {
			AN(av[1]);
			AN(av[2]);
			haproxy_store_conf(h, av[2], 0);
			h->expect_exit = 1;
			haproxy_check_conf(h, av[1]);
			av += 2;
			continue;
		}

		if (!strcmp(*av, HAPROXY_OPT_DAEMON)) {
			h->opt_daemon = 1;
			continue;
		}
		if (!strcmp(*av, HAPROXY_OPT_WORKER)) {
			h->opt_worker = 1;
			continue;
		}
		if (!strcmp(*av, HAPROXY_OPT_MCLI)) {
			h->opt_mcli = 1;
			continue;
		}
		if (!strcmp(*av, "-arg")) {
			AN(av[1]);
			AZ(h->pid);
			VSB_cat(h->args, " ");
			VSB_cat(h->args, av[1]);
			av++;
			continue;
		}

		if (!strcmp(*av, "-cli")) {
			REPLACE(h->cli->spec, av[1]);
			if (h->tp)
				haproxy_cli_run(h->cli);
			av++;
			continue;
		}

		if (!strcmp(*av, "-mcli")) {
			REPLACE(h->mcli->spec, av[1]);
			if (h->tp)
				haproxy_cli_run(h->mcli);
			av++;
			continue;
		}

		if (!strcmp(*av, "-conf")) {
			AN(av[1]);
			haproxy_store_conf(h, av[1], 0);
			av++;
			continue;
		}
		if (!strcmp(*av, "-conf+backend")) {
			AN(av[1]);
			haproxy_store_conf(h, av[1], 1);
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
			haproxy_wait(h);
			continue;
		}
		vtc_fatal(h->vl, "Unknown haproxy argument: %s", *av);
	}
}
