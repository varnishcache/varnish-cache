/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/sysctl.h>

#include "varnish.h"
#include "log.h"
#include "system.h"

void
system_init_ncpu(void)
{
	size_t size;

	size = sizeof sys.ncpu;
	if (sysctlbyname("hw.ncpu", &sys.ncpu, &size, 0, 0) == -1)
		sys.ncpu = 1;
	log_info("%d cpu(s)", sys.ncpu);
}
