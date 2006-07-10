/*
 * $Id: varnishlog.c 153 2006-04-25 08:17:43Z phk $
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "shmlog.h"
#include "varnishapi.h"

#ifndef MAP_HASSEMAPHORE
#define MAP_HASSEMAPHORE 0 /* XXX Linux */
#endif

static unsigned char *logstart, *logend;

struct shmloghead *
VSL_OpenLog(void)
{
	int fd;
	int i;
	struct shmloghead slh, *lh;

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

	lh = mmap(NULL, slh.size + sizeof slh,
	    PROT_READ, MAP_HASSEMAPHORE, fd, 0);
	if (lh == MAP_FAILED) {
		fprintf(stderr, "Cannot mmap %s: %s\n",
		    SHMLOG_FILENAME, strerror(errno));
		exit (1);
	}

	logstart = (unsigned char *)lh + lh->start;
	logend = logstart + lh->size;

	return (lh);
}

unsigned char *
VSL_NextLog(struct shmloghead *lh __unused, unsigned char **pp)
{
	unsigned char *p;

	p = *pp;
	if (p == NULL)
		p = logstart;
	while (1) {
		if (*p == SLT_WRAPMARKER) {
			p = logstart;
			continue;
		}
		if (*p == SLT_ENDMARKER) {
			*pp = p;
			return (NULL);
		}
		*pp = p + p[1] + 4;
		return (p);
	}
}
