/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "varnish.h"

static void
varnish(void)
{
	listener_t *l;
	connection_t *c;
	request_t *r;

	l = listener_create(8080);
	while ((c = listener_accept(l)) != NULL) {
		r = request_wait(c, 0);
		/* ... */
		request_destroy(r);
		connection_destroy(c);
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

	LOG_UNREACHABLE();
	exit(1);
}
