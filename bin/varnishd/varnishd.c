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

	exit(0);
}
