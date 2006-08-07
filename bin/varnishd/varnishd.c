/*
 * $Id$
 *
 * The management process and CLI handling
 */

#include <assert.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "vsb.h"

#include "libvarnish.h"
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
setup_hash(const char *sflag)
{
	const char *p, *q;
	struct hash_slinger *hp;

	p = strchr(sflag, ',');
	if (p == NULL)
		q = p = strchr(sflag, '\0');
	else
		q = p + 1;
	assert(p != NULL);
	assert(q != NULL);
	if (!cmp_hash(&hcl_slinger, sflag, p)) {
		hp = &hcl_slinger;
	} else if (!cmp_hash(&hsl_slinger, sflag, p)) {
		hp = &hsl_slinger;
	} else {
		fprintf(stderr, "Unknown hash method \"%*.*s\"\n",
			p - sflag, p - sflag, sflag);
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
setup_storage(const char *sflag)
{
	const char *p, *q;
	struct stevedore *stp;

	p = strchr(sflag, ',');
	if (p == NULL)
		q = p = strchr(sflag, '\0');
	else
		q = p + 1;
	assert(p != NULL);
	assert(q != NULL);
	if (!cmp_storage(&sma_stevedore, sflag, p)) {
		stp = &sma_stevedore;
	} else if (!cmp_storage(&smf_stevedore, sflag, p)) {
		stp = &smf_stevedore;
	} else {
		fprintf(stderr, "Unknown storage method \"%*.*s\"\n",
			p - sflag, p - sflag, sflag);
		exit (2);
	}
	heritage.stevedore = malloc(sizeof *heritage.stevedore);
	*heritage.stevedore = *stp;
	if (stp->init != NULL)
		stp->init(heritage.stevedore, q);
}

/*--------------------------------------------------------------------*/

static void
usage(void)
{
	fprintf(stderr, "usage: varnishd [options]\n");
	fprintf(stderr, "    %-28s # %s\n", "-b backend",
	    "backend location");
	fprintf(stderr, "    %-28s # %s\n", "",
	    "   -b <hostname_or_IP>");
	fprintf(stderr, "    %-28s # %s\n", "",
	    "   -b '<hostname_or_IP> <port_or_service>'");
	fprintf(stderr, "    %-28s # %s\n", "-d", "debug");
	fprintf(stderr, "    %-28s # %s\n", "-f file", "VCL_file");
	fprintf(stderr, "    %-28s # %s\n",
	    "-h kind[,hashoptions]", "Hash specification");
	fprintf(stderr, "    %-28s # %s\n", "",
	    "  -h simple_list");
	fprintf(stderr, "    %-28s # %s\n", "",
	    "  -h classic  [default]");
	fprintf(stderr, "    %-28s # %s\n", "",
	    "  -h classic,<buckets>");
	fprintf(stderr, "    %-28s # %s\n", "",
	    "  -h classic,<buckets>,<buckets_per_mutex>");
	fprintf(stderr, "    %-28s # %s\n", "-p number", "TCP listen port");
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
	fprintf(stderr, "    %-28s # %s\n", "-V", "version");
	fprintf(stderr, "    %-28s # %s\n", "-w int[,int[,int]]",
	    "Number of worker threads");
	fprintf(stderr, "    %-28s # %s\n", "",
	    "  -w <fixed_count>");
	fprintf(stderr, "    %-28s # %s\n", "",
	    "  -w min,max");
	fprintf(stderr, "    %-28s # %s\n", "",
	    "  -w min,max,timeout [default: -w1,INF,10]");
#if 0
	-c clusterid@cluster_controller
	-m memory_limit
	-s kind[,storage-options]
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
	int i;
	unsigned ua, ub, uc;

	i = sscanf(argv, "%u,%u,%u", &ua, &ub, &uc);
	if (i == 0)
		usage();
	if (ua < 1)
		usage();
	heritage.wthread_min = ua;
	heritage.wthread_max = ua;
	heritage.wthread_timeout = 10;
	if (i >= 2)
		heritage.wthread_max = ub;
	if (i >= 3)
		heritage.wthread_timeout = uc;
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

#include <err.h>

static void
DebugSigPass(int sig)
{
	int i;

	i = kill(d_child, sig);
	printf("sig %d i %d pid %d\n", sig, i, d_child);
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
	if (!d_child) {
		assert(dup2(pipes[0][0], 0) >= 0);
		assert(dup2(pipes[1][1], 1) >= 0);
		assert(dup2(pipes[1][1], 2) >= 0);
		AZ(close(pipes[0][0]));
		AZ(close(pipes[0][1]));
		AZ(close(pipes[1][0]));
		AZ(close(pipes[1][1]));
		return;
	}
	AZ(close(pipes[0][0]));
	assert(dup2(pipes[0][1], 3) >= 0);
	pipes[0][0] = 0;
	pipes[0][1] = 3;

	assert(dup2(pipes[1][0], 4) >= 0);
	AZ(close(pipes[1][1]));
	pipes[1][0] = 4;
	pipes[1][1] = 1;

	for (i = 5; i < 100; i++)
		close(i);

	pfd[0].fd = pipes[0][0];
	pfd[0].events = POLLIN;
	pfd[1].fd = pipes[1][0];
	pfd[1].events = POLLIN;

	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, DebugSigPass);
	i = read(pipes[1][0], buf, sizeof buf - 1);
	buf[i] = '\0';
	d_child = strtoul(buf, &p, 0);
	assert(p != NULL);
	printf("New Pid %d\n", d_child);
	i = strlen(p);
	j = write(pipes[1][1], p, i);
	assert(j == i);

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


/* for development purposes */
#include <printf.h>

int
main(int argc, char *argv[])
{
	int o;
	const char *portnumber = "8080";
	unsigned dflag = 0;
	const char *bflag = NULL;
	const char *fflag = NULL;
	const char *sflag = "file";
	const char *hflag = "classic";

	(void)register_printf_render_std((const unsigned char *)"HVQ");

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	mgt_vcc_init(); 

	heritage.default_ttl = 120;
	heritage.wthread_min = 1;
	heritage.wthread_max = UINT_MAX;
	heritage.wthread_timeout = 10;
	heritage.mem_workspace = 4096;

	while ((o = getopt(argc, argv, "b:df:h:p:s:t:Vw:")) != -1)
		switch (o) {
		case 'b':
			bflag = optarg;
			break;
		case 'd':
			dflag++;
			break;
		case 'f':
			fflag = optarg;
			break;
		case 'h':
			hflag = optarg;
			break;
		case 'p':
			portnumber = optarg;
			break;
		case 's':
			sflag = optarg;
			break;
		case 't':
			heritage.default_ttl = strtoul(optarg, NULL, 0);
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

	if (bflag != NULL && fflag != NULL) {
		fprintf(stderr, "Only one of -b or -f can be specified\n");
		usage();
	}
	if (bflag == NULL && fflag == NULL) {
		fprintf(stderr, "One of -b or -f must be specified\n");
		usage();
	}

	if (mgt_vcc_default(bflag, fflag))
		exit (2);

	setup_storage(sflag);
	setup_hash(hflag);

	/*
	 * XXX: Lacking the suspend/resume facility (due to the socket API
	 * missing an unlisten(2) facility) we may want to push this into
	 * the child to limit the amount of time where the socket(s) exists
	 * but do not answer.  That, on the other hand, would eliminate the
	 * possibility of doing a "no-glitch" restart of the child process.
	 */
	open_tcp(portnumber);

	VSL_MgtInit(SHMLOG_FILENAME, 8*1024*1024);

	if (dflag == 1)
		DebugStunt();
	if (dflag != 2)
		daemon(dflag, dflag);
	if (dflag)
		printf("%d\n%d\n%d\n", getpid(), getsid(0), getpgrp());

	mgt_cli_init();

	mgt_run(dflag);

	exit(0);
}
