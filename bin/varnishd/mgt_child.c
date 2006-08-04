/*
 * $Id$
 *
 * The mechanics of handling the child process
 */

#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <err.h>		/* XXX */

#include "libvarnish.h"
#include "heritage.h"
#include "mgt.h"

static pid_t		child_pid = -1;
static pid_t		mgr_pid;
static int		child_fds[2];
static unsigned 	child_should_run;
static pthread_t	child_listen_thread;
static pthread_t	child_poker_thread;
static pthread_mutex_t	child_mtx;
static pthread_cond_t	child_cv;
static unsigned		child_ticker;
static unsigned		gotint;
static unsigned		dstarts;

/*--------------------------------------------------------------------*/

static void *
child_listener(void *arg)
{
	int i;
	char buf[BUFSIZ];

	(void)arg;

	while (1) {
		i = read(child_fds[0], buf, sizeof buf - 1);
		if (i <= 0)
			break;
		buf[i] = '\0';
		printf("Child said: %s", buf);
	}
	return (NULL);
}

/*--------------------------------------------------------------------*/

static void *
child_poker(void *arg)
{

	(void)arg;
	while (1) {
		sleep (1);
		if (!mgt_cli_askchild(NULL, NULL, "ping\n"))
			child_ticker = 0;
	}
}

/*--------------------------------------------------------------------*/

static void
start_child(void)
{
	int i;

	AZ(pipe(&heritage.fds[0]));
	AZ(pipe(&heritage.fds[2]));
	AZ(pipe(child_fds));
	i = fork();
	if (i < 0) 
		errx(1, "Could not fork child");
	if (i == 0) {
		/* Redirect stdin/out/err */
		AZ(close(0));
		i = open("/dev/null", O_RDONLY);
		assert(i == 0);
		assert(dup2(child_fds[1], 1) == 1);
		assert(dup2(child_fds[1], 2) == 2);
		AZ(close(child_fds[0]));
		AZ(close(child_fds[1]));

		AZ(close(heritage.fds[0]));
		AZ(close(heritage.fds[3]));

		setproctitle("Varnish-Chld");
		child_main();

		exit (1);
	}

	printf("start child pid %d\n", i);

	AZ(close(child_fds[1]));
	child_fds[1] = -1;

	mgt_cli_start_child(heritage.fds[0], heritage.fds[3]);
	AZ(close(heritage.fds[1]));
	heritage.fds[1] = -1;
	AZ(close(heritage.fds[2]));
	heritage.fds[2] = -1;
	child_pid = i;
	AZ(pthread_create(&child_listen_thread, NULL, child_listener, NULL));
	AZ(pthread_detach(child_listen_thread));
	AZ(pthread_create(&child_poker_thread, NULL, child_poker, NULL));
	AZ(pthread_detach(child_poker_thread));
}

/*--------------------------------------------------------------------*/

static void
stop_child(void)
{
	int i;

	assert(child_pid != -1);

	printf("Stop child\n");
	AZ(pthread_cancel(child_poker_thread));
	mgt_cli_stop_child();

	/* We tell the child to die gracefully by closing the CLI */
	AZ(close(heritage.fds[0]));
	heritage.fds[0] = -1;
	AZ(close(heritage.fds[3]));
	heritage.fds[3] = -1;

	/*
	 * Give it one second to die, then wack it hard
	 * then another second and then we get real angry
	 */
	for (i = 0; i < 30; i++) {
		printf("Waiting %d %d\n",i, child_pid);
		if (child_pid == -2)
			break;
		if (i == 10) {
			printf("Giving cacher SIGINT\n");
			kill(child_pid, SIGINT);
		}
		if (i == 20) {
			printf("Giving cacher SIGKILL\n");
			kill(child_pid, SIGKILL);
		}
		usleep(100000);
	}

	assert(child_pid == -2);

	AZ(close(child_fds[0]));
	child_fds[0] = -1;
	child_pid = -1;
	printf("Child stopped\n");
}

/*--------------------------------------------------------------------*/

static void
mgt_sigchld(int arg)
{
	int status;
	pid_t r;

	(void)arg;
	r = wait4(-1, &status, WNOHANG, NULL);
	if (r == child_pid) {
		printf("Cache child died pid=%d status=0x%x\n",
		    r, status);
		child_pid = -2;
	} else {
		printf("Unknown child died pid=%d status=0x%x\n",
		    r, status);
	}
}

/*--------------------------------------------------------------------*/

static void
mgt_sigint(int arg)
{

	(void)arg;
	if (getpid() != mgr_pid) {
		printf("Got SIGINT\n");
		exit (2);
	}
	printf("Manager got SIGINT\n");
	gotint = 1;
	child_should_run = 0;
}

/*--------------------------------------------------------------------
 * This thread is the master thread in the management process.
 * The relatively simple task is to start and stop the child process
 * and to reincarnate it in case of trouble.
 */

void
mgt_run(int dflag)
{
	struct timespec to;
	struct sigaction sac;
	int i;

	mgr_pid = getpid();

	if (dflag)
		mgt_cli_setup(0, 1, 1);

	sac.sa_handler = mgt_sigchld;
	sac.sa_flags = SA_RESTART | SA_NOCLDSTOP;
	AZ(sigaction(SIGCHLD, &sac, NULL));

	sac.sa_handler = mgt_sigint;
	sac.sa_flags = SA_RESTART;
	AZ(sigaction(SIGINT, &sac, NULL));
	AZ(sigaction(SIGTERM, &sac, NULL));

	setproctitle("Varnish-Mgr");

	sac.sa_handler = SIG_IGN;
	sac.sa_flags = SA_RESTART;
	AZ(sigaction(SIGPIPE, &sac, NULL));
	AZ(sigaction(SIGHUP, &sac, NULL));

	child_should_run = !dflag;

	AZ(pthread_cond_init(&child_cv, NULL));
	AZ(pthread_mutex_init(&child_mtx, NULL));

	while (1) {
		if (child_should_run && child_pid == -2)
			stop_child();
		if (!child_should_run && child_pid != -1)
			stop_child();
		if (gotint) {
			printf("Manager died due to sigint\n");
			exit(2);
		}
		if (child_should_run && child_pid == -1) {
			if (dflag && dstarts) {
				printf(
				    "Manager not autostarting in debug mode\n");
				exit(2);
			}
			start_child();
			dstarts = 1;
		}
			
		/* XXX POSIXBRAINDAMAGE */
		AZ(clock_gettime(CLOCK_REALTIME, &to));
		to.tv_sec += 1;

		AZ(pthread_mutex_lock(&child_mtx));
		i = pthread_cond_timedwait(&child_cv, &child_mtx, &to);
		AZ(pthread_mutex_unlock(&child_mtx));
		if (i == ETIMEDOUT && ++child_ticker > 5 && child_pid != -1) {
			stop_child();
			if (dflag)
				exit (2);
		}
	}
}

/*--------------------------------------------------------------------*/

void
mgt_start_child(void)
{

	dstarts = 0;
	child_should_run = 1;
	AZ(pthread_cond_signal(&child_cv));
}

void
mgt_stop_child(void)
{

	child_should_run = 0;
	AZ(pthread_cond_signal(&child_cv));
}
