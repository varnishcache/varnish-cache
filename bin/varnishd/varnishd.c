/*
 * $Id$
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void
usage(void)
{
	fprintf(stderr, "usage: varnishd\n");
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

	exit(0);
}
