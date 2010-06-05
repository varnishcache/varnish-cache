/*
 * Copyright (c) 2008-2010 Redpill Linpro AS
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

#include "svnid.h"
SVNID("$Id$")

#include <stdio.h>

#include <limits.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <poll.h>
#include <pthread.h>
#include <inttypes.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>

#include "vqueue.h"
#include "miniobj.h"
#include "libvarnish.h"
#include "varnishapi.h"
#include "cli.h"
#include "cli_common.h"
#include "vss.h"
#include "vsb.h"

#include "vtc.h"

struct varnish {
	unsigned		magic;
#define VARNISH_MAGIC		0x208cd8e3
	char			*name;
	struct vtclog		*vl;
	struct vtclog		*vl1;
	VTAILQ_ENTRY(varnish)	list;

	struct varnish_stats	*stats;

	struct vsb		*storage;

	struct vsb		*args;
	int			fds[4];
	pid_t			pid;

	pthread_t		tp;

	int			cli_fd;
	int			vcl_nbr;
	char			*workdir;

	struct VSL_data 	*vd;
};

static VTAILQ_HEAD(, varnish)	varnishes =
    VTAILQ_HEAD_INITIALIZER(varnishes);

/**********************************************************************
 * Ask a question over CLI
 */

static enum cli_status_e
varnish_ask_cli(const struct varnish *v, const char *cmd, char **repl)
{
	int i;
	unsigned retval;
	char *r;

	if (cmd != NULL) {
		vtc_dump(v->vl, 4, "CLI TX", cmd);
		i = write(v->cli_fd, cmd, strlen(cmd));
		assert(i == strlen(cmd));
		i = write(v->cli_fd, "\n", 1);
		assert(i == 1);
	}
	i = cli_readres(v->cli_fd, &retval, &r, 20.0);
	if (i != 0) {
		vtc_log(v->vl, 0, "CLI failed (%s) = %d %u %s",
		    cmd, i, retval, r);
		return ((enum cli_status_e)retval);
	}
	assert(i == 0);
	vtc_log(v->vl, 3, "CLI RX  %u", retval);
	vtc_dump(v->vl, 4, "CLI RX", r);
	if (repl != NULL)
		*repl = r;
	else
		free(r);
	return ((enum cli_status_e)retval);
}

static void
varnish_cli_encode(struct vsb *vsb, const char *str)
{

	for (; *str != '\0'; str++) {
		switch (*str) {
		case '\\':
		case '"':
			vsb_printf(vsb, "\\%c", *str); break;
		case '\n':
			vsb_printf(vsb, "\\n"); break;
		case '\t':
			vsb_printf(vsb, "\\t"); break;
		default:
			if (isgraph(*str) || *str == ' ')
				vsb_putc(vsb, *str);
			else
				vsb_printf(vsb, "\\x%02x", *str);
		}
	}
}

/**********************************************************************
 * Allocate and initialize a varnish
 */

static struct varnish *
varnish_new(const char *name)
{
	struct varnish *v;
	char buf[1024];

	AN(name);
	ALLOC_OBJ(v, VARNISH_MAGIC);
	AN(v);
	REPLACE(v->name, name);

	bprintf(buf, "%s/%s", vtc_tmpdir, name);
	v->workdir = strdup(buf);
	AN(v->workdir);

	bprintf(buf, "rm -rf %s ; mkdir -p %s ; echo ' %ld' > %s/_S",
	    v->workdir, v->workdir, random(), v->workdir);
	AZ(system(buf));

	v->vl = vtc_logopen(name);
	AN(v->vl);

	v->vl1 = vtc_logopen(name);
	AN(v->vl1);

	if (*v->name != 'v')
		vtc_log(v->vl, 0, "Varnish name must start with 'v'");

	v->args = vsb_newauto();

	v->storage = vsb_newauto();
	vsb_printf(v->storage, "-sfile,%s,10M", v->workdir);
	vsb_finish(v->storage);

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
	VSL_Delete(v->vd);

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
	(void)TCP_nonblocking(v->fds[0]);
	while (1) {
		fds = &fd;
		memset(fds, 0, sizeof fds);
		fds->fd = v->fds[0];
		fds->events = POLLIN;
		i = poll(fds, 1, 1000);
		if (i == 0)
			continue;
		if (fds->revents & (POLLERR|POLLHUP))
			break;
		i = read(v->fds[0], buf, sizeof buf - 1);
		if (i <= 0)
			break;
		buf[i] = '\0';
		vtc_dump(v->vl1, 3, "debug", buf);
	}
	return (NULL);
}

/**********************************************************************
 * Launch a Varnish
 */

static void
varnish_launch(struct varnish *v)
{
	struct vsb *vsb;
	int i, nfd, nap;
	struct vss_addr **ap;
	char abuf[128], pbuf[128];
	struct pollfd fd;
	enum cli_status_e u;
	char *r;

	v->vd = VSL_New();

	/* Create listener socket */
	nap = VSS_resolve("127.0.0.1", "0", &ap);
	AN(nap);
	v->cli_fd = VSS_listen(ap[0], 1);
	TCP_myname(v->cli_fd, abuf, sizeof abuf, pbuf, sizeof pbuf);

	vsb_finish(v->args);
	AZ(vsb_overflowed(v->args));
	vtc_log(v->vl, 2, "Launch");
	vsb = vsb_newauto();
	AN(vsb);
	vsb_printf(vsb, "cd ../varnishd &&");
	vsb_printf(vsb, " ./varnishd -d -d -n %s", v->workdir);
	vsb_printf(vsb, " -l 10m,1m,-");
	vsb_printf(vsb, " -p auto_restart=off");
	vsb_printf(vsb, " -p syslog_cli_traffic=off");
	vsb_printf(vsb, " -a '%s'", "127.0.0.1:0");
	vsb_printf(vsb, " -S %s/_S", v->workdir);
	vsb_printf(vsb, " -M %s:%s", abuf, pbuf);
	vsb_printf(vsb, " -P %s/varnishd.pid", v->workdir);
	vsb_printf(vsb, " %s", vsb_data(v->storage));
	vsb_printf(vsb, " %s", vsb_data(v->args));
	vsb_finish(vsb);
	AZ(vsb_overflowed(vsb));
	vtc_log(v->vl, 3, "CMD: %s", vsb_data(vsb));
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
		AZ(execl("/bin/sh", "/bin/sh", "-c", vsb_data(vsb), NULL));
		exit(1);
	}
	AZ(close(v->fds[0]));
	AZ(close(v->fds[3]));
	v->fds[0] = v->fds[2];
	v->fds[2] = v->fds[3] = -1;
	vsb_delete(vsb);
	AZ(pthread_create(&v->tp, NULL, varnish_thread, v));

	/* Wait for the varnish to call home */
	fd.fd = v->cli_fd;
	fd.events = POLLIN;
	i = poll(&fd, 1, 6000);
	if (i != 1) {
		AZ(close(v->fds[1]));
		(void)kill(v->pid, SIGKILL);
		vtc_log(v->vl, 0, "FAIL timeout waiting for CLI connection");
		return;
	}
	nfd = accept(v->cli_fd, NULL, NULL);
	if (nfd < 0) {
		AZ(close(v->fds[1]));
		(void)kill(v->pid, SIGKILL);
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

	bprintf(abuf, "%s/_S", v->workdir);
	nfd = open(abuf, O_RDONLY);
	assert(nfd >= 0);

	assert(sizeof abuf >= CLI_AUTH_RESPONSE_LEN + 6);
	strcpy(abuf, "auth ");
	CLI_response(nfd, r, abuf + 5);
	AZ(close(nfd));
	free(r);
	strcat(abuf, "\n");

	u = varnish_ask_cli(v, abuf, &r);
	if (vtc_error)
		return;
	if (u != CLIS_OK)
		vtc_log(v->vl, 0, "CLI auth command failed: %u %s", u, r);
	free(r);

	if (v->stats != NULL)
		VSL_Close(v->vd);
	(void)VSL_Log_Arg(v->vd, 'n', v->workdir);
	v->stats = VSL_OpenStats(v->vd);
}

/**********************************************************************
 * Start a Varnish
 */

static void
varnish_start(struct varnish *v)
{
	enum cli_status_e u;
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
	free(resp);
	u = varnish_ask_cli(v, "debug.xid 1000", &resp);
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
	macro_def(v->vl, v->name, "sock", "%s:%s", h, p);
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
	macro_def(v->vl, v->name, "addr", NULL);
	macro_def(v->vl, v->name, "port", NULL);
	macro_def(v->vl, v->name, "sock", NULL);
	vtc_log(v->vl, 2, "Stop");
	(void)varnish_ask_cli(v, "stop", NULL);
	while (1) {
		(void)varnish_ask_cli(v, "status", &r);
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

	if (v->cli_fd < 0)
		return;
	if (vtc_error)
		(void)sleep(1);	/* give panic messages a chance */
	varnish_stop(v);
	vtc_log(v->vl, 2, "Wait");
	AZ(close(v->cli_fd));
	v->cli_fd = -1;

	AZ(close(v->fds[1]));

	AZ(pthread_join(v->tp, &p));
	AZ(close(v->fds[0]));
	r = wait4(v->pid, &status, 0, NULL);
	vtc_log(v->vl, 2, "R %d Status: %04x", r, status);
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
	enum cli_status_e u;

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
varnish_vcl(struct varnish *v, const char *vcl, enum cli_status_e expect)
{
	struct vsb *vsb;
	enum cli_status_e u;

	if (v->cli_fd < 0)
		varnish_launch(v);
	if (vtc_error)
		return;
	vsb = vsb_newauto();
	AN(vsb);

	v->vcl_nbr++;
	vsb_printf(vsb, "vcl.inline vcl%d \"", v->vcl_nbr);
	varnish_cli_encode(vsb, vcl);
	vsb_printf(vsb, "\"", *vcl);
	vsb_finish(vsb);
	AZ(vsb_overflowed(vsb));

	u = varnish_ask_cli(v, vsb_data(vsb), NULL);
	if (u != expect) {
		vsb_delete(vsb);
		vtc_log(v->vl, 0,
		    "VCL compilation got %u expected %u",
		    u, expect);
		return;
	}
	if (u == CLIS_OK) {
		vsb_clear(vsb);
		vsb_printf(vsb, "vcl.use vcl%d", v->vcl_nbr);
		vsb_finish(vsb);
		AZ(vsb_overflowed(vsb));
		u = varnish_ask_cli(v, vsb_data(vsb), NULL);
		assert(u == CLIS_OK);
	} else {
		vtc_log(v->vl, 2, "VCL compilation failed (as expected)");
	}
	vsb_delete(vsb);
}

/**********************************************************************
 * Load a VCL program prefixed by backend decls for our servers
 */

static void
varnish_vclbackend(struct varnish *v, const char *vcl)
{
	struct vsb *vsb, *vsb2;
	enum cli_status_e u;

	if (v->cli_fd < 0)
		varnish_launch(v);
	if (vtc_error)
		return;
	vsb = vsb_newauto();
	AN(vsb);

	vsb2 = vsb_newauto();
	AN(vsb2);

	cmd_server_genvcl(vsb2);
	vsb_finish(vsb2);
	AZ(vsb_overflowed(vsb2));

	v->vcl_nbr++;
	vsb_printf(vsb, "vcl.inline vcl%d \"", v->vcl_nbr);

	varnish_cli_encode(vsb, vsb_data(vsb2));

	varnish_cli_encode(vsb, vcl);

	vsb_printf(vsb, "\"", *vcl);
	vsb_finish(vsb);
	AZ(vsb_overflowed(vsb));

	u = varnish_ask_cli(v, vsb_data(vsb), NULL);
	if (u != CLIS_OK) {
		vsb_delete(vsb);
		vsb_delete(vsb2);
		vtc_log(v->vl, 0, "FAIL VCL does not compile");
		return;
	}
	vsb_clear(vsb);
	vsb_printf(vsb, "vcl.use vcl%d", v->vcl_nbr);
	vsb_finish(vsb);
	AZ(vsb_overflowed(vsb));
	u = varnish_ask_cli(v, vsb_data(vsb), NULL);
	assert(u == CLIS_OK);
	vsb_delete(vsb);
	vsb_delete(vsb2);
}

/**********************************************************************
 * Check statistics
 */

struct stat_priv {
	const char *target;
	uintmax_t val;
};

static int
do_stat_cb(void *priv, const struct vsl_statpt * const pt)
{
	struct stat_priv *sp = priv;
	const char *p = sp->target;
	int i;

	if (strcmp(pt->type, "")) {
		i = strlen(pt->type);
		if (memcmp(pt->type, p, i))
			return (0);
		p += i;
		if (*p != '.')
			return (0);
		p++;
	}
	if (strcmp(pt->ident, "")) {
		i = strlen(pt->ident);
		if (memcmp(pt->ident, p, i))
			return (0);
		p += i;
		if (*p != '.')
			return (0);
		p++;
	}
	if (strcmp(pt->nm, p))
		return (0);

	assert(!strcmp(pt->fmt, "uint64_t"));
	sp->val = *(const volatile uint64_t*)pt->ptr;
	return (1);
}

static void
varnish_expect(const struct varnish *v, char * const *av) {
	uint64_t ref;
	int good;
	char *p;
	int i;
	struct stat_priv sp;
	
	good = -1;

	sp.target = av[0];
	sp.val = 0;
	ref = 0;
	for (i = 0; i < 10; i++, (void)usleep(100000)) {

		good = -1;
		if (!VSL_IterStat(v->vd, do_stat_cb, &sp))
			continue;
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
		vtc_log(v->vl, 0, "stats field %s unknown", av[0]);
	} else if (good) {
		vtc_log(v->vl, 2, "as expected: %s (%ju) %s %s",
		    av[0], sp.val, av[1], av[2]);
	} else {
		vtc_log(v->vl, 0, "Not true: %s (%ju) %s %s (%ju)",
		    av[0], sp.val, av[1], av[2], ref);
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
		if (!strcmp(*av, "-storage")) {
			vsb_clear(v->storage);
			vsb_cat(v->storage, av[1]);
			vsb_finish(v->storage);
			av++;
			continue;
		}
		if (!strcmp(*av, "-arg")) {
			AN(av[1]);
			AZ(v->pid);
			vsb_cat(v->args, " ");
			vsb_cat(v->args, av[1]);
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
		if (!strcmp(*av, "-badvcl")) {
			AN(av[1]);
			varnish_vcl(v, av[1], CLIS_PARAM);
			av++;
			continue;
		}
		if (!strcmp(*av, "-vcl")) {
			AN(av[1]);
			varnish_vcl(v, av[1], CLIS_OK);
			av++;
			continue;
		}
		if (!strcmp(*av, "-stop")) {
			varnish_stop(v);
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
