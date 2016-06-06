/*-
 * Copyright (c) 2008-2015 Varnish Software AS
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
 */

#include "config.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/resource.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vtc.h"

#include "vapi/vsc.h"
#include "vapi/vsl.h"
#include "vapi/vsm.h"
#include "vcli.h"
#include "vss.h"
#include "vsub.h"
#include "vtcp.h"
#include "vtim.h"

extern int leave_temp;

struct varnish {
	unsigned		magic;
#define VARNISH_MAGIC		0x208cd8e3
	char			*name;
	struct vtclog		*vl;
	VTAILQ_ENTRY(varnish)	list;

	struct vsb		*args;
	int			fds[4];
	pid_t			pid;

	pthread_t		tp;
	pthread_t		tp_vsl;

	int			cli_fd;
	int			vcl_nbr;
	char			*workdir;
	char			*jail;
	char			*proto;

	struct VSM_data		*vd;		/* vsc use */

	unsigned		vsl_tag_count[256];

	volatile int		vsl_idle;
};

#define NONSENSE	"%XJEIFLH|)Xspa8P"

static VTAILQ_HEAD(, varnish)	varnishes =
    VTAILQ_HEAD_INITIALIZER(varnishes);

/**********************************************************************
 * Ask a question over CLI
 */

static enum VCLI_status_e
varnish_ask_cli(const struct varnish *v, const char *cmd, char **repl)
{
	int i;
	unsigned retval;
	char *r;

	if (cmd != NULL) {
		vtc_dump(v->vl, 4, "CLI TX", cmd, -1);
		i = write(v->cli_fd, cmd, strlen(cmd));
		if (i != strlen(cmd))
			vtc_log(v->vl, 0, "CLI write failed (%s) = %u %s",
			    cmd, errno, strerror(errno));
		i = write(v->cli_fd, "\n", 1);
		if (i != 1)
			vtc_log(v->vl, 0, "CLI write failed (%s) = %u %s",
			    cmd, errno, strerror(errno));
	}
	i = VCLI_ReadResult(v->cli_fd, &retval, &r, vtc_maxdur);
	if (i != 0) {
		vtc_log(v->vl, 0, "CLI failed (%s) = %d %u %s",
		    cmd, i, retval, r);
		return ((enum VCLI_status_e)retval);
	}
	AZ(i);
	vtc_log(v->vl, 3, "CLI RX  %u", retval);
	vtc_dump(v->vl, 4, "CLI RX", r, -1);
	if (repl != NULL)
		*repl = r;
	else
		free(r);
	return ((enum VCLI_status_e)retval);
}

/**********************************************************************
 *
 */

static void
wait_stopped(const struct varnish *v)
{
	char *r = NULL;
	enum VCLI_status_e st;

	while (1) {
		vtc_log(v->vl, 3, "wait-stopped");
		st = varnish_ask_cli(v, "status", &r);
		if (st != CLIS_OK)
			vtc_log(v->vl, 0,
			    "CLI status command failed: %u %s", st, r);
		if (!strcmp(r, "Child in state stopped")) {
			free(r);
			break;
		}
		free(r);
		r = NULL;
		(void)usleep(200000);
	}
}
/**********************************************************************
 *
 */

static void
wait_running(const struct varnish *v)
{
	char *r = NULL;
	enum VCLI_status_e st;

	while (1) {
		vtc_log(v->vl, 3, "wait-running");
		st = varnish_ask_cli(v, "status", &r);
		if (st != CLIS_OK)
			vtc_log(v->vl, 0,
			    "CLI status command failed: %u %s", st, r);
		if (!strcmp(r, "Child in state stopped")) {
			vtc_log(v->vl, 0,
			    "Child stopped before running: %u %s", st, r);
			free(r);
			break;
		}
		if (!strcmp(r, "Child in state running")) {
			free(r);
			break;
		}
		free(r);
		r = NULL;
		(void)usleep(200000);
	}
}

/**********************************************************************
 * Varnishlog gatherer thread
 */

static void *
varnishlog_thread(void *priv)
{
	struct varnish *v;
	struct VSL_data *vsl;
	struct VSM_data *vsm;
	struct VSL_cursor *c;
	enum VSL_tag_e tag;
	uint32_t vxid;
	unsigned len;
	const char *tagname, *data;
	int type, i, opt;

	CAST_OBJ_NOTNULL(v, priv, VARNISH_MAGIC);

	vsl = VSL_New();
	AN(vsl);
	vsm = VSM_New();
	AN(vsm);
	(void)VSM_n_Arg(vsm, v->workdir);

	c = NULL;
	opt = 0;
	while (v->pid) {
		if (c == NULL) {
			v->vsl_idle++;
			VTIM_sleep(0.1);
			if (VSM_Open(vsm)) {
				VSM_ResetError(vsm);
				continue;
			}
			c = VSL_CursorVSM(vsl, vsm, opt);
			if (c == NULL) {
				VSL_ResetError(vsl);
				continue;
			}
		}
		AN(c);

		opt = VSL_COPT_TAIL;

		i = VSL_Next(c);
		if (i == 0) {
			v->vsl_idle++;
			/* Nothing to do but wait */
			VTIM_sleep(0.1);
			continue;
		} else if (i == -2) {
			/* Abandoned - try reconnect */
			VSL_DeleteCursor(c);
			c = NULL;
			VSM_Close(vsm);
			continue;
		} else if (i != 1)
			break;

		v->vsl_idle = 0;

		tag = VSL_TAG(c->rec.ptr);
		vxid = VSL_ID(c->rec.ptr);
		if (tag != SLT_CLI)
			v->vsl_idle = 0;
		if (tag == SLT__Batch)
			continue;
		tagname = VSL_tags[tag];
		len = VSL_LEN(c->rec.ptr);
		type = VSL_CLIENT(c->rec.ptr) ? 'c' : VSL_BACKEND(c->rec.ptr) ?
		    'b' : '-';
		data = VSL_CDATA(c->rec.ptr);
		v->vsl_tag_count[tag]++;
		vtc_log(v->vl, 4, "vsl| %10u %-15s %c %.*s", vxid, tagname,
		    type, (int)len, data);
	}

	v->vsl_idle = 100;

	if (c)
		VSL_DeleteCursor(c);
	VSL_Delete(vsl);
	VSM_Delete(vsm);

	return (NULL);
}

/**********************************************************************
 * Allocate and initialize a varnish
 */

static struct varnish *
varnish_new(const char *name)
{
	struct varnish *v;
	struct vsb *vsb;
	char buf[1024];

	AN(name);
	ALLOC_OBJ(v, VARNISH_MAGIC);
	AN(v);
	REPLACE(v->name, name);

	REPLACE(v->jail, "");

	v->vl = vtc_logopen(name);
	AN(v->vl);

	bprintf(buf, "${tmpdir}/%s", name);
	vsb = macro_expand(v->vl, buf);
	AN(vsb);
	v->workdir = strdup(VSB_data(vsb));
	AN(v->workdir);
	VSB_delete(vsb);

	bprintf(buf, "rm -rf %s ; mkdir -p %s", v->workdir, v->workdir);
	AZ(system(buf));

	if (*v->name != 'v')
		vtc_log(v->vl, 0, "Varnish name must start with 'v'");

	v->args = VSB_new_auto();

	v->cli_fd = -1;
	VTAILQ_INSERT_TAIL(&varnishes, v, list);


	return (v);
}

/**********************************************************************
 * Delete a varnish instance
 */

static void
varnish_delete(struct varnish *v)
{

	CHECK_OBJ_NOTNULL(v, VARNISH_MAGIC);
	vtc_logclose(v->vl);
	free(v->name);
	free(v->workdir);
	if (v->vd != NULL)
		VSM_Delete(v->vd);

	/*
	 * We do not delete the workdir, it may contain stuff people
	 * want (coredumps, shmlog/stats etc), and trying to divine
	 * "may want" is just too much trouble.  Leave it around and
	 * nuke it at the start of the next test-run.
	 */

	/* XXX: MEMLEAK (?) */
	FREE_OBJ(v);
}

/**********************************************************************
 * Varnish listener
 */

static void *
varnish_thread(void *priv)
{
	struct varnish *v;
	char buf[65536];
	struct pollfd fds[1];
	int i;

	CAST_OBJ_NOTNULL(v, priv, VARNISH_MAGIC);
	(void)VTCP_nonblocking(v->fds[0]);
	while (1) {
		memset(fds, 0, sizeof fds);
		fds->fd = v->fds[0];
		fds->events = POLLIN;
		i = poll(fds, 1, 1000);
		if (i == 0)
			continue;
		if (fds->revents & POLLIN) {
			i = read(v->fds[0], buf, sizeof buf - 1);
			if (i > 0) {
				buf[i] = '\0';
				vtc_dump(v->vl, 3, "debug", buf, -2);
			}
		}
		if (fds->revents & (POLLERR|POLLHUP)) {
			vtc_log(v->vl, 4, "STDOUT poll 0x%x", fds->revents);
			break;
		}
	}
	return (NULL);
}

/**********************************************************************
 * Launch a Varnish
 */

static void
varnish_launch(struct varnish *v)
{
	struct vsb *vsb, *vsb1;
	int i, nfd;
	char abuf[128], pbuf[128];
	struct pollfd fd[2];
	enum VCLI_status_e u;
	const char *err;
	char *r = NULL;

	v->vd = VSM_New();

	/* Create listener socket */
	v->cli_fd = VTCP_listen_on("127.0.0.1:0", NULL, 1, &err);
	if (err != NULL)
		vtc_log(v->vl, 0, "Create CLI listen socket failed: %s", err);
	assert(v->cli_fd > 0);
	VTCP_myname(v->cli_fd, abuf, sizeof abuf, pbuf, sizeof pbuf);

	AZ(VSB_finish(v->args));
	vtc_log(v->vl, 2, "Launch");
	vsb = VSB_new_auto();
	AN(vsb);
	VSB_printf(vsb, "cd ${pwd} &&");
	VSB_printf(vsb, " exec ${varnishd} %s -d -n %s",
	    v->jail, v->workdir);
	if (vtc_witness)
		VSB_cat(vsb, " -p debug=+witness");
	if (leave_temp)
		VSB_cat(vsb, " -p debug=+vsm_keep");
	VSB_printf(vsb, " -l 2m,1m,-");
	VSB_printf(vsb, " -p auto_restart=off");
	VSB_printf(vsb, " -p syslog_cli_traffic=off");
	VSB_printf(vsb, " -p sigsegv_handler=on");
	VSB_printf(vsb, " -p thread_pool_min=10");
	VSB_printf(vsb, " -p debug=+vtc_mode");
	VSB_printf(vsb, " -a '%s'", "127.0.0.1:0");
	if (v->proto != NULL)
		VSB_printf(vsb, ",%s", v->proto);
	VSB_printf(vsb, " -M '%s %s'", abuf, pbuf);
	VSB_printf(vsb, " -P %s/varnishd.pid", v->workdir);
	VSB_printf(vsb, " %s", VSB_data(v->args));
	AZ(VSB_finish(vsb));
	vtc_log(v->vl, 3, "CMD: %s", VSB_data(vsb));
	vsb1 = macro_expand(v->vl, VSB_data(vsb));
	AN(vsb1);
	VSB_delete(vsb);
	vsb = vsb1;
	vtc_log(v->vl, 3, "CMD: %s", VSB_data(vsb));
	AZ(pipe(&v->fds[0]));
	AZ(pipe(&v->fds[2]));
	v->pid = fork();
	assert(v->pid >= 0);
	if (v->pid == 0) {
		AZ(dup2(v->fds[0], 0));
		assert(dup2(v->fds[3], 1) == 1);
		assert(dup2(1, 2) == 2);
		AZ(close(v->fds[0]));
		AZ(close(v->fds[1]));
		AZ(close(v->fds[2]));
		AZ(close(v->fds[3]));
		VSUB_closefrom(STDERR_FILENO + 1);
		AZ(execl("/bin/sh", "/bin/sh", "-c", VSB_data(vsb), (char*)0));
		exit(1);
	} else {
		vtc_log(v->vl, 3, "PID: %ld", (long)v->pid);
		macro_def(v->vl, v->name, "pid", "%ld", (long)v->pid);
		macro_def(v->vl, v->name, "name", "%s", v->workdir);
	}
	AZ(close(v->fds[0]));
	AZ(close(v->fds[3]));
	v->fds[0] = v->fds[2];
	v->fds[2] = v->fds[3] = -1;
	VSB_delete(vsb);
	AZ(pthread_create(&v->tp, NULL, varnish_thread, v));
	AZ(pthread_create(&v->tp_vsl, NULL, varnishlog_thread, v));

	/* Wait for the varnish to call home */
	memset(fd, 0, sizeof fd);
	fd[0].fd = v->cli_fd;
	fd[0].events = POLLIN;
	fd[1].fd = v->fds[1];
	fd[1].events = POLLIN;
	i = poll(fd, 2, 10000);
	vtc_log(v->vl, 4, "CLIPOLL %d 0x%x 0x%x",
	    i, fd[0].revents, fd[1].revents);
	if (i == 0) {
		vtc_log(v->vl, 0, "FAIL timeout waiting for CLI connection");
		AZ(close(v->cli_fd));
		v->cli_fd = -1;
		return;
	}
	if (fd[1].revents & POLLHUP) {
		vtc_log(v->vl, 0, "FAIL debug pipe closed");
		AZ(close(v->cli_fd));
		v->cli_fd = -1;
		return;
	}
	if (!(fd[0].revents & POLLIN)) {
		vtc_log(v->vl, 0, "FAIL CLI connection wait failure");
		AZ(close(v->cli_fd));
		v->cli_fd = -1;
		return;
	}
	nfd = accept(v->cli_fd, NULL, NULL);
	if (nfd < 0) {
		AZ(close(v->cli_fd));
		v->cli_fd = -1;
		vtc_log(v->vl, 0, "FAIL no CLI connection accepted");
		return;
	}

	AZ(close(v->cli_fd));
	v->cli_fd = nfd;

	vtc_log(v->vl, 3, "CLI connection fd = %d", v->cli_fd);
	assert(v->cli_fd >= 0);

	/* Receive the banner or auth response */
	u = varnish_ask_cli(v, NULL, &r);
	if (vtc_error)
		return;
	if (u != CLIS_AUTH)
		vtc_log(v->vl, 0, "CLI auth demand expected: %u %s", u, r);

	bprintf(abuf, "%s/_.secret", v->workdir);
	nfd = open(abuf, O_RDONLY);
	assert(nfd >= 0);

	assert(sizeof abuf >= CLI_AUTH_RESPONSE_LEN + 7);
	strcpy(abuf, "auth ");
	VCLI_AuthResponse(nfd, r, abuf + 5);
	AZ(close(nfd));
	free(r);
	r = NULL;
	strcat(abuf, "\n");

	u = varnish_ask_cli(v, abuf, &r);
	if (vtc_error)
		return;
	if (u != CLIS_OK)
		vtc_log(v->vl, 0, "CLI auth command failed: %u %s", u, r);
	free(r);

	(void)VSM_n_Arg(v->vd, v->workdir);
	AZ(VSM_Open(v->vd));
}

/**********************************************************************
 * Start a Varnish
 */

static void
varnish_start(struct varnish *v)
{
	enum VCLI_status_e u;
	char *resp = NULL, *h, *p;

	if (v->cli_fd < 0)
		varnish_launch(v);
	if (vtc_error)
		return;
	vtc_log(v->vl, 2, "Start");
	u = varnish_ask_cli(v, "start", &resp);
	if (vtc_error)
		return;
	if (u != CLIS_OK)
		vtc_log(v->vl, 0, "CLI start command failed: %u %s", u, resp);
	wait_running(v);
	free(resp);
	resp = NULL;
	u = varnish_ask_cli(v, "debug.xid 999", &resp);
	if (vtc_error)
		return;
	if (u != CLIS_OK)
		vtc_log(v->vl, 0, "CLI debug.xid command failed: %u %s",
		    u, resp);
	free(resp);
	resp = NULL;
	u = varnish_ask_cli(v, "debug.listen_address", &resp);
	if (vtc_error)
		return;
	if (u != CLIS_OK)
		vtc_log(v->vl, 0,
		    "CLI debug.listen_address command failed: %u %s", u, resp);
	h = resp;
	p = strchr(h, '\n');
	if (p != NULL)
		*p = '\0';
	p = strchr(h, ' ');
	AN(p);
	*p++ = '\0';
	vtc_log(v->vl, 2, "Listen on %s %s", h, p);
	macro_def(v->vl, v->name, "addr", "%s", h);
	macro_def(v->vl, v->name, "port", "%s", p);
	macro_def(v->vl, v->name, "sock", "%s %s", h, p);
}

/**********************************************************************
 * Stop a Varnish
 */

static void
varnish_stop(struct varnish *v)
{
	char *r;

	if (v->cli_fd < 0)
		varnish_launch(v);
	if (vtc_error)
		return;
	vtc_log(v->vl, 2, "Stop");
	(void)varnish_ask_cli(v, "stop", NULL);
	while (1) {
		r = NULL;
		(void)varnish_ask_cli(v, "status", &r);
		AN(r);
		if (!strcmp(r, "Child in state stopped"))
			break;
		free(r);
		(void)sleep (1);
		/* XXX: should fail eventually */
	}
}

/**********************************************************************
 * Cleanup
 */

static void
varnish_cleanup(struct varnish *v)
{
	void *p;
	int status, r;
	struct rusage ru;

	/* Give the VSL log time to finish */
	while (v->vsl_idle < 10)
		(void)usleep(200000);

	/* Close the CLI connection */
	AZ(close(v->cli_fd));
	v->cli_fd = -1;

	/* Close the STDIN connection. */
	AZ(close(v->fds[1]));

	/* Wait until STDOUT+STDERR closes */
	AZ(pthread_join(v->tp, &p));
	AZ(close(v->fds[0]));

	r = wait4(v->pid, &status, 0, &ru);
	v->pid = 0;
	vtc_log(v->vl, 2, "R %d Status: %04x (u %.6f s %.6f)", r, status,
	    ru.ru_utime.tv_sec + 1e-6 * ru.ru_utime.tv_usec,
	    ru.ru_stime.tv_sec + 1e-6 * ru.ru_stime.tv_usec
	);

	/* Pick up the VSL thread */
	AZ(pthread_join(v->tp_vsl, &p));

	if (WIFEXITED(status) && (WEXITSTATUS(status) == 0))
		return;
#ifdef WCOREDUMP
	vtc_log(v->vl, 0, "Bad exit code: %04x sig %x exit %x core %x",
	    status, WTERMSIG(status), WEXITSTATUS(status),
	    WCOREDUMP(status));
#else
	vtc_log(v->vl, 0, "Bad exit code: %04x sig %x exit %x",
	    status, WTERMSIG(status), WEXITSTATUS(status));
#endif
}

/**********************************************************************
 * Wait for a Varnish
 */

static void
varnish_wait(struct varnish *v)
{
	char *resp;

	if (v->cli_fd < 0)
		return;

	vtc_log(v->vl, 2, "Wait");

	if (!vtc_error) {
		/* Do a backend.list to log if child is still running */
		varnish_ask_cli(v, "backend.list", &resp);
	}

	/* Then stop it */
	varnish_stop(v);

	varnish_cleanup(v);
}


/**********************************************************************
 * Ask a CLI question
 */

static void
varnish_cli(struct varnish *v, const char *cli, unsigned exp)
{
	enum VCLI_status_e u;

	if (v->cli_fd < 0)
		varnish_launch(v);
	if (vtc_error)
		return;
	u = varnish_ask_cli(v, cli, NULL);
	vtc_log(v->vl, 2, "CLI %03u <%s>", u, cli);
	if (exp != 0 && exp != (unsigned)u)
		vtc_log(v->vl, 0, "FAIL CLI response %u expected %u", u, exp);
}

/**********************************************************************
 * Load a VCL program
 */

static void
varnish_vcl(struct varnish *v, const char *vcl, enum VCLI_status_e expect,
    char **resp)
{
	struct vsb *vsb;
	enum VCLI_status_e u;

	if (v->cli_fd < 0)
		varnish_launch(v);
	if (vtc_error)
		return;
	vsb = VSB_new_auto();
	AN(vsb);

	VSB_printf(vsb, "vcl.inline vcl%d << %s\nvcl 4.0;\n%s\n%s\n",
	    ++v->vcl_nbr, NONSENSE, vcl, NONSENSE);
	AZ(VSB_finish(vsb));

	u = varnish_ask_cli(v, VSB_data(vsb), resp);
	if (u != expect) {
		VSB_delete(vsb);
		vtc_log(v->vl, 0,
		    "VCL compilation got %u expected %u",
		    u, expect);
		return;
	}
	if (u == CLIS_OK) {
		VSB_clear(vsb);
		VSB_printf(vsb, "vcl.use vcl%d", v->vcl_nbr);
		AZ(VSB_finish(vsb));
		u = varnish_ask_cli(v, VSB_data(vsb), NULL);
		assert(u == CLIS_OK);
	} else {
		vtc_log(v->vl, 2, "VCL compilation failed (as expected)");
	}
	VSB_delete(vsb);
}

/**********************************************************************
 * Load a VCL program prefixed by backend decls for our servers
 */

static void
varnish_vclbackend(struct varnish *v, const char *vcl)
{
	struct vsb *vsb, *vsb2;
	enum VCLI_status_e u;

	if (v->cli_fd < 0)
		varnish_launch(v);
	if (vtc_error)
		return;
	vsb = VSB_new_auto();
	AN(vsb);

	vsb2 = VSB_new_auto();
	AN(vsb2);

	VSB_printf(vsb2, "vcl 4.0;\n");

	cmd_server_genvcl(vsb2);

	AZ(VSB_finish(vsb2));

	VSB_printf(vsb, "vcl.inline vcl%d << %s\n%s\n%s\n%s\n",
	    ++v->vcl_nbr, NONSENSE, VSB_data(vsb2), vcl, NONSENSE);
	AZ(VSB_finish(vsb));

	u = varnish_ask_cli(v, VSB_data(vsb), NULL);
	if (u != CLIS_OK) {
		VSB_delete(vsb);
		VSB_delete(vsb2);
		vtc_log(v->vl, 0, "FAIL VCL does not compile");
		return;
	}
	VSB_clear(vsb);
	VSB_printf(vsb, "vcl.use vcl%d", v->vcl_nbr);
	AZ(VSB_finish(vsb));
	u = varnish_ask_cli(v, VSB_data(vsb), NULL);
	assert(u == CLIS_OK);
	VSB_delete(vsb);
	VSB_delete(vsb2);
}

/**********************************************************************
 * Check statistics
 */

struct stat_priv {
	char target_type[256];
	char target_ident[256];
	char target_name[256];
	uintmax_t val;
	const struct varnish *v;
};

static int
do_stat_cb(void *priv, const struct VSC_point * const pt)
{
	struct stat_priv *sp = priv;

	if (pt == NULL)
		return(0);

	if (strcmp(pt->section->type, sp->target_type))
		return(0);
	if (strcmp(pt->section->ident, sp->target_ident))
		return(0);
	if (strcmp(pt->desc->name, sp->target_name))
		return(0);

	AZ(strcmp(pt->desc->ctype, "uint64_t"));
	sp->val = *(const volatile uint64_t*)pt->ptr;
	return (1);
}

static void
varnish_expect(const struct varnish *v, char * const *av)
{
	uint64_t ref;
	int good;
	char *r;
	char *p;
	char *q;
	int i, not = 0;
	struct stat_priv sp;

	r = av[0];
	if (r[0] == '!') {
		not = 1;
		r++;
		AZ(av[1]);
	} else {
		AN(av[1]);
		AN(av[2]);
	}
	p = strchr(r, '.');
	if (p == NULL) {
		strcpy(sp.target_type, "MAIN");
		sp.target_ident[0] = '\0';
		bprintf(sp.target_name, "%s", r);
	} else {
		bprintf(sp.target_type, "%.*s", (int)(p - r), r);
		p++;
		q = strrchr(p, '.');
		if (q == NULL) {
			sp.target_ident[0] = '\0';
			bprintf(sp.target_name, "%s", p);
		} else {
			bprintf(sp.target_ident, "%.*s", (int)(q - p), p);
			bprintf(sp.target_name, "%s", q + 1);
		}
	}

	sp.val = 0;
	sp.v = v;
	ref = 0;
	good = 0;
	for (i = 0; i < 10; i++, (void)usleep(100000)) {
		if (VSM_Abandoned(v->vd)) {
			VSM_Close(v->vd);
			good = VSM_Open(v->vd);
		}
		if (good < 0)
			continue;

		good = VSC_Iter(v->vd, NULL, do_stat_cb, &sp);
		if (!good) {
			good = -2;
			continue;
		}

		if (not) {
			vtc_log(v->vl, 0, "Found (not expected): %s", av[0]+1);
			return;
		}

		good = 0;
		ref = strtoumax(av[2], &p, 0);
		if (ref == UINTMAX_MAX || *p)
			vtc_log(v->vl, 0, "Syntax error in number (%s)", av[2]);
		if      (!strcmp(av[1], "==")) { if (sp.val == ref) good = 1; }
		else if (!strcmp(av[1], "!=")) { if (sp.val != ref) good = 1; }
		else if (!strcmp(av[1], ">"))  { if (sp.val > ref)  good = 1; }
		else if (!strcmp(av[1], "<"))  { if (sp.val < ref)  good = 1; }
		else if (!strcmp(av[1], ">=")) { if (sp.val >= ref) good = 1; }
		else if (!strcmp(av[1], "<=")) { if (sp.val <= ref) good = 1; }
		else {
			vtc_log(v->vl, 0, "comparison %s unknown", av[1]);
		}
		if (good)
			break;
	}
	if (good == -1) {
		vtc_log(v->vl, 0, "VSM error: %s", VSM_Error(v->vd));
	}
	if (good == -2) {
		if (not) {
			vtc_log(v->vl, 2, "not found (as expected): %s",
			    av[0] + 1);
			return;
		}
		vtc_log(v->vl, 0, "stats field %s unknown", av[0]);
	}

	if (good == 1) {
		vtc_log(v->vl, 2, "as expected: %s (%ju) %s %s",
		    av[0], sp.val, av[1], av[2]);
	} else {
		vtc_log(v->vl, 0, "Not true: %s (%ju) %s %s (%ju)",
		    av[0], (uintmax_t)sp.val, av[1], av[2], (uintmax_t)ref);
	}
}
/* SECTION: varnish varnish
 *
 * Define and interact with varnish instances.
 *
 * To define a Varnish server, you'll use this syntax::
 *
 *         varnish vNAME [-arg STRING] [-vcl STRING] [-vcl+backend STRING]
 *	                 [-errvcl STRING STRING] [-jail STRING] [-proto PROXY]
 *
 * The first ``varnish vNAME`` invocation will start the varnishd master
 * process in the background, waiting for the ``-start`` switch to actually
 * start the child.
 *
 * With:
 *
 * vNAME
 *         Identify the Varnish server with a string, it must starts with 'v'.
 *
 * \-arg STRING
 *         Pass an argument to varnishd, for example "-h simple_list".
 *
 * \-vcl STRING
 *         Specify the VCL to load on this Varnish instance. You'll probably
 *         want to use multi-lines strings for this ({...}).
 *
 * \-vcl+backend STRING
 *         Do the exact same thing as -vcl, but adds the definition block of
 *         known backends (ie. already defined).
 *
 * \-errvcl STRING1 STRING2
 *         Load STRING2 as VCL, expecting it to fail, and Varnish to send an
 *         error string matching STRING2
 *
 * \-jail STRING
 *         Look at ``man varnishd`` (-j) for more information.
 *
 * \-proto PROXY
 *         Have Varnish use the proxy protocol. Note that PROXY here is the
 *         actual string.
 *
 * You can decide to start the Varnish instance and/or wait for several events::
 *
 *         varnish vNAME [-start] [-wait] [-wait-running] [-wait-stopped]
 *
 * \-start
 *         Start the child process.
 *
 * \-stop
 *         Stop the child process.
 *
 * \-wait
 *         Wait for that instance to terminate.
 *
 * \-wait-running
 *         Wait for the Varnish child process to be started.
 *
 * \-wait-stopped
 *         Wait for the Varnish child process to stop.
 *
 * \-cleanup
 *         Once Varnish is stopped, clean everything after it. This is only used
 *         in one test and you should never need it.
 *
 * Once Varnish is started, you can talk to it (as you would through
 * ``varnishadm``) with these additional switches::
 *
 *         varnish vNAME [-cli STRING] [-cliok STRING] [-clierr STRING]
 *                       [-expect STRING OP NUMBER]
 *
 * \-cli|-cliok|-clierr STRING
 *         All three of these will send STRING to the CLI, the only difference
 *         is what they expect the return code to be. -cli doesn't expect
 *         anything, -cliok expects 200 and -clierr expects not 200.
 *
 * \-expect STRING OP NUMBER
 *         Look into the VSM and make sure the counter identified by STRING has
 *         a correct value. OP can be ==, >, >=, <, <=. For example::
 *
 *                 varnish v1 -expect SMA.s1.g_space > 1000000
 */

void
cmd_varnish(CMD_ARGS)
{
	struct varnish *v, *v2;

	(void)priv;
	(void)cmd;
	(void)vl;

	if (av == NULL) {
		/* Reset and free */
		VTAILQ_FOREACH_SAFE(v, &varnishes, list, v2) {
			if (v->cli_fd >= 0)
				varnish_wait(v);
			VTAILQ_REMOVE(&varnishes, v, list);
			varnish_delete(v);
		}
		return;
	}

	AZ(strcmp(av[0], "varnish"));
	av++;

	VTAILQ_FOREACH(v, &varnishes, list)
		if (!strcmp(v->name, av[0]))
			break;
	if (v == NULL)
		v = varnish_new(av[0]);
	av++;

	for (; *av != NULL; av++) {
		if (vtc_error)
			break;
		if (!strcmp(*av, "-arg")) {
			AN(av[1]);
			AZ(v->pid);
			VSB_cat(v->args, " ");
			VSB_cat(v->args, av[1]);
			av++;
			continue;
		}
		if (!strcmp(*av, "-cli")) {
			AN(av[1]);
			varnish_cli(v, av[1], 0);
			av++;
			continue;
		}
		if (!strcmp(*av, "-clierr")) {
			AN(av[1]);
			AN(av[2]);
			varnish_cli(v, av[2], atoi(av[1]));
			av += 2;
			continue;
		}
		if (!strcmp(*av, "-cleanup")) {
			AZ(av[1]);
			varnish_cleanup(v);
			continue;
		}
		if (!strcmp(*av, "-cliok")) {
			AN(av[1]);
			varnish_cli(v, av[1], (unsigned)CLIS_OK);
			av++;
			continue;
		}
		if (!strcmp(*av, "-errvcl")) {
			char *r = NULL;
			AN(av[1]);
			AN(av[2]);
			varnish_vcl(v, av[2], CLIS_PARAM, &r);
			if (strstr(r, av[1]) == NULL)
				vtc_log(v->vl, 0,
				    "Did not find expected string: (\"%s\")",
				    av[1]);
			else
				vtc_log(v->vl, 3,
				    "Found expected string: (\"%s\")",
				    av[1]);
			free(r);
			av += 2;
			continue;
		}
		if (!strcmp(*av, "-expect")) {
			av++;
			varnish_expect(v, av);
			av += 2;
			continue;
		}
		if (!strcmp(*av, "-jail")) {
			AN(av[1]);
			AZ(v->pid);
			REPLACE(v->jail, av[1]);
			av++;
			continue;
		}
		if (!strcmp(*av, "-proto")) {
			AN(av[1]);
			AZ(v->pid);
			REPLACE(v->proto, av[1]);
			av++;
			continue;
		}
		if (!strcmp(*av, "-start")) {
			varnish_start(v);
			continue;
		}
		if (!strcmp(*av, "-stop")) {
			varnish_stop(v);
			continue;
		}
		if (!strcmp(*av, "-vcl")) {
			AN(av[1]);
			varnish_vcl(v, av[1], CLIS_OK, NULL);
			av++;
			continue;
		}
		if (!strcmp(*av, "-vcl+backend")) {
			AN(av[1]);
			varnish_vclbackend(v, av[1]);
			av++;
			continue;
		}
		if (!strcmp(*av, "-wait-stopped")) {
			wait_stopped(v);
			continue;
		}
		if (!strcmp(*av, "-wait-running")) {
			wait_running(v);
			continue;
		}
		if (!strcmp(*av, "-wait")) {
			varnish_wait(v);
			continue;
		}
		vtc_log(v->vl, 0, "Unknown varnish argument: %s", *av);
	}
}
