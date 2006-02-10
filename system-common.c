/*
 * $Id$
 */

#include <sys/types.h>

#include <unistd.h>

#include "varnish.h"
#include "system.h"

system_t sys;

/*
 * gather system information at startup
 */
void
system_init(void)
{
	sys.pid = getpid();
	system_init_ncpu();
}

/*
 * fork() wrapper, updates sys.pid
 */
pid_t
system_fork(void)
{
	pid_t pid;

	if ((pid = fork()) == 0)
		sys.pid = getpid();
	return (pid);
}
