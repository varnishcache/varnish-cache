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

#include "vcl_lang.h"

#include "mgt.h"
#include "heritage.h"
#include "cli_event.h"

/*--------------------------------------------------------------------*/

struct heritage heritage;
struct event_base *mgt_eb;

/*--------------------------------------------------------------------
 * Generic passthrough for CLI functions
 */

void
cli_passthrough_cb(unsigned u, const char *r, void *priv)
{
	struct cli *cli = priv;

	cli_out(cli, "%s\n", r);
	cli_result(cli, u);
	cli_resume(cli);
}

static void
cli_func_passthrough(struct cli *cli, char **av __unused, void *priv)
{

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
	    "        pass;\n"
	    "    }\n"
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
	    "", bflag);
	assert(buf != NULL);
	sb = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
	assert(sb != NULL);
	vf = VCL_Compile(sb, buf, NULL);
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
cli_func_config_inline(struct cli *cli, char **av, void *priv __unused)
{
	char *vf;
	struct sbuf *sb;

	sb = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
	assert(sb != NULL);
	vf = VCL_Compile(sb, av[3], NULL);
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
m_cli_func_config_load(struct cli *cli, char **av, void *priv __unused)
{
	char *vf;
	struct sbuf *sb;

	sb = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
	assert(sb != NULL);
	vf = VCL_CompileFile(sb, av[3]);
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
	vf = VCL_CompileFile(sb, fflag);
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
cli_func_server_start(struct cli *cli, char **av __unused, void *priv __unused)
{

	mgt_child_start();
}

/*--------------------------------------------------------------------*/

static void
cli_func_server_stop(struct cli *cli, char **av __unused, void *priv __unused)
{

	mgt_child_stop();
}

/*--------------------------------------------------------------------*/

static void
cli_func_verbose(struct cli *cli, char **av __unused, void *priv)
{

	cli->verbose = !cli->verbose;
}


static void
cli_func_ping(struct cli *cli, char **av, void *priv __unused)
{
	time_t t;

	if (av[2] != NULL) {
		cli_out(cli, "Got your %s\n", av[2]);
	} 
	time(&t);
	cli_out(cli, "PONG %ld\n", t);
}

/*--------------------------------------------------------------------*/

static struct cli_proto cli_proto[] = {
	/* URL manipulation */
	{ CLI_URL_QUERY,	cli_func_passthrough, NULL },
	{ CLI_URL_PURGE,	cli_func_passthrough, NULL },
	{ CLI_URL_STATUS,	cli_func_passthrough, NULL },
	{ CLI_CONFIG_LOAD,	m_cli_func_config_load, NULL },
	{ CLI_CONFIG_INLINE,	cli_func_config_inline, NULL },
	{ CLI_CONFIG_UNLOAD,	cli_func_passthrough, NULL },
	{ CLI_CONFIG_LIST,	cli_func_passthrough, NULL },
	{ CLI_CONFIG_USE,	cli_func_passthrough, NULL },
	{ CLI_SERVER_FREEZE,	cli_func_passthrough, NULL },
	{ CLI_SERVER_THAW,	cli_func_passthrough, NULL },
	{ CLI_SERVER_SUSPEND,	cli_func_passthrough, NULL },
	{ CLI_SERVER_RESUME,	cli_func_passthrough, NULL },
	{ CLI_SERVER_STOP,	cli_func_server_stop, NULL },
	{ CLI_SERVER_START,	cli_func_server_start, NULL },
	{ CLI_SERVER_RESTART },
	{ CLI_PING,		cli_func_ping, NULL },
	{ CLI_STATS },
	{ CLI_ZERO },
	{ CLI_HELP,		cli_func_help, cli_proto },
	{ CLI_VERBOSE,		cli_func_verbose, NULL },
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

static void
usage(void)
{
	fprintf(stderr, "usage: varnishd [options]\n");
	fprintf(stderr, "    %-28s # %s\n", "-b", "backend_IP_number");
	fprintf(stderr, "    %-28s # %s\n", "-d", "debug");
	fprintf(stderr, "    %-28s # %s\n", "-f", "VCL_file");
	fprintf(stderr, "    %-28s # %s\n", "-p number", "TCP listen port");
	fprintf(stderr, "    %-28s # %s\n",
	    "-s kind[,storageoptions]", "Backend storage specification");
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

	q = p = strchr(sflag, ',');
	if (p == NULL)
		q = p = strchr(sflag, '\0');
	else
		q++;
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

#include "shmlog.h"

static void
init_vsl(const char *fn, unsigned size)
{
	struct shmloghead slh;
	int i;

	heritage.vsl_fd = open(fn, O_RDWR | O_CREAT, 0600);
	if (heritage.vsl_fd < 0) {
		fprintf(stderr, "Could not open %s: %s\n",
		    fn, strerror(errno));
		exit (1);
	}
	i = read(heritage.vsl_fd, &slh, sizeof slh);
	if (i == sizeof slh && slh.magic == SHMLOGHEAD_MAGIC) {
		/* XXX more checks */
		heritage.vsl_size = slh.size + slh.start;
		return;
	}
	slh.magic = SHMLOGHEAD_MAGIC;
	slh.size = size;
	slh.ptr = 0;
	slh.start = sizeof slh;
	AZ(lseek(heritage.vsl_fd, 0, SEEK_SET));
	i = write(heritage.vsl_fd, &slh, sizeof slh);
	assert(i == sizeof slh);
	AZ(ftruncate(heritage.vsl_fd, sizeof slh + size));
	heritage.vsl_size = slh.size + slh.start;
}

/*--------------------------------------------------------------------*/

/* for development purposes */
#include <printf.h>
#include <err.h>

int
main(int argc, char *argv[])
{
	int o;
	const char *portnumber = "8080";
	unsigned dflag = 1;	/* XXX: debug=on for now */
	const char *bflag = NULL;
	const char *fflag = NULL;
	const char *sflag = "file";

	register_printf_render_std((const unsigned char *)"HVQ");
 
	VCL_InitCompile();

	while ((o = getopt(argc, argv, "b:df:p:s:")) != -1)
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
		case 'p':
			portnumber = optarg;
			break;
		case 's':
			sflag = optarg;
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

	/*
	 * XXX: Lacking the suspend/resume facility (due to the socket API
	 * missing an unlisten(2) facility) we may want to push this into
	 * the child to limit the amount of time where the socket(s) exists
	 * but do not answer.  That, on the other hand, would eliminate the
	 * possibility of doing a "no-glitch" restart of the child process.
	 */
	open_tcp(portnumber);

	init_vsl(SHMLOG_FILENAME, 1024*1024);

	testme();

	exit(0);
}
