/*
 * $Id$
 */

#include <sys/wait.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "varnish.h"
#include "connection.h"
#include "listener.h"
#include "log.h"
#include "request.h"
#include "system.h"

static void
varnish_child(listener_t *l)
{
	connection_t *c;
	request_t *r;

	while ((c = listener_accept(l)) != NULL) {
		r = request_wait(c, 0);
		/* ... */
		request_destroy(r);
		connection_destroy(c);
	}
	LOG_UNREACHABLE();
}

static void
varnish(void)
{
	listener_t *l;
	int i, status;
	pid_t pid;

	system_init();
	log_info("starting Varnish");
	l = listener_create(8080);
	for (i = 0; i < sys.ncpu; ++i) {
		switch ((pid = system_fork())) {
		case -1:
			log_panic("fork()");
			break;
		case 0:
			varnish_child(l);
			_exit(1);
			break;
		default:
			log_info("forked child %lu", (unsigned long)pid);
			break;
		}
	}
	for (;;) {
		if ((pid = wait(&status)) == -1) {
			if (errno == ECHILD)
				return;
		} else {
			log_info("child %lu exited", (unsigned long)pid);
		}
	}
}

static void
usage(void)
{
	fprintf(stderr, "usage: varnish\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	int o;

	while ((o = getopt(argc, argv, "")) != -1)
		switch (o) {
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	if (argc != 0)
		usage();

	varnish();

	exit(1);
}
