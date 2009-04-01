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
 * The management process and CLI handling
 */

#include "config.h"

#include <sys/stat.h>

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
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#ifndef HAVE_DAEMON
#include "compat/daemon.h"
#endif

#ifndef HAVE_STRLCPY
#include "compat/strlcpy.h"
#endif

#include "vsb.h"
#include "vpf.h"
#include "vsha256.h"

#include "cli.h"
#include "cli_priv.h"
#include "cli_common.h"

#include "shmlog.h"
#include "heritage.h"
#include "mgt.h"
#include "hash_slinger.h"
#include "stevedore.h"

/* INFTIM indicates an infinite timeout for poll(2) */
#ifndef INFTIM
#define INFTIM -1
#endif

struct heritage heritage;
volatile struct params *params;

/*--------------------------------------------------------------------*/

struct choice {
	const char	*name;
	void		*ptr;
};

static void *
pick(const struct choice *cp, const char *which, const char *kind)
{

	for(; cp->name != NULL; cp++) {
		if (!strcmp(cp->name, which))
			return (cp->ptr);
	}
	ARGV_ERR("Unknown %s method \"%s\"\n", kind, which);
}

/*--------------------------------------------------------------------*/

static unsigned long
arg_ul(const char *p)
{
	char *q;
	unsigned long ul;

	ul = strtoul(p, &q, 0);
	if (*q != '\0')
		ARGV_ERR("Invalid number: \"%s\"\n", p);
	return (ul);
}

/*--------------------------------------------------------------------*/
extern struct stevedore sma_stevedore;
extern struct stevedore smf_stevedore;
#ifdef HAVE_LIBUMEM
extern struct stevedore smu_stevedore;
#endif

static const struct choice stv_choice[] = {
	{ "file",	&smf_stevedore },
	{ "malloc",	&sma_stevedore },
#ifdef HAVE_LIBUMEM
	{ "umem",	&smu_stevedore },
#endif
	{ NULL,		NULL }
};

static void
setup_storage(const char *spec)
{
	char **av;
	void *priv;
	int ac;

	av = ParseArgv(spec, ARGV_COMMA);
	AN(av);

	if (av[0] != NULL)
		ARGV_ERR("%s\n", av[0]);

	if (av[1] == NULL)
		ARGV_ERR("-s argument is empty\n");

	for (ac = 0; av[ac + 2] != NULL; ac++)
		continue;

	priv = pick(stv_choice, av[1], "storage");
	AN(priv);

	STV_add(priv, ac, av + 2);

	/* We do not free av, to make life simpler for stevedores */
}

/*--------------------------------------------------------------------*/

extern struct hash_slinger hsl_slinger;
extern struct hash_slinger hcl_slinger;
extern struct hash_slinger hcb_slinger;

static const struct choice hsh_choice[] = {
	{ "classic",		&hcl_slinger },
	{ "simple",		&hsl_slinger },
	{ "simple_list",	&hsl_slinger },	/* backwards compat */
	{ "critbit",		&hcb_slinger },
	{ NULL,			NULL }
};

static void
setup_hash(const char *h_arg)
{
	char **av;
	int ac;
	struct hash_slinger *hp;

	av = ParseArgv(h_arg, ARGV_COMMA);
	AN(av);

	if (av[0] != NULL)
		ARGV_ERR("%s\n", av[0]);

	if (av[1] == NULL)
		ARGV_ERR("-h argument is empty\n");

	for (ac = 0; av[ac + 2] != NULL; ac++)
		continue;

	hp = pick(hsh_choice, av[1], "hash");
	CHECK_OBJ_NOTNULL(hp, SLINGER_MAGIC);
	heritage.hash = hp;
	if (hp->init != NULL)
		hp->init(ac, av + 2);
	else if (ac > 0)
		ARGV_ERR("Hash method \"%s\" takes no arguments\n",
		    hp->name);
}

/*--------------------------------------------------------------------*/

static void
usage(void)
{
#define FMT "    %-28s # %s\n"

	fprintf(stderr, "usage: varnishd [options]\n");
	fprintf(stderr, FMT, "-a address:port", "HTTP listen address and port");
	fprintf(stderr, FMT, "-b address:port", "backend address and port");
	fprintf(stderr, FMT, "", "   -b <hostname_or_IP>");
	fprintf(stderr, FMT, "", "   -b '<hostname_or_IP>:<port_or_service>'");
	fprintf(stderr, FMT, "-d", "debug");
	fprintf(stderr, FMT, "-f file", "VCL script");
	fprintf(stderr, FMT, "-F", "Run in foreground");
	fprintf(stderr, FMT, "-h kind[,hashoptions]", "Hash specification");
	fprintf(stderr, FMT, "", "  -h simple_list");
	fprintf(stderr, FMT, "", "  -h classic  [default]");
	fprintf(stderr, FMT, "", "  -h classic,<buckets>");
	fprintf(stderr, FMT, "-i identity", "Identity of varnish instance");
	fprintf(stderr, FMT, "-l bytesize", "Size of shared memory log");
	fprintf(stderr, FMT, "-n dir", "varnishd working directory");
	fprintf(stderr, FMT, "-P file", "PID file");
	fprintf(stderr, FMT, "-p param=value", "set parameter");
	fprintf(stderr, FMT,
	    "-s kind[,storageoptions]", "Backend storage specification");
	fprintf(stderr, FMT, "", "  -s malloc");
#ifdef HAVE_LIBUMEM
	fprintf(stderr, FMT, "", "  -s umem");
#endif
	fprintf(stderr, FMT, "", "  -s file  [default: use /tmp]");
	fprintf(stderr, FMT, "", "  -s file,<dir_or_file>");
	fprintf(stderr, FMT, "", "  -s file,<dir_or_file>,<size>");
	fprintf(stderr, FMT, "",
	    "  -s file,<dir_or_file>,<size>,<granularity>");
	fprintf(stderr, FMT, "-t", "Default TTL");
	fprintf(stderr, FMT, "-T address:port",
	    "Telnet listen address and port");
	fprintf(stderr, FMT, "-V", "version");
	fprintf(stderr, FMT, "-w int[,int[,int]]", "Number of worker threads");
	fprintf(stderr, FMT, "", "  -w <fixed_count>");
	fprintf(stderr, FMT, "", "  -w min,max");
	fprintf(stderr, FMT, "", "  -w min,max,timeout [default: -w2,500,300]");
	fprintf(stderr, FMT, "-u user", "Priviledge separation user id");
#undef FMT
	exit(1);
}


/*--------------------------------------------------------------------*/

static void
tackle_warg(const char *argv)
{
	char **av;
	unsigned int u;

	av = ParseArgv(argv, ARGV_COMMA);
	AN(av);

	if (av[0] != NULL)
		ARGV_ERR("%s\n", av[0]);

	if (av[1] == NULL)
		usage();

	u = arg_ul(av[1]);
	if (u < 1)
		usage();
	params->wthread_max = params->wthread_min = u;

	if (av[2] != NULL) {
		u = arg_ul(av[2]);
		if (u < params->wthread_min)
			usage();
		params->wthread_max = u;

		if (av[3] != NULL) {
			u = arg_ul(av[3]);
			params->wthread_timeout = u;
		}
	}
	FreeArgv(av);
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

	(void)kill(d_child, sig);
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
		(void)close(i);

	pfd[0].fd = pipes[0][0];
	pfd[0].events = POLLIN;
	pfd[1].fd = pipes[1][0];
	pfd[1].events = POLLIN;

	(void)signal(SIGPIPE, SIG_IGN);
	(void)signal(SIGINT, DebugSigPass);
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
cli_check(const struct cli *cli)
{
	if (cli->result == CLIS_OK) {
		vsb_clear(cli->sb);
		return;
	}
	vsb_finish(cli->sb);
	AZ(vsb_overflowed(cli->sb));
	fprintf(stderr, "Error:\n%s\n", vsb_data(cli->sb));
	exit (2);
}

/*--------------------------------------------------------------------*/

int
main(int argc, char * const *argv)
{
	int o;
	unsigned C_flag = 0;
	unsigned d_flag = 0;
	unsigned F_flag = 0;
	const char *b_arg = NULL;
	const char *f_arg = NULL;
	const char *i_arg = NULL;
	const char *l_arg = "80m";
	uintmax_t l_size;
	const char *q;
	const char *h_arg = "classic";
	const char *n_arg = NULL;
	const char *P_arg = NULL;
	const char *s_arg = "file";
	int s_arg_given = 0;
	const char *T_arg = NULL;
	char *p, *vcl = NULL;
	struct cli cli[1];
	struct pidfh *pfh = NULL;
	char dirname[1024];

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	/*
	 * Run in UTC timezone, on the off-chance that this operating
	 * system does not have a timegm() function, and translates
	 * timestamps on the local timescale.
	 * See lib/libvarnish/time.c
	 */
	AZ(setenv("TZ", "UTC", 1));
	tzset();
	assert(TIM_parse("Sun, 06 Nov 1994 08:49:37 GMT") == 784111777);
	assert(TIM_parse("Sunday, 06-Nov-94 08:49:37 GMT") == 784111777);
	assert(TIM_parse("Sun Nov  6 08:49:37 1994") == 784111777);

	/*
	 * Check that our SHA256 works
	 */
	SHA256_Test();

	memset(cli, 0, sizeof cli);
	cli[0].sb = vsb_newauto();
	XXXAN(cli[0].sb);
	cli[0].result = CLIS_OK;

	VTAILQ_INIT(&heritage.socks);

	mgt_vcc_init();

	MCF_ParamInit(cli);
	cli_check(cli);

	while ((o = getopt(argc, argv,
	    "a:b:Cdf:Fg:h:i:l:n:P:p:s:T:t:u:Vw:")) != -1)
		switch (o) {
		case 'a':
			MCF_ParamSet(cli, "listen_address", optarg);
			cli_check(cli);
			break;
		case 'b':
			b_arg = optarg;
			break;
		case 'C':
			C_flag = 1 - C_flag;
			break;
		case 'd':
			d_flag++;
			break;
		case 'f':
			f_arg = optarg;
			break;
		case 'F':
			F_flag = 1 - F_flag;
			break;
		case 'g':
			MCF_ParamSet(cli, "group", optarg);
			break;
		case 'h':
			h_arg = optarg;
			break;
		case 'i':
			i_arg = optarg;
			break;
		case 'l':
			l_arg = optarg;
			break;
		case 'n':
			n_arg = optarg;
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
			s_arg_given = 1;
			setup_storage(optarg);
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
		fprintf(stderr, "Too many arguments (%s...)\n", argv[0]);
		usage();
	}

	q = str2bytes(l_arg, &l_size, 0);
	if (q != NULL) {
		fprintf(stderr, "Parameter error:\n");
		fprintf(stderr, "\t-l ...:  %s\n", q);
		exit (1);
	}

	/* XXX: we can have multiple CLI actions above, is this enough ? */
	if (cli[0].result != CLIS_OK) {
		fprintf(stderr, "Parameter errors:\n");
		vsb_finish(cli[0].sb);
		AZ(vsb_overflowed(cli[0].sb));
		fprintf(stderr, "%s\n", vsb_data(cli[0].sb));
		exit(1);
	}

	if (d_flag && F_flag) {
		fprintf(stderr, "Only one of -d or -F can be specified\n");
		usage();
	}

	if (b_arg != NULL && f_arg != NULL) {
		fprintf(stderr, "Only one of -b or -f can be specified\n");
		usage();
	}

	if (f_arg != NULL) {
		vcl = vreadfile(f_arg);
		if (vcl == NULL) {
			fprintf(stderr, "Cannot read '%s': %s\n",
			    f_arg, strerror(errno));
			exit(1);
		}
	}

	if (varnish_instance(n_arg, heritage.name, sizeof heritage.name,
	    dirname, sizeof dirname) != 0) {
		fprintf(stderr, "Invalid instance name: %s\n",
		    strerror(errno));
		exit(1);
	}


	if (i_arg != NULL) {
		if (snprintf(heritage.identity, sizeof heritage.identity,
		    "%s", i_arg) > sizeof heritage.identity) {
			fprintf(stderr, "Invalid identity name: %s\n",
			    strerror(ENAMETOOLONG));
			exit(1);
		}
	}
	

	if (n_arg != NULL)
		openlog(n_arg, LOG_PID, LOG_LOCAL0);
	else
		openlog("varnishd", LOG_PID, LOG_LOCAL0);

	if (mkdir(dirname, 0755) < 0 && errno != EEXIST) {
		fprintf(stderr, "Cannot create working directory '%s': %s\n",
		    dirname, strerror(errno));
		exit(1);
	}

	if (chdir(dirname) < 0) {
		fprintf(stderr, "Cannot change to working directory '%s': %s\n",
		    dirname, strerror(errno));
		exit(1);
	}

	/* XXX: should this be relative to the -n arg ? */
	if (P_arg && (pfh = vpf_open(P_arg, 0644, NULL)) == NULL) {
		perror(P_arg);
		exit(1);
	}

	if (b_arg != NULL || f_arg != NULL)
		if (mgt_vcc_default(b_arg, vcl, C_flag))
			exit (2);

	if (C_flag)
		exit (0);

	if (!s_arg_given)
		setup_storage(s_arg);

	setup_hash(h_arg);

	VSL_MgtInit(SHMLOG_FILENAME, l_size);

	if (d_flag == 1)
		DebugStunt();
	if (d_flag < 2 && !F_flag)
		AZ(inxorcise(1, d_flag));
	if (d_flag == 1)
		printf("%d\n", getpid());

	if (pfh != NULL && vpf_write(pfh))
		fprintf(stderr, "NOTE: Could not write PID file\n");

	mgt_run(d_flag, T_arg);

	if (pfh != NULL)
		(void)vpf_remove(pfh);
	exit(0);
}
