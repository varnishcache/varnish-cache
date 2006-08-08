/*
 * $Id$
 */

#ifndef HAVE_SRANDOMDEV

#include <sys/time.h>

#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "compat/srandomdev.h"

void
srandomdev(void)
{
	struct timeval tv;
	unsigned int seed;
	int fd;

	if ((fd = open("/dev/random", O_RDONLY)) >= 0) {
		read(fd, &seed, sizeof seed);
		close(fd);
	} else {
		gettimeofday(&tv, NULL);
		/* NOTE: intentional use of uninitialized variable */
		seed ^= (getpid() << 16) ^ tv.tv_sec ^ tv.tv_usec;
	}
	srandom(seed);
}
#endif
