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

struct VSL_data {
	struct shmloghead	*head;
	unsigned char		*logstart;
	unsigned char		*logend;
	unsigned char		*ptr;
	char			*r_arg;
	FILE			*fi;
	unsigned char		rbuf[4 + 255 + 1];
};

#ifndef MAP_HASSEMAPHORE
#define MAP_HASSEMAPHORE 0 /* XXX Linux */
#endif

static int vsl_fd;
static struct shmloghead *vsl_lh;

/*--------------------------------------------------------------------*/

static int
vsl_shmem_map(void)
{
	int i;
	struct shmloghead slh;

	if (vsl_lh != NULL)
		return (0);

	vsl_fd = open(SHMLOG_FILENAME, O_RDONLY);
	if (vsl_fd < 0) {
		fprintf(stderr, "Cannot open %s: %s\n",
		    SHMLOG_FILENAME, strerror(errno));
		return (1);
	}
	i = read(vsl_fd, &slh, sizeof slh);
	if (i != sizeof slh) {
		fprintf(stderr, "Cannot read %s: %s\n",
		    SHMLOG_FILENAME, strerror(errno));
		return (1);
	}
	if (slh.magic != SHMLOGHEAD_MAGIC) {
		fprintf(stderr, "Wrong magic number in file %s\n",
		    SHMLOG_FILENAME);
		return (1);
	}

	vsl_lh = mmap(NULL, slh.size + sizeof slh,
	    PROT_READ, MAP_HASSEMAPHORE, vsl_fd, 0);
	if (vsl_lh == MAP_FAILED) {
		fprintf(stderr, "Cannot mmap %s: %s\n",
		    SHMLOG_FILENAME, strerror(errno));
		return (1);
	}
	return (0);
}

/*--------------------------------------------------------------------*/

struct VSL_data *
VSL_New(void)
{
	struct VSL_data *vd;

	vd = calloc(sizeof *vd, 1);
	return (vd);
}

int
VSL_OpenLog(struct VSL_data *vd)
{

	if (vd->r_arg != NULL) {
		if (!strcmp(vd->r_arg, "-"))
			vd->fi = stdin;
		else
			vd->fi = fopen(vd->r_arg, "r");
		if (vd->fi != NULL)
			return (0);
		perror(vd->r_arg);
		return (1);
	}
	if (vsl_shmem_map())
		return (1);

	vd->logstart = (unsigned char *)vsl_lh + vsl_lh->start;
	vd->logend = vd->logstart + vsl_lh->size;
	vd->head = vsl_lh;
	return (0);
}

unsigned char *
VSL_NextLog(struct VSL_data *vd)
{
	unsigned char *p;
	int i;

	if (vd->fi != NULL) {
		i = fread(vd->rbuf, 4, 1, vd->fi);
		if (i != 1)
			return (NULL);
		if (vd->rbuf[1] > 0) {
			i = fread(vd->rbuf + 4, vd->rbuf[1], 1, vd->fi);
			if (i != 1)
				return (NULL);
		}
		return (vd->rbuf);
	}

	p = vd->ptr;
	if (p == NULL)
		p = vd->logstart;
	while (1) {
		if (*p == SLT_WRAPMARKER) {
			p = vd->logstart;
			continue;
		}
		if (*p == SLT_ENDMARKER) {
			vd->ptr = p;
			return (NULL);
		}
		vd->ptr = p + p[1] + 4;
		return (p);
	}
}

int
VSL_Arg(struct VSL_data *vd, int arg, const char *opt)
{
	switch (arg) {
	case 'r':
		vd->r_arg = strdup(opt);
		return (1);
	default:
		return (0);
	}
}

struct varnish_stats *
VSL_OpenStats(void)
{

	if (vsl_shmem_map())
		return (NULL);
	return (&vsl_lh->stats);
}

