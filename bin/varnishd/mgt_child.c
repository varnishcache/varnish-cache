/*
 * $Id$
 *
 * The mechanics of handling the child process
 */

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <sys/wait.h>
#include <sys/queue.h>

#include <event.h>
#include <sbuf.h>

#include <libvarnish.h>
#include <cli.h>

#include "cli_event.h"	/* for cli_encode_string */
#include "heritage.h"
#include "mgt.h"

/*--------------------------------------------------------------------*/

static enum {
	H_STOP = 0,
	H_START
}	desired;

static pid_t	child_pid;
static int	child_fds[2];

static struct bufferevent *child_std;
static struct bufferevent *child_cli0, *child_cli1;

static struct event ev_child_pingpong;

struct creq {
	TAILQ_ENTRY(creq)	list;
	char			*req;
	char			**argv;
	mgt_ccb_f		*func;
	void			*priv;
};

static TAILQ_HEAD(,creq)	creqhead = TAILQ_HEAD_INITIALIZER(creqhead);

/*--------------------------------------------------------------------
 * Handle stdout+stderr from the child.
 */

static void
std_rdcb(struct bufferevent *bev, void *arg)
{
	const char *p;

	while (1) {
		p = evbuffer_readline(bev->input);
		if (p == NULL)
			return;
		printf("Child said <%s>\n", p);
	}
}

static void
std_wrcb(struct bufferevent *bev, void *arg)
{

	printf("%s(%p, %p)\n", __func__, (void*)bev, arg);
	exit (2);
}

static void
std_excb(struct bufferevent *bev, short what, void *arg)
{

	printf("%s(%p, %d, %p)\n", __func__, (void*)bev, what, arg);
	exit (2);
}

/*--------------------------------------------------------------------
 * Multiplex requests/answers to the child
 */

static void
send_req(void)
{
	struct creq *cr;
	int u;

	cr = TAILQ_FIRST(&creqhead);
	if (cr == NULL)
		return;
	printf("Send Request <%s>\n", cr->req);
	evbuffer_add_printf(child_cli1->output, "%s", cr->req);
	for (u = 0; cr->argv != NULL && cr->argv[u] != NULL; u++) {
		evbuffer_add_printf(child_cli1->output, " ");
		cli_encode_string(child_cli1->output, cr->argv[u]);
	}
	evbuffer_add_printf(child_cli1->output, "\n");
	bufferevent_enable(child_cli1, EV_WRITE);
}

void
mgt_child_request(mgt_ccb_f *func, void *priv, char **argv, const char *fmt, ...)
{
	struct creq *cr;
	va_list	ap;
	int i;

	cr = calloc(sizeof *cr, 1);
	assert(cr != NULL);
	cr->func = func;
	cr->priv = priv;
	cr->argv = argv;
	va_start(ap, fmt);
	vasprintf(&cr->req, fmt, ap);
	va_end(ap);
	i = TAILQ_EMPTY(&creqhead);
	TAILQ_INSERT_TAIL(&creqhead, cr, list);
	if (i)
		send_req();
}

static void
cli_rdcb(struct bufferevent *bev, void *arg)
{
	const char *p;
	char **av;
	struct creq *cr;

	p = evbuffer_readline(bev->input);
	if (p == NULL)
		return;
	cr = TAILQ_FIRST(&creqhead);
	assert(cr != NULL);
	av = ParseArgv(p, 0);
	if (av[0] != NULL) 
		cr->func(CLIS_SYNTAX, av[0], cr->priv);
	else
		cr->func(strtoul(av[1], NULL, 0), av[2], cr->priv);
	FreeArgv(av);
	TAILQ_REMOVE(&creqhead, cr, list);
	free(cr->req);
	free(cr);
	send_req();
}

static void
cli_wrcb(struct bufferevent *bev __unused, void *arg __unused)
{

}

static void
cli_excb(struct bufferevent *bev, short what, void *arg)
{

	printf("%s(%p, %d, %p)\n", __func__, (void*)bev, what, arg);
	exit (2);
}

/*--------------------------------------------------------------------*/

static void
child_pingpong_ccb(unsigned u, const char *r, void *priv)
{
	printf("%s(%u, \"%s\", %p)\n", __func__, u, r, priv);
	/* XXX: reset keepalive timer */
}


static void
child_pingpong(int a, short b, void *c)
{
	time_t t;
	struct timeval tv;

	printf("%s(%d, %d, %p)\n", __func__, a, b, c);
	time(&t);
	mgt_child_request(child_pingpong_ccb, NULL, NULL, "ping %ld", t);
	if (1) {
		tv.tv_sec = 30;
		tv.tv_usec = 0;
		evtimer_del(&ev_child_pingpong);
		evtimer_add(&ev_child_pingpong, &tv);
	}
}


/*--------------------------------------------------------------------*/

static void
start_child(void)
{
	int i;

	assert(pipe(&heritage.fds[0]) == 0);
	assert(pipe(&heritage.fds[2]) == 0);
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
	child_cli0 = bufferevent_new(heritage.fds[0],
	    cli_rdcb, cli_wrcb, cli_excb, NULL);
	assert(child_cli0 != NULL);
	bufferevent_enable(child_cli0, EV_READ);
	child_cli1 = bufferevent_new(heritage.fds[3],
	    cli_rdcb, cli_wrcb, cli_excb, NULL);
	assert(child_cli1 != NULL);

	evtimer_set(&ev_child_pingpong, child_pingpong, NULL);
	child_pingpong(0, 0, NULL);
}


/*--------------------------------------------------------------------*/

void
mgt_child_start(void)
{

	if (desired == H_START)
		return;
	desired = H_START;
	start_child();
}

/*--------------------------------------------------------------------*/

void
mgt_child_stop(void)
{

	if (desired == H_STOP)
		return;
	desired = H_STOP;
}

/*--------------------------------------------------------------------*/

void
mgt_sigchld(int a, short b, void *c)
{
	pid_t p;
	int status;

	printf("sig_chld(%d, %d, %p)\n", a, b, c);

	p = wait4(-1, &status, WNOHANG, NULL);
	if (p == 0)
		return;
	printf("pid = %d status = 0x%x\n", p, status);
	assert(p == child_pid);

	bufferevent_free(child_std); /* XXX: is this enough ? */
	child_std = NULL;

	close(heritage.fds[0]);
	close(heritage.fds[1]);
	close(heritage.fds[2]);
	close(heritage.fds[3]);
	close(child_fds[0]);
	close(child_fds[1]);

	if (desired == H_START)
		start_child();
}
