#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "vxp.h"
#include "vas.h"
#include "vsb.h"

int
main(int argc, char **argv)
{
	int i;
	unsigned l;
	char *s;
	struct vsb *vsb;
	struct vex *vex;

	l = 0;
	for (i = 1; i < argc; i++)
		l += strlen(argv[i]) + 1;
	s = calloc(l + 1, sizeof (char));
	for (i = 1; i < argc; strcat(s, " "), i++)
		strcat(s, argv[i]);

	vsb = VSB_new_auto();
	AN(vsb);
	vex = vex_New(s, vsb);

	if (vex == NULL) {
		VSB_finish(vsb);
		fprintf(stderr, "Error:\n%s", VSB_data(vsb));
		VSB_delete(vsb);
		free(s);
		exit(1);
	}
	VSB_delete(vsb);

	vex_Free(&vex);
	AZ(vex);
	free(s);

	return (0);
}
