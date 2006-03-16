/*
 * $Id$
 */

#include <errno.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#include <sys/wait.h>

#include <event.h>
#include <sbuf.h>

#include <cli.h>
#include <cli_priv.h>
#include <libvarnish.h>

#include "heritage.h"
#include "cli_event.h"

/*--------------------------------------------------------------------*/

static enum {
	H_STOP = 0,
	H_START,
}	desired;
static pid_t	child_pid;
static int	child_fds[2];

struct heritage heritage;

static struct event_base *eb;

static struct bufferevent *child_std;

/*--------------------------------------------------------------------*/

static void
std_rdcb(struct bufferevent *bev, void *arg)
{
	const char *p;

	p = evbuffer_readline(bev->input);
	if (p == NULL)
		return;
	printf("Child said <%s>\n", p);
}

static void
std_wrcb(struct bufferevent *bev, void *arg)
{

	printf("%s(%p, %p)\n", __func__, bev, arg);
	exit (2);
}

static void
std_excb(struct bufferevent *bev, short what, void *arg)
{

	printf("%s(%p, %d, %p)\n", __func__, bev, what, arg);
	exit (2);
}



/*--------------------------------------------------------------------*/

static void
start_child(void)
{
	int i;

	assert(pipe(heritage.fds) == 0);
	assert(pipe(child_fds) == 0);
	i = fork();
	if (i < 0) 
		errx(1, "Could not fork child");
	if (i == 0) {
		/* XXX: close fds */
		/* XXX: (re)set signals */

		/* Redirect stdin/out/err */
		close(0);
		i = open("/dev/null", O_RDONLY);
		assert(i == 0);
		close(child_fds[0]);
		dup2(child_fds[1], 1);
		dup2(child_fds[1], 2);
		close(child_fds[1]);

		child_main();

		exit (1);
	}
	child_pid = i;
	printf("start child pid %d\n", i);

	/*
 	 * We do not close the unused ends of the pipes here to avoid
	 * doing SIGPIPE handling.
	 */
	child_std = bufferevent_new(child_fds[0],
	    std_rdcb, std_wrcb, std_excb, NULL);
	assert(child_std != NULL);
	bufferevent_enable(child_std, EV_READ);
}

/*--------------------------------------------------------------------*/

static void
sig_chld(int a, short b, void *c)
{
	pid_t p;
	int status;

	printf("sig_chld(%d, %d, %p)\n", a, b, c);

	p = wait4(-1, &status, WNOHANG, NULL);
	printf("pid = %d status = 0x%x\n", p, status);
	assert(p == child_pid);

	bufferevent_free(child_std); /* XXX: is this enough ? */
	child_std = NULL;

	close(heritage.fds[0]);
	close(heritage.fds[1]);
	close(child_fds[0]);
	close(child_fds[1]);

	if (desired == H_START)
		start_child();
}

/*--------------------------------------------------------------------*/

static void
cli_func_server_start(struct cli *cli, char **av __unused, void *priv __unused)
{

	if (desired != H_START) {
		desired = H_START;
		start_child();
	}
}

/*--------------------------------------------------------------------*/

static void
cli_func_server_stop(struct cli *cli, char **av __unused, void *priv __unused)
{

	if (desired != H_STOP) {
		desired = H_STOP;
#if 0
		stop_child();
#endif
	}
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
	{ CLI_URL_QUERY },
	{ CLI_URL_PURGE },
	{ CLI_URL_STATUS },
	{ CLI_CONFIG_LOAD },
	{ CLI_CONFIG_INLINE },
	{ CLI_CONFIG_UNLOAD },
	{ CLI_CONFIG_LIST },
	{ CLI_CONFIG_USE },
	{ CLI_SERVER_FREEZE },
	{ CLI_SERVER_THAW },
	{ CLI_SERVER_SUSPEND },
	{ CLI_SERVER_RESUME },
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

	eb = event_init();
	assert(eb != NULL);

	cli = cli_setup(0, 1, 1, cli_proto);

	signal_set(&e_sigchld, SIGCHLD, sig_chld, NULL);
	signal_add(&e_sigchld, NULL);

	i = event_dispatch();
	if (i != 0)
		printf("event_dispatch() = %d\n", i);

}

/*--------------------------------------------------------------------*/

static void
usage(void)
{
	fprintf(stderr, "usage: varnishd [options]\n");
	fprintf(stderr, "    %-20s # %s\n", "-d", "debug");
	fprintf(stderr, "    %-20s # %s\n", "-p number", "TCP listen port");
#if 0
	-c clusterid@cluster_controller
	-f config_file
	-m memory_limit
	-s kind[,storage-options]
	-l logfile,logsize
	-b backend ip...
	-u uid
	-a CLI_port
#endif
	exit(1);
}

/*--------------------------------------------------------------------*/

int
main(int argc, char *argv[])
{
	int o;
	unsigned portnumber = 8080;
	unsigned dflag = 1;	/* XXX: debug=on for now */

	while ((o = getopt(argc, argv, "dp:")) != -1)
		switch (o) {
		case 'd':
			dflag++;
			break;
		case 'p':
			portnumber = strtoul(optarg, NULL, 0);
			break;
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	if (argc != 0)
		usage();

	testme();


	exit(0);
}
