/*
 * $Id$
 */

#include <sys/types.h>

#include <stdio.h>

#include "varnish.h"
#include "log.h"
#include "system.h"

void
system_init_ncpu(void)
{
	FILE *cpuinfo;
	char line[256];
	int n;

	sys.ncpu = 0;
	if ((cpuinfo = fopen("/proc/cpuinfo", "r")) == NULL)
		return;
	while (fgets(line, sizeof line, cpuinfo) != NULL) {
		if (sscanf(line, "processor : %d", &n) == 1)
			sys.ncpu++;
	}
	fclose(cpuinfo);
	if (sys.ncpu == 0)
		sys.ncpu = 1;
	log_info("%d cpu(s)", sys.ncpu);
}
