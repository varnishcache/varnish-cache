#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "vxp.h"
#include "vas.h"
#include "vsb.h"
#include "miniobj.h"

static void
usage(void)
{
	fprintf(stderr, "Usage: vxp_test -q <query-expression>\n");
	exit(1);
}

int
main(int argc, char * const *argv)
{
	struct vsb *vsb;
	struct vex *vex;
	char *q_arg = NULL;
	char opt;

	while ((opt = getopt(argc, argv, "q:")) != -1) {
		switch (opt) {
		case 'q':
			REPLACE(q_arg, optarg);
			break;
		default:
			usage();
		}
	}
	if (q_arg == NULL || optind != argc)
		usage();

	vsb = VSB_new_auto();
	AN(vsb);
	vex = vex_New(q_arg, vsb);

	if (vex == NULL) {
		VSB_finish(vsb);
		fprintf(stderr, "Error:\n%s", VSB_data(vsb));
		VSB_delete(vsb);
		free(q_arg);
		exit(1);
	}
	VSB_delete(vsb);

	vex_Free(&vex);
	AZ(vex);
	free(q_arg);

	return (0);
}
