/*
 * Copyright (c) 2006-2008 Linpro AS
 * All rights reserved.
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
 */


#include <stdio.h>

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>


#include "vqueue.h"
#include "miniobj.h"
#include "libvarnish.h"
#include "cli.h"
#include "cli_common.h"
#include "vss.h"
#include "vsb.h"

#include "vtc.h"

struct varnish {
	unsigned		magic;
#define VARNISH_MAGIC		0x208cd8e3
	char			*name;
	VTAILQ_ENTRY(varnish)	list;

	const char		*args;
	int			fds[4];
	pid_t			pid;
	const char		*telnet;
	const char		*accept;

	pthread_t		tp;

	char			*addr;
	char			*port;
	int			naddr;
	struct vss_addr		**vss_addr;

	int			cli_fd;
	int			vcl_nbr;
};

static VTAILQ_HEAD(, varnish)	varnishes =
    VTAILQ_HEAD_INITIALIZER(varnishes);

/**********************************************************************
 * Ask a question over CLI
 */

static unsigned
varnish_ask_cli(struct varnish *v, const char *cmd, char **repl)
{
	int i;
	unsigned retval;
	char *r;

	vct_dump(v->name, "CLI TX", cmd);
	i = write(v->cli_fd, cmd, strlen(cmd));
	assert(i == strlen(cmd));
	i = write(v->cli_fd, "\n", 1);
	assert(i == 1);
	i = cli_readres(v->cli_fd, &retval, &r, 1000);
	assert(i == 0);
	printf("###  %-4s CLI %u <%s>\n", v->name, retval, cmd);
	vct_dump(v->name, "CLI RX", r);
	if (repl != NULL)
		*repl = r;
	else
		free(r);
	return (retval);
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
varnish_new(char *name)
{
	struct varnish *v;

	if (*name != 'v') {
		fprintf(stderr,
		    "---- %-4s Varnish name must start with 'v'\n", name);
		exit (1);
	}
	ALLOC_OBJ(v, VARNISH_MAGIC);
	AN(v);
	v->name = name;
	v->args = "";
	v->telnet = ":9001";
	v->accept = ":9081";
	VTAILQ_INSERT_TAIL(&varnishes, v, list);
	return (v);
}

/**********************************************************************
 * Varnish listener
 */

static void *
varnish_thread(void *priv)
{
	struct varnish *v;
	char buf[BUFSIZ];
	int i;

	CAST_OBJ_NOTNULL(v, priv, VARNISH_MAGIC);
	while (1) {
		i = read(v->fds[0], buf, sizeof buf - 1);
		if (i <= 0)
			break;
		buf[i] = '\0';
		vct_dump(v->name, "debug", buf);
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
	int i;

	vsb = vsb_newauto();
	AN(vsb);
	vsb_printf(vsb, "cd ../varnishd &&");
	vsb_printf(vsb, " ./varnishd -d -d -n %s", v->name);
	vsb_printf(vsb, " -a %s -T %s", v->accept, v->telnet);
	vsb_printf(vsb, " %s", v->args);
	vsb_finish(vsb);
	AZ(vsb_overflowed(vsb));
	printf("###  %-4s CMD: %s\n", v->name, vsb_data(vsb));
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
		AZ(execl("/bin/sh", "/bin/sh", "-c", vsb_data(vsb), NULL));
		exit(1);
	}
	AZ(close(v->fds[0]));
	AZ(close(v->fds[3]));
	v->fds[0] = v->fds[2];
	v->fds[2] = v->fds[3] = -1;
	vsb_delete(vsb);
	AZ(pthread_create(&v->tp, NULL, varnish_thread, v));

	printf("###  %-4s opening CLI connection\n", v->name);
	for (i = 0; i < 10; i++) {
		usleep(200000);
		v->cli_fd = VSS_open(v->telnet);
		if (v->cli_fd >= 0)
			break;
	}
	if (v->cli_fd < 0) {
		fprintf(stderr, "---- %-4s FAIL no CLI connection\n", v->name);
		kill(v->pid, SIGKILL);
		exit (1);
	}
	printf("###  %-4s CLI connection fd = %d\n", v->name, v->cli_fd);
}

/**********************************************************************
 * Start a Varnish
 */

static void
varnish_start(struct varnish *v)
{

	varnish_launch(v);
	varnish_ask_cli(v, "start", NULL);
}

/**********************************************************************
 * Stop a Varnish
 */

static void
varnish_stop(struct varnish *v)
{
	void *p;

	varnish_ask_cli(v, "stop", NULL);
	AZ(kill(v->pid, SIGKILL));
	AZ(pthread_cancel(v->tp));
	AZ(pthread_join(v->tp, &p));
	close(v->fds[0]);
	close(v->fds[1]);
}

/**********************************************************************
 * Load a VCL program
 */

static void
varnish_vcl(struct varnish *v, char *vcl)
{
	struct vsb *vsb;
	unsigned u;

	vsb = vsb_newauto();
	AN(vsb);

	v->vcl_nbr++;
	vsb_printf(vsb, "vcl.inline vcl%d \"", v->vcl_nbr);
	for (vcl++; vcl[1] != '\0'; vcl++) {
		switch (*vcl) {
		case '\\':
		case '"':
			vsb_printf(vsb, "\\%c", *vcl); break;
		case '\n':
			vsb_printf(vsb, "\\n"); break;
		case '\t':
			vsb_printf(vsb, "\\t"); break;
		default:
			if (isgraph(*vcl) || *vcl == ' ')
				vsb_putc(vsb, *vcl);
			else
				vsb_printf(vsb, "\\x%02x", *vcl);
		}
	}
	vsb_printf(vsb, "\"", *vcl);
	vsb_finish(vsb);
	AZ(vsb_overflowed(vsb));

	u = varnish_ask_cli(v, vsb_data(vsb), NULL);
	assert(u == CLIS_OK);
	vsb_clear(vsb);
	vsb_printf(vsb, "vcl.use vcl%d", v->vcl_nbr);
	vsb_finish(vsb);
	AZ(vsb_overflowed(vsb));
	u = varnish_ask_cli(v, vsb_data(vsb), NULL);
	assert(u == CLIS_OK);
	vsb_delete(vsb);
}

/**********************************************************************
 * Load a VCL program prefixed by backend decls for our servers
 */

static void
varnish_vclbackend(struct varnish *v, char *vcl)
{
	struct vsb *vsb, *vsb2;
	char *p;
	unsigned u;

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

	if (*vcl == '{') {
		p = strchr(++vcl, '\0');
		if (p > vcl && p[-1] == '}')
			p[-1] = '\0';
	}
	varnish_cli_encode(vsb, vcl);

	vsb_printf(vsb, "\"", *vcl);
	vsb_finish(vsb);
	AZ(vsb_overflowed(vsb));

	u = varnish_ask_cli(v, vsb_data(vsb), NULL);
	assert(u == CLIS_OK);
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
 * Varnish server cmd dispatch
 */

void
cmd_varnish(char **av, void *priv)
{
	struct varnish *v, *v2;

	(void)priv;

	if (av == NULL) {
		/* Reset and free */
		VTAILQ_FOREACH_SAFE(v, &varnishes, list, v2) {
			VTAILQ_REMOVE(&varnishes, v, list);
			FREE_OBJ(v);
			/* XXX: MEMLEAK */
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
		if (!strcmp(*av, "-telnet")) {
			v->telnet = av[1];
			av++;
			continue;
		}
		if (!strcmp(*av, "-accept")) {
			v->accept = av[1];
			av++;
			continue;
		}
		if (!strcmp(*av, "-arg")) {
			v->args = av[1];
			av++;
			continue;
		}
		if (!strcmp(*av, "-launch")) {
			varnish_launch(v);
			continue;
		}
		if (!strcmp(*av, "-start")) {
			varnish_start(v);
			continue;
		}
		if (!strcmp(*av, "-vcl+backend")) {
			varnish_vclbackend(v, av[1]);
			av++;
			continue;
		}
		if (!strcmp(*av, "-vcl")) {
			varnish_vcl(v, av[1]);
			av++;
			continue;
		}
		if (!strcmp(*av, "-stop")) {
			varnish_stop(v);
			continue;
		}
		fprintf(stderr, "Unknown varnish argument: %s\n", *av);
		exit (1);
	}
}
