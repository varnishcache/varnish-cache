#include <stdio.h>

#include "vtest_ext_vinyl.h"

static __attribute__((constructor)) void
vtest_ext_vinyl_init(void) {

	add_cmd("vinyl", cmd_varnish, CMDS_F_NONE);
	add_cmd("vsl_expect", cmd_logexpect, CMDS_F_NONE);
	add_cmd("vsm", cmd_vsm, CMDS_F_NONE);
}
