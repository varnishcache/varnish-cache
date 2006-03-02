/*
 * $Id$
 */

#include "config.h"

#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <varnishapi.h>

/**
 * Open and lock a file.
 */
int
vut_open_lock(const char *name, int mode, int lockop, int perm)
{
	struct stat sb, fsb;
	int fd, serr;

	for (;;) {
		if ((fd = open(name, mode, perm)) == -1)
			/* not much we can do about that */
			return (-1);
		while (flock(fd, lockop) == -1) {
			if (errno != EINTR) {
				serr = errno;
				close(fd);
				errno = serr;
				return (-1);
			}
		}
		if (stat(name, &sb) == -1) {
			serr = errno;
			close(fd);
			errno = serr;

			if (errno == ENOENT && (mode & O_CREAT))
				/* file was deleted from under our nose */
				continue;
			return (-1);
		}
		if (fstat(fd, &fsb) == -1) {
			/* serious voodoo is going on*/
			serr = errno;
			close(fd);
			errno = serr;
			return (-1);
		}
		if (sb.st_dev == fsb.st_dev && sb.st_ino == fsb.st_ino)
			/* we have the correct file */
			return (fd);
		close(fd);
	}
	/* not reached */
}
