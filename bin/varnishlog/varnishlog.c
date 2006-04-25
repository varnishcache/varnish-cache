/*
 * $Id$
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

/*
 * It would be simpler to use sparse array initialization and put it
 * directly in tagnames, but -pedantic gets in the way
 */

static struct tagnames {
	enum shmlogtag	tag;
	const char	*name;
} stagnames[] = {
#define SLTM(foo)	{ SLT_##foo, #foo },
#include "shmlog_tags.h"
#undef SLTM
	{ SLT_ENDMARKER, NULL}
};

static const char *tagnames[256];

static struct shmloghead *loghead;
static unsigned char *logstart, *logend;

int
main(int argc, char **argv)
{
	int fd;
	int i;
	unsigned u;
	unsigned startup;
	struct shmloghead slh;
	unsigned char *p;

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
	logstart = (unsigned char *)loghead + loghead->start;
	logend = logstart + loghead->size;

	for (i = 0; stagnames[i].tag != SLT_ENDMARKER; i++)
		tagnames[stagnames[i].tag] = stagnames[i].name;

	startup = 1;
	while (1) {
		p = logstart;
		while (1) {
			if (*p == SLT_WRAPMARKER)
				break;
			while (*p == SLT_ENDMARKER) {
				fflush(stdout);
				sleep(1);
				startup = 0;
			}
			u = (p[2] << 8) | p[3];
			if (!startup) {
				printf("%02x %3d %4d %-12s <",
				    p[0], p[1], u, tagnames[p[0]]);
				if (p[1] > 0)
					fwrite(p + 4, p[1], 1, stdout);
				printf(">\n");
			}
			p += p[1] + 4;
		}
	}
}
