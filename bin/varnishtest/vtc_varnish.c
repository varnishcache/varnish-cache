/*-
 * Copyright (c) 2008-2014 Varnish Software AS
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
#include "vtcp.h"
#include "vtim.h"

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

	struct VSM_data		*vd;		/* vsc use */

	unsigned		vsl_tag_count[256];
	unsigned		vsl_sleep;
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
		assert(i == strlen(cmd));
		i = write(v->cli_fd, "\n", 1);
		assert(i == 1);
	}
	i = VCLI_ReadResult(v->cli_fd, &retval, &r, 20.0);
	if (i != 0) {
		vtc_log(v->vl, 0, "CLI failed (%s) = %d %u %s",
		    cmd, i, retval, r);
		return ((enum VCLI_status_e)retval);
	}
	assert(i == 0);
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
	char *r;
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
		(void)usleep(200000);
	}
}
/**********************************************************************
 *
 */

static void
wait_running(const struct varnish *v)
{
	char *r;
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
	int type, i;

	CAST_OBJ_NOTNULL(v, priv, VARNISH_MAGIC);

	vsl = VSL_New();
	AN(vsl);
	vsm = VSM_New();
	AN(vsm);
	(void)VSM_n_Arg(vsm, v->workdir);

	c = NULL;
	while (v->pid) {
		if (c == NULL) {
			VTIM_sleep(0.1);
			if (VSM_Open(vsm)) {
				VSM_ResetError(vsm);
				continue;
			}
			c = VSL_CursorVSM(vsl, vsm, 1);
			if (c == NULL) {
				VSL_ResetError(vsl);
				continue;
			}
		}
		AN(c);

		i = VSL_Next(c);
		if (i == 0) {
			/* Nothing to do but wait */
			VTIM_sleep(0.01);
			continue;
		} else if (i == -2) {
			/* Abandoned - try reconnect */
			VSL_DeleteCursor(c);
			c = NULL;
			VSM_Close(vsm);
			continue;
		} else if (i != 1)
			break;

		tag = VSL_TAG(c->rec.ptr);
		vxid = VSL_ID(c->rec.ptr);
		if (tag == SLT__Batch) {
			tagname = "Batch";
			len = 0;
			continue;
		} else {
			tagname = VSL_tags[tag];
			len = VSL_LEN(c->rec.ptr);
		}
		type = VSL_CLIENT(c->rec.ptr) ? 'c' : VSL_BACKEND(c->rec.ptr) ?
		    'b' : '-';
		data = VSL_CDATA(c->rec.ptr);
		v->vsl_tag_count[tag]++;
		vtc_log(v->vl, 4, "vsl| %10u %-15s %c %.*s", vxid, tagname,
		    type, (int)len, data);
	}

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
	char buf[BUFSIZ];
	struct pollfd *fds, fd;
	int i;

	CAST_OBJ_NOTNULL(v, priv, VARNISH_MAGIC);
	(void)VTCP_nonblocking(v->fds[0]);
	while (1) {
		fds = &fd;
		memset(fds, 0, sizeof *fds);
		fds->fd = v->fds[0];
		fds->events = POLLIN;
		i = poll(fds, 1, 1000);
		if (i == 0)
			continue;
		if (fds->revents & (POLLERR|POLLHUP)) {
			vtc_log(v->vl, 4, "STDOUT poll 0x%x", fds->revents);
			break;
		}
		i = read(v->fds[0], buf, sizeof buf - 1);
		if (i <= 0)
			break;
		buf[i] = '\0';
		vtc_dump(v->vl, 3, "debug", buf, -2);
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
	int i, nfd, nap;
	struct vss_addr **ap;
	char abuf[128], pbuf[128];
	struct pollfd fd[2];
	enum VCLI_status_e u;
	char *r;

	v->vd = VSM_New();

	/* Create listener socket */
	nap = VSS_resolve("127.0.0.1", "0", &ap);
	AN(nap);
	v->cli_fd = VSS_listen(ap[0], 1);
	assert(v->cli_fd > 0);
	VTCP_myname(v->cli_fd, abuf, sizeof abuf, pbuf, sizeof pbuf);

	AZ(VSB_finish(v->args));
	vtc_log(v->vl, 2, "Launch");
	vsb = VSB_new_auto();
	AN(vsb);
	VSB_printf(vsb, "cd ${pwd} &&");
	VSB_printf(vsb, " ${varnishd} -d -d -n %s", v->workdir);
	VSB_printf(vsb, " -l 2m,1m,-");
	VSB_printf(vsb, " -p auto_restart=off");
	VSB_printf(vsb, " -p syslog_cli_traffic=off");
	VSB_printf(vsb, " -p sigsegv_handler=on");
	VSB_printf(vsb, " -a '%s'", "127.0.0.1:0");
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
		assert(dup2(v->fds[0], 0) == 0);
		assert(dup2(v->fds[3], 1) == 1);
		assert(dup2(1, 2) == 2);
		AZ(close(v->fds[0]));
		AZ(close(v->fds[1]));
		AZ(close(v->fds[2]));
		AZ(close(v->fds[3]));
		for (i = 3; i <getdtablesize(); i++)
			(void)close(i);
		AZ(execl("/bin/sh", "/bin/sh", "-c", VSB_data(vsb), (char*)0));
		exit(1);
	} else {
		vtc_log(v->vl, 3, "PID: %ld", (long)v->pid);
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
		return;
	}
	if (fd[1].revents & POLLHUP) {
		vtc_log(v->vl, 0, "FAIL debug pipe closed");
		return;
	}
	if (!(fd[0].revents & POLLIN)) {
		vtc_log(v->vl, 0, "FAIL CLI connection wait failure");
		return;
	}
	nfd = accept(v->cli_fd, NULL, NULL);
	if (nfd < 0) {
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
	char *resp, *h, *p, *q;

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
	u = varnish_ask_cli(v, "debug.xid 999", &resp);
	if (vtc_error)
		return;
	if (u != CLIS_OK)
		vtc_log(v->vl, 0, "CLI debug.xid command failed: %u %s",
		    u, resp);
	free(resp);
	u = varnish_ask_cli(v, "debug.listen_address", &resp);
	if (vtc_error)
		return;
	if (u != CLIS_OK)
		vtc_log(v->vl, 0,
		    "CLI debug.listen_address command failed: %u %s", u, resp);
	h = resp;
	p = strchr(h, ' ');
	AN(p);
	*p++ = '\0';
	q = strchr(p, '\n');
	AN(q);
	*q = '\0';
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
	macro_undef(v->vl, v->name, "addr");
	macro_undef(v->vl, v->name, "port");
	macro_undef(v->vl, v->name, "sock");
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
 * Wait for a Varnish
 */

static void
varnish_wait(struct varnish *v)
{
	void *p;
	int status, r;
	struct rusage ru;

	if (v->cli_fd < 0)
		return;
	if (vtc_error)
		(void)sleep(1);	/* give panic messages a chance */
	varnish_stop(v);
	vtc_log(v->vl, 2, "Wait");
	AZ(close(v->cli_fd));
	v->cli_fd = -1;

	(void)close(v->fds[1]);		/* May already have been closed */

	AZ(pthread_join(v->tp, &p));
	AZ(close(v->fds[0]));
	r = wait4(v->pid, &status, 0, &ru);
	v->pid = 0;
	vtc_log(v->vl, 2, "R %d Status: %04x (u %.6f s %.6f)", r, status,
	    ru.ru_utime.tv_sec + 1e-6 * ru.ru_utime.tv_usec,
	    ru.ru_stime.tv_sec + 1e-6 * ru.ru_stime.tv_usec
	);
	AZ(pthread_join(v->tp_vsl, &p));
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
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
	char target[256];
	uintmax_t val;
};

static int
do_stat_cb(void *priv, const struct VSC_point * const pt)
{
	struct stat_priv *sp = priv;
	const char *p = sp->target;
	int i;

	if (pt == NULL)
		return(0);
	if (strcmp(pt->section->type, "")) {
		i = strlen(pt->section->type);
		if (memcmp(pt->section->type, p, i))
			return (0);
		p += i;
		if (*p != '.')
			return (0);
		p++;
	}
	if (strcmp(pt->section->ident, "")) {
		i = strlen(pt->section->ident);
		if (memcmp(pt->section->ident, p, i))
			return (0);
		p += i;
		if (*p != '.')
			return (0);
		p++;
	}
	if (strcmp(pt->desc->name, p))
		return (0);

	assert(!strcmp(pt->desc->fmt, "uint64_t"));
	sp->val = *(const volatile uint64_t*)pt->ptr;
	return (1);
}

static void
varnish_expect(const struct varnish *v, char * const *av) {
	uint64_t ref;
	int good;
	char *p;
	int i;
	const char *prefix = "";
	struct stat_priv sp;

	if (NULL == strchr(av[0], '.'))
		prefix = "MAIN.";
	snprintf(sp.target, sizeof sp.target, "%s%s", prefix, av[0]);
	sp.target[sizeof sp.target - 1] = '\0';
	sp.val = 0;
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
		VSM_ResetError(v->vd);
	} else if (good == -2) {
		vtc_log(v->vl, 0, "stats field %s unknown", av[0]);
	} else if (good) {
		vtc_log(v->vl, 2, "as expected: %s (%ju) %s %s",
		    av[0], sp.val, av[1], av[2]);
	} else {
		vtc_log(v->vl, 0, "Not true: %s (%ju) %s %s (%ju)",
		    av[0], (uintmax_t)sp.val, av[1], av[2], (uintmax_t)ref);
	}
}

/**********************************************************************
 * Varnish server cmd dispatch
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

	assert(!strcmp(av[0], "varnish"));
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
		if (!strcmp(*av, "-cliok")) {
			AN(av[1]);
			varnish_cli(v, av[1], (unsigned)CLIS_OK);
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
		if (!strcmp(*av, "-start")) {
			varnish_start(v);
			continue;
		}
		if (!strcmp(*av, "-vcl+backend")) {
			AN(av[1]);
			varnish_vclbackend(v, av[1]);
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
		if (!strcmp(*av, "-vcl")) {
			AN(av[1]);
			varnish_vcl(v, av[1], CLIS_OK, NULL);
			av++;
			continue;
		}
		if (!strcmp(*av, "-stop")) {
			varnish_stop(v);
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
		if (!strcmp(*av, "-expect")) {
			av++;
			varnish_expect(v, av);
			av += 2;
			continue;
		}
		vtc_log(v->vl, 0, "Unknown varnish argument: %s", *av);
	}
}
