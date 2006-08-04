/*
 * $Id$
 *
 * The management process' CLI handling
 */

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <pthread.h>
#include <sys/types.h>

#include "libvarnish.h"
#include "cli_priv.h"
#include "cli.h"
#include "sbuf.h"
#include "common_cli.h"
#include "mgt.h"
#include "shmlog.h"

static int		cli_i = -1, cli_o = -1;
static pthread_mutex_t	cli_mtx;

/*--------------------------------------------------------------------*/

static void
mcf_server_startstop(struct cli *cli, char **av, void *priv)
{

	(void)cli;
	(void)av;
	if (priv != NULL)
		mgt_stop_child();
	else
		mgt_start_child();
}

/*--------------------------------------------------------------------*/

static void
mcf_stats(struct cli *cli, char **av, void *priv)
{

	(void)av;
	(void)priv;

	assert (VSL_stats != NULL);
#define MAC_STAT(n,t,f,d) \
    cli_out(cli, "%12ju  " d "\n", (VSL_stats->n));
#include "stat_field.h"
#undef MAC_STAT
}


/*--------------------------------------------------------------------
 * Passthru of cli commands.  It is more or less just undoing what
 * the cli parser did, but such is life...
 */

static void
mcf_passthru(struct cli *cli, char **av, void *priv)
{
	char *p, *q, *r;
	unsigned u, v;
	int i;

	(void)priv;

	AZ(pthread_mutex_lock(&cli_mtx));

	/* Request */
	if (cli_o <= 0) {
		AZ(pthread_mutex_unlock(&cli_mtx));
		cli_result(cli, CLIS_CANT);
		cli_out(cli, "Cache process not running");
		return;
	}
	v = 0;
	for (u = 1; av[u] != NULL; u++)
		v += strlen(av[u]) + 3;
	p = malloc(v);
	assert(p != NULL);
	q = p;
	for (u = 1; av[u] != NULL; u++) {
		*q++ = '"';
		for (r = av[u]; *r; r++) {
			switch (*r) {
			case '\\':	*q++ = '\\'; *q++ = '\\'; break;
			case '\n':	*q++ = '\\'; *q++ = 'n'; break;
			case '"':	*q++ = '\\'; *q++ = '"'; break;
			default:	*q++ = *r; break;
			}
		}
		*q++ = '"';
		*q++ = ' ';
	}
	*q++ = '\n';
	v = q - p;
	i = write(cli_o, p, v);
	assert(i == v);
	free(p);

	i = cli_readres(cli_i, &u, &p);
	assert(i == 0);
	cli_result(cli, u);
	cli_out(cli, "%s", p);
	free(p);

	AZ(pthread_mutex_unlock(&cli_mtx));
}

/*--------------------------------------------------------------------*/

static struct cli_proto *cli_proto;

static struct cli_proto mgt_cli_proto[] = {
	{ CLI_HELP,		cli_func_help, NULL },	/* must be first */
	{ CLI_PING,		cli_func_ping },
	{ CLI_SERVER_START,	mcf_server_startstop, NULL },
	{ CLI_SERVER_STOP,	mcf_server_startstop, &cli_proto },
	{ CLI_STATS,		mcf_stats, NULL },
	{ CLI_CONFIG_LOAD },
#if 0
	{ CLI_CONFIG_LOAD,	m_cli_func_config_load, NULL },
	{ CLI_CONFIG_INLINE,	m_cli_func_config_inline, NULL },
	{ CLI_SERVER_STOP,	m_cli_func_server_stop, NULL },
	{ CLI_SERVER_RESTART },
	{ CLI_PING,		m_cli_func_ping, NULL },
	{ CLI_ZERO },
	{ CLI_VERBOSE,		m_cli_func_verbose, NULL },
	{ CLI_EXIT, 		m_cli_func_exit, NULL},
#endif
	{ CLI_QUIT },
	{ CLI_BYE },
	{ NULL }
};


/*--------------------------------------------------------------------*/

void
mgt_cli_init(void)
{
	struct cli_proto *cp;
	unsigned u, v;


	AZ(pthread_mutex_init(&cli_mtx, NULL));
	/*
	 * Build the joint cli_proto by combining the manager process
	 * entries with with the cache process entries.  The latter
	 * get a "passthough" function in the joint list
	 */
	u = 0;
	for (cp = mgt_cli_proto; cp->request != NULL; cp++)
		u++;
	for (cp = CLI_cmds; cp->request != NULL; cp++)
		u++;
	cli_proto = calloc(sizeof *cli_proto, u + 1);
	assert(cli_proto != NULL);
	u = 0;
	for (cp = mgt_cli_proto; cp->request != NULL; cp++)
		cli_proto[u++] = *cp;
	for (cp = CLI_cmds; cp->request != NULL; cp++) {
		/* Skip any cache commands we already have in the manager */
		for (v = 0; v < u; v++)
			if (!strcmp(cli_proto[v].request, cp->request))
				break;
		if (v < u)
			continue;
		cli_proto[u] = *cp;
		cli_proto[u].func = mcf_passthru;
		u++;
	}

	/* Fixup the entry for 'help' entry */
	assert(!strcmp(cli_proto[0].request, "help"));
	cli_proto[0].priv = cli_proto;

	/* XXX: open listening sockets, contact cluster server etc */
}

/*--------------------------------------------------------------------
 * Ask the child something over CLI, return zero only if everything is
 * happy happy.
 */

int
mgt_cli_askchild(int *status, char **resp, const char *fmt, ...)
{
	char *p;
	int i, j;
	va_list ap;

	va_start(ap, fmt);
	i = vasprintf(&p, fmt, ap);
	va_end(ap);
	if (i < 0)
		return (i);
	AZ(pthread_mutex_lock(&cli_mtx));
	assert(p[i - 1] == '\n');
	i = write(cli_o, p, strlen(p));
	assert(i == strlen(p));
	free(p);

	i = cli_readres(cli_i, &j, resp);
	AZ(pthread_mutex_unlock(&cli_mtx));
	if (status != NULL)
		*status = j;
	return (j == CLIS_OK ? 0 : j);
}

/*--------------------------------------------------------------------*/

void
mgt_cli_start_child(int fdi, int fdo)
{

	cli_i = fdi;
	cli_o = fdo;
}

/*--------------------------------------------------------------------*/

void
mgt_cli_stop_child(void)
{

	cli_i = -1;
	cli_o = -1;
	/* XXX: kick any users */
}

/*--------------------------------------------------------------------*/

struct cli_port {
	int			fdi;
	int			fdo;
	int			verbose;
	char			*buf;
	unsigned		nbuf;
	unsigned		lbuf;
	struct cli		cli[1];
};

static void *
mgt_cli_main(void *arg)
{
	struct cli_port *cp;
	char *p;
	int i;

	assert(arg != NULL);
	cp = arg;

	cp->lbuf = 4096;
	cp->buf = malloc(cp->lbuf);
	assert(cp->buf != NULL);
	cp->cli->sb = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
	assert(cp->cli->sb != NULL);
	while (1) {
		if (cp->nbuf == cp->lbuf) {
			cp->lbuf += cp->lbuf;
			cp->buf = realloc(cp->buf, cp->lbuf);
			assert(cp->buf != NULL);
		}
		i = read(cp->fdi, cp->buf + cp->nbuf, cp->lbuf - cp->nbuf);
		if (i <= 0)
			break;
		cp->nbuf += i;
		p = strchr(cp->buf, '\n');
		if (p == NULL)
			continue;
		*p = '\0';
		sbuf_clear(cp->cli->sb);
		cli_dispatch(cp->cli, cli_proto, cp->buf);
		sbuf_finish(cp->cli->sb);
		/* XXX: cp->verbose */
		if (cli_writeres(cp->fdo, cp->cli))
			break;
		i = ++p - cp->buf;
		assert(i <= cp->nbuf);
		if (i < cp->nbuf)
			memcpy(cp->buf, p, cp->nbuf - i);
		cp->nbuf -= i;
	}
	sbuf_delete(cp->cli->sb);
	free(cp->buf);
	close(cp->fdi);
	close(cp->fdo);
	free(cp);
	return (NULL);
}

void
mgt_cli_setup(int fdi, int fdo, int verbose)
{
	struct cli_port *cp;
	pthread_t tp;

	cp = calloc(sizeof *cp, 1);
	assert(cp != NULL);

	cp->fdi = fdi;
	cp->fdo = fdo;
	cp->verbose = verbose;
	AZ(pthread_create(&tp, NULL, mgt_cli_main, cp));
	AZ(pthread_detach(tp));
}

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

	{
	struct event e_sigchld;
	struct cli *cli;
	int i;

	mgt_eb = event_init();
	assert(mgt_eb != NULL);

	if (dflag)
		cli = cli_setup(mgt_eb, 0, 1, 1, cli_proto);

	signal_set(&e_sigchld, SIGCHLD, mgt_sigchld, NULL);
	AZ(event_base_set(mgt_eb, &e_sigchld));
	AZ(signal_add(&e_sigchld, NULL));

	mgt_child_start();

	i = event_base_loop(mgt_eb, 0);
	if (i != 0)
		printf("event_dispatch() = %d\n", i);

	}

	exit(0);
}
#endif
