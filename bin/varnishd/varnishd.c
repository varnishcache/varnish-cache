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

#include "sbuf.h"

#include "libvarnish.h"
#include "libvcl.h"
#include "cli.h"
#include "cli_priv.h"
#include "common_cli.h"

#include "mgt.h"
#include "heritage.h"
#include "shmlog.h"

/*--------------------------------------------------------------------*/

static const char *default_vcl =
    "sub default_vcl_recv {\n"
    "    if (req.request != \"GET\" && req.request != \"HEAD\") {\n"
    "        pipe;\n"
    "    }\n"
    "    if (req.http.Expect) {\n"
    "        pipe;\n"
    "    }\n"
    "    if (req.http.Authenticate || req.http.Cookie) {\n"
    "        pass;\n"
    "    }\n"
    "    lookup;\n"
    "}\n"
    "\n"
    "sub default_vcl_hit {\n"
    "    if (!obj.cacheable) {\n"
    "        pass;\n"
    "    }\n"
    "    deliver;\n"
    "}\n"
    "\n"
    "sub default_vcl_miss {\n"
    "    fetch;\n"
    "}\n"
    "\n"
    "sub default_vcl_fetch {\n"
    "    if (!obj.valid) {\n"
    "        error;\n"
    "    }\n"
    "    if (!obj.cacheable) {\n"
    "        insert_pass;\n"
    "    }\n"
    "    insert;\n"
    "}\n"
    "sub default_vcl_timeout {\n"
    "    discard;\n"
    "}\n";

/*--------------------------------------------------------------------*/

struct heritage heritage;

#if 0
/*--------------------------------------------------------------------
 * Generic passthrough for CLI functions
 */

static void
cli_passthrough_cb(unsigned u, const char *r, void *priv)
{
	struct cli *cli = priv;

	cli_out(cli, "%s\n", r);
	cli_result(cli, u);
	cli_resume(cli);
}

static void
m_cli_func_passthrough(struct cli *cli, char **av, void *priv)
{

	(void)av;
	(void)priv;

	cli_suspend(cli);
	mgt_child_request(cli_passthrough_cb, cli, &av[2], av[1]);
}
#endif

/*--------------------------------------------------------------------*/

static char *
vcl_default(const char *bflag)
{
	char *buf, *vf;
	const char *p, *q;
	struct sbuf *sb;

	/*
	 * XXX: should do a "HEAD /" on the -b argument to see that
	 * XXX: it even works.  On the other hand, we should do that
	 * XXX: for all backends in the cache process whenever we
	 * XXX: change config, but for a complex VCL, it might not be
	 * XXX: a bug for a backend to not reply at that time, so then
	 * XXX: again: we should check it here in the "trivial" case.
	 */
	p = strchr(bflag, ' ');
	if (p != NULL) {
		q = p + 1;
	} else {
		p = strchr(bflag, '\0');
		assert(p != NULL);
		q = "http";
	}
	
	buf = NULL;
	asprintf(&buf,
	    "backend default {\n"
	    "    set backend.host = \"%*.*s\";\n"
	    "    set backend.port = \"%s\";\n"
	    "}\n", (int)(p - bflag), (int)(p - bflag), bflag, q);
	assert(buf != NULL);
	sb = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
	assert(sb != NULL);
	vf = VCC_Compile(sb, buf, NULL);
	sbuf_finish(sb);
	if (sbuf_len(sb) > 0) {
		fprintf(stderr, "%s", sbuf_data(sb));
		free(buf);
		sbuf_delete(sb);
		return (NULL);
	}
	sbuf_delete(sb);
	free(buf);
	return (vf);
}
#if 0
static void
m_cli_func_config_inline(struct cli *cli, char **av, void *priv)
{
	char *vf;
	struct sbuf *sb;

	(void)priv;

	sb = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
	assert(sb != NULL);
	vf = VCC_Compile(sb, av[3], NULL);
	sbuf_finish(sb);
	if (sbuf_len(sb) > 0) {
		cli_out(cli, "%s", sbuf_data(sb));
		sbuf_delete(sb);
		return;
	}
	sbuf_delete(sb);
	cli_suspend(cli);
	mgt_child_request(cli_passthrough_cb, cli, NULL,
	    "config.load %s %s", av[2], vf);
}

/* XXX: m prefix to avoid name clash */
static void
m_cli_func_config_load(struct cli *cli, char **av, void *priv)
{
	char *vf;
	struct sbuf *sb;

	(void)priv;

	sb = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
	assert(sb != NULL);
	vf = VCC_CompileFile(sb, av[3]);
	sbuf_finish(sb);
	if (sbuf_len(sb) > 0) {
		cli_out(cli, "%s", sbuf_data(sb));
		sbuf_delete(sb);
		return;
	}
	sbuf_delete(sb);
	cli_suspend(cli);
	mgt_child_request(cli_passthrough_cb, cli, NULL,
	    "config.load %s %s", av[2], vf);
}
#endif

static char *
vcl_file(const char *fflag)
{
	char *vf;
	struct sbuf *sb;

	sb = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
	assert(sb != NULL);
	vf = VCC_CompileFile(sb, fflag);
	sbuf_finish(sb);
	if (sbuf_len(sb) > 0) {
		fprintf(stderr, "%s", sbuf_data(sb));
		sbuf_delete(sb);
		return (NULL);
	}
	sbuf_delete(sb);
	return (vf);
}

#if 0

/*--------------------------------------------------------------------*/

static void
m_cli_func_server_start(struct cli *cli, char **av, void *priv)
{

	(void)cli;
	(void)av;
	(void)priv;

	mgt_child_start();
}

/*--------------------------------------------------------------------*/

static void
m_cli_func_server_stop(struct cli *cli, char **av, void *priv)
{

	(void)cli;
	(void)av;
	(void)priv;

	mgt_child_stop();
}

/*--------------------------------------------------------------------*/

static void
m_cli_func_exit(struct cli *cli, char **av, void *priv)
{

	(void)cli;
	(void)av;
	(void)priv;
	mgt_child_kill();
	exit (0);
}

/*--------------------------------------------------------------------*/

static void
m_cli_func_verbose(struct cli *cli, char **av, void *priv)
{

	(void)av;
	(void)priv;

	cli->verbose = !cli->verbose;
}


static void
m_cli_func_ping(struct cli *cli, char **av, void *priv)
{
	time_t t;

	(void)priv;

	if (av[2] != NULL) {
		cli_out(cli, "Got your %s\n", av[2]);
	} 
	t = time(NULL);
	cli_out(cli, "PONG %ld\n", t);
}

/*--------------------------------------------------------------------*/

static void
m_cli_func_stats(struct cli *cli, char **av, void *priv)
{

	(void)av;
	(void)priv;

	assert (VSL_stats != NULL);
#define MAC_STAT(n,t,f,d) \
    cli_out(cli, "%12ju  " d "\n", (VSL_stats->n));
#include "stat_field.h"
#undef MAC_STAT
}

/*--------------------------------------------------------------------*/

static struct cli_proto cli_proto[] = {
	/* URL manipulation */
	{ CLI_URL_QUERY,	m_cli_func_passthrough, NULL },
	{ CLI_URL_PURGE,	m_cli_func_passthrough, NULL },
	{ CLI_URL_STATUS,	m_cli_func_passthrough, NULL },
	{ CLI_CONFIG_LOAD,	m_cli_func_config_load, NULL },
	{ CLI_CONFIG_INLINE,	m_cli_func_config_inline, NULL },
	{ CLI_CONFIG_UNLOAD,	m_cli_func_passthrough, NULL },
	{ CLI_CONFIG_LIST,	m_cli_func_passthrough, NULL },
	{ CLI_CONFIG_USE,	m_cli_func_passthrough, NULL },
	{ CLI_SERVER_FREEZE,	m_cli_func_passthrough, NULL },
	{ CLI_SERVER_THAW,	m_cli_func_passthrough, NULL },
	{ CLI_SERVER_SUSPEND,	m_cli_func_passthrough, NULL },
	{ CLI_SERVER_RESUME,	m_cli_func_passthrough, NULL },
	{ CLI_SERVER_STOP,	m_cli_func_server_stop, NULL },
	{ CLI_SERVER_START,	m_cli_func_server_start, NULL },
	{ CLI_SERVER_RESTART },
	{ CLI_PING,		m_cli_func_ping, NULL },
	{ CLI_STATS,		m_cli_func_stats, NULL },
	{ CLI_ZERO },
	{ CLI_HELP,		cli_func_help, cli_proto },
	{ CLI_VERBOSE,		m_cli_func_verbose, NULL },
	{ CLI_EXIT, 		m_cli_func_exit, NULL},
	{ CLI_QUIT },
	{ CLI_BYE },
	{ NULL }
};

#endif

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
		i = poll(pfd, 2, INFTIM);
		for (k = 0; k < 2; k++) {
			if (pfd[k].revents) {
				j = read(pipes[k][0], buf, sizeof buf);
				if (j == 0)
					exit (0);
				if (j > 0) {
					i = write(pipes[k][1], buf, j);
					if (i != j)
						err(1, "i = %d j = %d\n", i, j);
				}
			}
		}
	}
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
 
	VCC_InitCompile(default_vcl);

	heritage.default_ttl = 120;
	heritage.wthread_min = 1;
	heritage.wthread_max = UINT_MAX;
	heritage.wthread_timeout = 10;
	heritage.mem_workspace = 4096;

	while ((o = getopt(argc, argv, "b:df:h:p:s:t:w:")) != -1)
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

	if (bflag != NULL)
		heritage.vcl_file = vcl_default(bflag);
	else
		heritage.vcl_file = vcl_file(fflag);
	if (heritage.vcl_file == NULL)
		exit (1);

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

	if (dflag)
		DebugStunt();
	daemon(dflag, dflag);
	if (dflag)
		printf("%d\n%d\n%d\n", getpid(), getsid(0), getpgrp());

	mgt_cli_init();

	mgt_run(dflag);

	exit(0);
}
