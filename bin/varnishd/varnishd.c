/*
 * $Id$
 *
 * The management process and CLI handling
 */

#include <assert.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <sys/wait.h>

#include <event.h>
#include <sbuf.h>

#include <cli.h>
#include <cli_priv.h>
#include <libvarnish.h>
#include <libvcl.h>

#include "mgt.h"
#include "heritage.h"
#include "shmlog.h"
#include "cli_event.h"

/*--------------------------------------------------------------------*/

struct heritage heritage;
struct event_base *mgt_eb;

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

	cli_suspend(cli);
	mgt_child_request(cli_passthrough_cb, cli, &av[2], av[1]);
}

/*--------------------------------------------------------------------*/

static char *
vcl_default(const char *bflag)
{
	char *buf, *vf;
	struct sbuf *sb;

	/*
	 * XXX: should do a "HEAD /" on the -b argument to see that
	 * XXX: it even works.  On the other hand, we should do that
	 * XXX: for all backends in the cache process whenever we
	 * XXX: change config, but for a complex VCL, it might not be
	 * XXX: a bug for a backend to not reply at that time, so then
	 * XXX: again: we should check it here in the "trivial" case.
	 */
	buf = NULL;
	asprintf(&buf,
	    "backend default {\n"
	    "    set backend.host = \"%s\";\n"
	    "}\n"
	    "sub vcl_recv {\n"
	    "    if (req.request != \"GET\" && req.request != \"HEAD\") {\n"
	    "        pipe;\n"
	    "    }\n"
	    "    if (req.http.Authenticate || req.http.Cookie) {\n"
	    "        pass;\n"
	    "    }\n"
	    "    lookup;\n"
	    "}\n"
	    "\n"
	    "sub vcl_hit {\n"
	    "    if (!obj.cacheable) {\n"
	    "        pass;\n"
	    "    }\n"
	    "    deliver;\n"
	    "}\n"
	    "\n"
	    "sub vcl_miss {\n"
	    "    fetch;\n"
	    "}\n"
	    "\n"
	    "sub vcl_fetch {\n"
	    "    if (!obj.valid) {\n"
	    "        error;\n"
	    "    }\n"
	    "    if (!obj.cacheable) {\n"
	    "        pass;\n"
	    "    }\n"
	    "    insert;\n"
	    "}\n"
	    "sub vcl_timeout {\n"
	    "    discard;\n"
	    "}\n"
	    "", bflag);
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
	time(&t);
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
	{ CLI_EXIT },
	{ CLI_QUIT },
	{ CLI_BYE },
	{ NULL }
};

static void
testme(void)
{
	struct event e_sigchld;
	struct cli *cli;
	int i;

	mgt_eb = event_init();
	assert(mgt_eb != NULL);

	cli = cli_setup(mgt_eb, 0, 1, 1, cli_proto);

	signal_set(&e_sigchld, SIGCHLD, mgt_sigchld, NULL);
	signal_add(&e_sigchld, NULL);

	i = event_base_loop(mgt_eb, 0);
	if (i != 0)
		printf("event_dispatch() = %d\n", i);

}

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
	    "backend IP or hostname");
	fprintf(stderr, "    %-28s # %s\n", "-d", "debug");
	fprintf(stderr, "    %-28s # %s\n", "-f file", "VCL_file");
	fprintf(stderr, "    %-28s # %s\n",
	    "-h kind[,hashoptions]", "Hash specification");
	fprintf(stderr, "    %-28s # %s\n", "-p number", "TCP listen port");
	fprintf(stderr, "    %-28s # %s\n",
	    "-s kind[,storageoptions]", "Backend storage specification");
	fprintf(stderr, "    %-28s # %s\n", "-t", "Default TTL");
	fprintf(stderr, "    %-28s # %s\n", "-w int[,int]",
	    "Number of worker threads (fixed/{min,max})");
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

/* for development purposes */
#include <printf.h>
#include <err.h>

int
main(int argc, char *argv[])
{
	int o, i;
	unsigned ua, ub;
	const char *portnumber = "8080";
	unsigned dflag = 1;	/* XXX: debug=on for now */
	const char *bflag = NULL;
	const char *fflag = NULL;
	const char *sflag = "file";
	const char *hflag = "classic";

	register_printf_render_std((const unsigned char *)"HVQ");
 
	VCC_InitCompile();

	heritage.default_ttl = 120;
	heritage.wthread_min = 5;
	heritage.wthread_max = 5;
	heritage.mem_http_1_line= 512;
	heritage.mem_http_header= 4096;
	heritage.mem_workspace = 0;

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
			i = sscanf(optarg, "%u,%u", &ua, &ub);
			if (i == 0)
				usage();
			heritage.wthread_min = ua;
			heritage.wthread_max = ua;
			if (i == 2)
				heritage.wthread_max = ub;
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

	testme();

	exit(0);
}
