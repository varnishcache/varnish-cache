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
 * The management process and CLI handling
 */

#include <err.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef HAVE_DAEMON
#include "compat/daemon.h"
#endif

#include "vsb.h"
#include "vpf.h"

#include "cli.h"
#include "cli_priv.h"
#include "cli_common.h"

#include "mgt.h"
#include "heritage.h"
#include "shmlog.h"

/* INFTIM indicates an infinite timeout for poll(2) */
#ifndef INFTIM
#define INFTIM -1
#endif

struct heritage heritage;
volatile struct params *params;

/*--------------------------------------------------------------------*/

static int
cmp_hash(struct hash_slinger *s, const char *p, const char *q)
{
	if (strlen(s->name) != q - p)
		return (1);
	if (strncmp(s->name, p, q - p))
		return (1);
	return (0);
}

static void
setup_hash(const char *s_arg)
{
	const char *p, *q;
	struct hash_slinger *hp;

	p = strchr(s_arg, ',');
	if (p == NULL)
		q = p = strchr(s_arg, '\0');
	else
		q = p + 1;
	xxxassert(p != NULL);
	xxxassert(q != NULL);
	if (!cmp_hash(&hcl_slinger, s_arg, p)) {
		hp = &hcl_slinger;
	} else if (!cmp_hash(&hsl_slinger, s_arg, p)) {
		hp = &hsl_slinger;
	} else {
		fprintf(stderr, "Unknown hash method \"%.*s\"\n",
		    (int)(p - s_arg), s_arg);
		exit (2);
	}
	heritage.hash = hp;
	if (hp->init != NULL) {
		if (hp->init(q))
			exit (1);
	} else if (*q) {
		fprintf(stderr, "Hash method \"%s\" takes no arguments\n",
		    hp->name);
		exit (1);
	}
}

/*--------------------------------------------------------------------*/

static int
cmp_storage(struct stevedore *s, const char *p, const char *q)
{
	if (strlen(s->name) != q - p)
		return (1);
	if (strncmp(s->name, p, q - p))
		return (1);
	return (0);
}

static void
setup_storage(const char *s_arg)
{
	const char *p, *q;
	struct stevedore *stp;

	p = strchr(s_arg, ',');
	if (p == NULL)
		q = p = strchr(s_arg, '\0');
	else
		q = p + 1;
	xxxassert(p != NULL);
	xxxassert(q != NULL);
	if (!cmp_storage(&sma_stevedore, s_arg, p)) {
		stp = &sma_stevedore;
	} else if (!cmp_storage(&smf_stevedore, s_arg, p)) {
		stp = &smf_stevedore;
	} else {
		fprintf(stderr, "Unknown storage method \"%.*s\"\n",
		    (int)(p - s_arg), s_arg);
		exit (2);
	}
	heritage.stevedore = malloc(sizeof *heritage.stevedore);
	*heritage.stevedore = *stp;
	if (stp->init != NULL)
		stp->init(heritage.stevedore, q, params->name);
}

/*--------------------------------------------------------------------*/

static void
usage(void)
{
	fprintf(stderr, "usage: varnishd [options]\n");
	fprintf(stderr, "    %-28s # %s\n", "-a address:port",
	    "HTTP listen address and port");
	fprintf(stderr, "    %-28s # %s\n", "-b address:port",
	    "backend address and port");
	fprintf(stderr, "    %-28s # %s\n", "",
	    "   -b <hostname_or_IP>");
	fprintf(stderr, "    %-28s # %s\n", "",
	    "   -b '<hostname_or_IP>:<port_or_service>'");
	fprintf(stderr, "    %-28s # %s\n", "-d", "debug");
	fprintf(stderr, "    %-28s # %s\n", "-f file", "VCL script");
	fprintf(stderr, "    %-28s # %s\n",
	    "-h kind[,hashoptions]", "Hash specification");
	fprintf(stderr, "    %-28s # %s\n", "",
	    "  -h simple_list");
	fprintf(stderr, "    %-28s # %s\n", "",
	    "  -h classic  [default]");
	fprintf(stderr, "    %-28s # %s\n", "",
	    "  -h classic,<buckets>");
	fprintf(stderr, "    %-28s # %s\n", "-n name", "varnishd instance name");
	fprintf(stderr, "    %-28s # %s\n", "-P file", "PID file");
	fprintf(stderr, "    %-28s # %s\n", "-p param=value",
	    "set parameter");
	fprintf(stderr, "    %-28s # %s\n",
	    "-s kind[,storageoptions]", "Backend storage specification");
	fprintf(stderr, "    %-28s # %s\n", "",
	    "  -s malloc");
	fprintf(stderr, "    %-28s # %s\n", "",
	    "  -s file  [default: use /tmp]");
	fprintf(stderr, "    %-28s # %s\n", "",
	    "  -s file,<dir_or_file>");
	fprintf(stderr, "    %-28s # %s\n", "",
	    "  -s file,<dir_or_file>,<size>");
	fprintf(stderr, "    %-28s # %s\n", "-t", "Default TTL");
	fprintf(stderr, "    %-28s # %s\n", "-T address:port",
	    "Telnet listen address and port");
	fprintf(stderr, "    %-28s # %s\n", "-V", "version");
	fprintf(stderr, "    %-28s # %s\n", "-w int[,int[,int]]",
	    "Number of worker threads");
	fprintf(stderr, "    %-28s # %s\n", "",
	    "  -w <fixed_count>");
	fprintf(stderr, "    %-28s # %s\n", "",
	    "  -w min,max");
	fprintf(stderr, "    %-28s # %s\n", "",
	    "  -w min,max,timeout [default: -w1,1000,120]");
#if 0
	-c clusterid@cluster_controller
	-m memory_limit
	-l logfile,logsize
	-u uid
	-a CLI_port
#endif
	exit(1);
}


/*--------------------------------------------------------------------*/

static void
tackle_warg(const char *argv)
{
	unsigned int u;
	char *ep, *eq;

	u = strtoul(argv, &ep, 0);
	if (ep == argv)
		usage();
	while (isspace(*ep))
		ep++;
	if (u < 1)
		usage();
	params->wthread_min = u;

	if (*ep == '\0') {
		params->wthread_max = params->wthread_min;
		return;
	}

	if (*ep != ',')
		usage();
	u = strtoul(++ep, &eq, 0);
	if (eq == ep)
		usage();
	if (u < params->wthread_min)
		usage();
	while (isspace(*eq))
		eq++;
	params->wthread_max = u;

	if (*eq == '\0')
		return;

	if (*eq != ',')
		usage();
	u = strtoul(++eq, &ep, 0);
	if (ep == eq)
		usage();
	while (isspace(*ep))
		ep++;
	if (*ep != '\0')
		usage();
	params->wthread_timeout = u;
}

/*--------------------------------------------------------------------
 * When -d is specified we fork a third process which will relay
 * keystrokes between the terminal and the CLI.  This allows us to
 * detach from the process and have it daemonize properly (ie: it already
 * did that long time ago).
 * Doing the simple thing and calling daemon(3) when the user asks for
 * it does not work, daemon(3) forks and all the threads are lost.
 */

static pid_t d_child;


static void
DebugSigPass(int sig)
{

	kill(d_child, sig);
}

static void
DebugStunt(void)
{
	int pipes[2][2];
	struct pollfd pfd[2];
	char buf[BUFSIZ];
	int i, j, k;
	char *p;

	AZ(pipe(pipes[0]));
	AZ(pipe(pipes[1]));
	d_child = fork();
	xxxassert(d_child >= 0);
	if (!d_child) {
		/* stdin from parent, std{out,err} to parent */
		assert(dup2(pipes[0][0], 0) == 0);
		assert(dup2(pipes[1][1], 1) == 1);
		assert(dup2(pipes[1][1], 2) == 2);
		AZ(close(pipes[0][0]));
		AZ(close(pipes[0][1]));
		AZ(close(pipes[1][0]));
		AZ(close(pipes[1][1]));
		return;
	}

	/* set up parent's end of pipe to child's stdin */
	AZ(close(pipes[0][0]));
	pipes[0][0] = 0;
	assert(dup2(pipes[0][1], 3) == 3);
	pipes[0][1] = 3;

	/* set up parent's end of pipe from child's std{out,err} */
	assert(dup2(pipes[1][0], 4) == 4);
	pipes[1][0] = 4;
	AZ(close(pipes[1][1]));
	pipes[1][1] = 1;

	/* close the rest */
	for (i = 5; i < getdtablesize(); i++)
		close(i);

	pfd[0].fd = pipes[0][0];
	pfd[0].events = POLLIN;
	pfd[1].fd = pipes[1][0];
	pfd[1].events = POLLIN;

	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, DebugSigPass);
	i = read(pipes[1][0], buf, sizeof buf - 1);
	xxxassert(i >= 0);
	buf[i] = '\0';
	d_child = strtoul(buf, &p, 0);
	xxxassert(p != NULL);
	printf("New Pid %d\n", d_child);
	xxxassert(d_child != 0);
	i = strlen(p);
	j = write(pipes[1][1], p, i);
	xxxassert(j == i);

	while (1) {
		if (pfd[0].fd == -1 && pfd[1].fd == -1)
			break;
		i = poll(pfd, 2, INFTIM);
		for (k = 0; k < 2; k++) {
			if (pfd[k].fd == -1)
				continue;
			if (pfd[k].revents == 0)
				continue;
			if (pfd[k].revents != POLLIN) {
				printf("k %d rev %d\n", k, pfd[k].revents);
				AZ(close(pipes[k][0]));
				AZ(close(pipes[k][1]));
				pfd[k].fd = -1;
				if (k == 1)
					exit (0);
			}
			j = read(pipes[k][0], buf, sizeof buf);
			if (j == 0) {
				printf("k %d eof\n", k);
				AZ(close(pipes[k][0]));
				AZ(close(pipes[k][1]));
				pfd[k].fd = -1;
			}
			if (j > 0) {
				i = write(pipes[k][1], buf, j);
				if (i != j) {
					printf("k %d write (%d %d)\n", k, i, j);
					AZ(close(pipes[k][0]));
					AZ(close(pipes[k][1]));
					pfd[k].fd = -1;
				}
			}
		}
	}
	exit (0);
}


/*--------------------------------------------------------------------*/

static void
cli_check(struct cli *cli)
{
	if (cli->result == CLIS_OK) {
		vsb_clear(cli->sb);
		return;
	}
	vsb_finish(cli->sb);
	fprintf(stderr, "Error:\n%s\n", vsb_data(cli->sb));
	exit (2);
}

/*--------------------------------------------------------------------*/

int
main(int argc, char *argv[])
{
	int o;
	unsigned d_flag = 0;
	const char *b_arg = NULL;
	const char *f_arg = NULL;
	const char *h_arg = "classic";
	const char *P_arg = NULL;
	const char *s_arg = "file";
	const char *T_arg = NULL;
	unsigned C_flag = 0;
	char *p;
	struct cli cli[1];
	struct pidfh *pfh = NULL;
	char buf[BUFSIZ];

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	memset(cli, 0, sizeof cli);
	cli[0].sb = vsb_new(NULL, NULL, 0, VSB_AUTOEXTEND);
	XXXAN(cli[0].sb);
	cli[0].result = CLIS_OK;

	TAILQ_INIT(&heritage.socks);
	mgt_vcc_init();

	MCF_ParamInit(cli);
	cli_check(cli);

	while ((o = getopt(argc, argv, "a:b:Cdf:g:h:n:P:p:s:T:t:u:Vw:")) != -1)
		switch (o) {
		case 'a':
			MCF_ParamSet(cli, "listen_address", optarg);
			cli_check(cli);
			break;
		case 'b':
			b_arg = optarg;
			break;
		case 'C':
			C_flag = 1;
			break;
		case 'd':
			d_flag++;
			break;
		case 'f':
			f_arg = optarg;
			break;
		case 'g':
			MCF_ParamSet(cli, "group", optarg);
			break;
		case 'h':
			h_arg = optarg;
			break;
		case 'n':
			MCF_ParamSet(cli, "name", optarg);
			break;
		case 'P':
			P_arg = optarg;
			break;
		case 'p':
			p = strchr(optarg, '=');
			if (p == NULL)
				usage();
			AN(p);
			*p++ = '\0';
			MCF_ParamSet(cli, optarg, p);
			cli_check(cli);
			break;
		case 's':
			s_arg = optarg;
			break;
		case 't':
			MCF_ParamSet(cli, "default_ttl", optarg);
			break;
		case 'T':
			T_arg = optarg;
			break;
		case 'u':
			MCF_ParamSet(cli, "user", optarg);
			break;
		case 'V':
			varnish_version("varnishd");
			exit(0);
		case 'w':
			tackle_warg(optarg);
			break;
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	if (argc != 0) {
		fprintf(stderr, "Too many arguments\n");
		usage();
	}

	if (cli[0].result != CLIS_OK) {
		fprintf(stderr, "Parameter errors:\n");
		vsb_finish(cli[0].sb);
		fprintf(stderr, "%s\n", vsb_data(cli[0].sb));
		exit(1);
	}

	if (b_arg != NULL && f_arg != NULL) {
		fprintf(stderr, "Only one of -b or -f can be specified\n");
		usage();
	}
	if (b_arg == NULL && f_arg == NULL) {
		fprintf(stderr, "One of -b or -f must be specified\n");
		usage();
	}

	if (P_arg && (pfh = vpf_open(P_arg, 0600, NULL)) == NULL) {
		perror(P_arg);
		exit(1);
	}

	if (mgt_vcc_default(b_arg, f_arg, C_flag))
		exit (2);
	if (C_flag)
		exit (0);

	setup_storage(s_arg);
	setup_hash(h_arg);

	sprintf(buf, "/tmp/%s/%s", params->name, SHMLOG_FILENAME);
	VSL_MgtInit(buf, 8*1024*1024);

	if (d_flag == 1)
		DebugStunt();
	if (d_flag < 2)
		daemon(d_flag, d_flag);
	if (d_flag == 1)
		printf("%d\n", getpid());

	if (pfh != NULL)
		vpf_write(pfh);

	mgt_cli_init();

	mgt_run(d_flag, T_arg);

	if (pfh != NULL)
		vpf_remove(pfh);
	exit(0);
}
