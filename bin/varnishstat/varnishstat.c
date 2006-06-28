/*
 * $Id: varnishlog.c 153 2006-04-25 08:17:43Z phk $
 *
 * Log tailer for Varnish
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <shmlog.h>

static struct shmloghead *loghead;

int
main(int argc, char **argv)
{
	int fd;
	int i;
	struct shmloghead slh;
	struct varnish_stats *VSL_stats;

	fd = open(SHMLOG_FILENAME, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Cannot open %s: %s\n",
		    SHMLOG_FILENAME, strerror(errno));
		exit (1);
	}
	i = read(fd, &slh, sizeof slh);
	if (i != sizeof slh) {
		fprintf(stderr, "Cannot read %s: %s\n",
		    SHMLOG_FILENAME, strerror(errno));
		exit (1);
	}
	if (slh.magic != SHMLOGHEAD_MAGIC) {
		fprintf(stderr, "Wrong magic number in file %s\n",
		    SHMLOG_FILENAME);
		exit (1);
	}

	loghead = mmap(NULL, slh.size + sizeof slh,
	    PROT_READ, MAP_HASSEMAPHORE, fd, 0);
	if (loghead == MAP_FAILED) {
		fprintf(stderr, "Cannot mmap %s: %s\n",
		    SHMLOG_FILENAME, strerror(errno));
		exit (1);
	}
	
	VSL_stats = &loghead->stats;

#define MAC_STAT(n,t,f,d) \
    printf("%12ju  " d "\n", (VSL_stats->n));
#include "stat_field.h"
#undef MAC_STAT

	exit (0);

}
