/*
 * $Id: storage_malloc.c 170 2006-06-13 07:57:32Z phk $
 *
 * Storage method based on mmap'ed file
 */

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/param.h>
#include <sys/mount.h>

#include "vcl_lang.h"
#include "libvarnish.h"
#include "cache.h"

struct smf_sc {
	char			*filename;
	int			fd;
	uintmax_t		filesize;
};

struct smf {
	struct storage		s;
};

/*--------------------------------------------------------------------*/

static void
smf_calcsize(struct smf_sc *sc, const char *size, int newfile)
{
	uintmax_t l;
	unsigned bs;
	char suff[2];
	int i;
	off_t o;
	struct statfs fsst;
	struct stat st;

	AZ(fstat(sc->fd, &st));
	AZ(fstatfs(sc->fd, &fsst));

	/* We use units of the larger of filesystem blocksize and pagesize */
	bs = getpagesize();
	if (bs < fsst.f_bsize)
		bs = fsst.f_bsize;

	assert(S_ISREG(st.st_mode));

	i = sscanf(size, "%ju%1s", &l, suff); /* can return -1, 0, 1 or 2 */

	if (i == 0) {
		fprintf(stderr,
		    "Error: (-sfile) size \"%s\" not understood\n", size);
		exit (2);
	}

	if (i >= 1 && l == 0) {
		fprintf(stderr,
		    "Error: (-sfile) zero size not permitted\n");
		exit (2);
	}

	if (i == -1 && !newfile) /* Use the existing size of the file */
		l = st.st_size;

	/* We must have at least one block */
	if (l < bs) {	
		if (i == -1) {
			fprintf(stderr,
			    "Info: (-sfile) default to 80%% size\n");
			l = 80;
			suff[0] = '%';
			i = 2;
		}

		if (i == 2) {
			if (suff[0] == 'k' || suff[0] == 'K')
				l *= 1024UL;
			else if (suff[0] == 'm' || suff[0] == 'M')
				l *= 1024UL * 1024UL;
			else if (suff[0] == 'g' || suff[0] == 'G')
				l *= 1024UL * 1024UL * 1024UL;
			else if (suff[0] == 't' || suff[0] == 'T')
				l *= (uintmax_t)(1024UL * 1024UL) *
				    (uintmax_t)(1024UL * 1024UL);
			else if (suff[0] == '%') {
				l *= fsst.f_bsize * fsst.f_bavail;
				l /= 100;
			}
		}

		o = l;
		if (o != l || o < 0) {
			fprintf(stderr,
			    "Warning: size reduced to system limit (off_t)\n");
			do {
				l >>= 1;
				o = l;
			} while (o != l || o < 0);
		}

		if (l < st.st_size) {
			AZ(ftruncate(sc->fd, l));
		} else if (l - st.st_size > fsst.f_bsize * fsst.f_bavail) {
			fprintf(stderr,
			    "Warning: size larger than filesystem free space,"
			    " reduced to 80%% of free space.\n");
			l = (fsst.f_bsize * fsst.f_bavail * 80) / 100;
		}
	}

	/* round down to of filesystem blocksize or pagesize */
	l -= (l % bs);

	assert(l >= bs);

	printf("file %s size %ju bytes (%ju fs-blocks, %ju pages)\n",
	    sc->filename, l, l / fsst.f_bsize, l / getpagesize());

	sc->filesize = l;
}

static void
smf_initfile(struct smf_sc *sc, const char *size, int newfile)
{
	smf_calcsize(sc, size, newfile);
}

static void
smf_init(struct stevedore *parent, const char *spec)
{
	char *size;
	char *p, *q;
	struct stat st;
	struct smf_sc *sc;

	sc = calloc(sizeof *sc, 1);
	assert(sc != NULL);

	/* If no size specified, use 50% of filesystem free space */
	if (spec == NULL || *spec == '\0')
		spec = "/tmp,50%";

	if (strchr(spec, ',') == NULL)
		asprintf(&p, "%s,", spec);
	else
		p = strdup(spec);
	assert(p != NULL);
	size = strchr(p, ',');
	assert(size != NULL);

	*size++ = '\0';

	/* try to create a new file of this name */
	sc->fd = open(p, O_RDWR | O_CREAT | O_EXCL, 0600);
	if (sc->fd >= 0) {
		sc->filename = p;
		smf_initfile(sc, size, 1);
		return;
	}

	/* it must exist then */
	if (stat(p, &st)) {
		fprintf(stderr,
		    "Error: (-sfile) \"%s\" "
		    "does not exist and could not be created\n", p);
		exit (2);
	}

	/* and it should be a file or directory */
	if (!(S_ISREG(st.st_mode) || S_ISDIR(st.st_mode))) {
		fprintf(stderr,
		    "Error: (-sfile) \"%s\" "
		    "is neither file nor directory\n", p);
		exit (2);
	}

	if (S_ISREG(st.st_mode)) {
		sc->fd = open(p, O_RDWR);
		if (sc->fd < 0) {
			fprintf(stderr,
			    "Error: (-sfile) \"%s\" "
			    "could not open (%s)\n", p, strerror(errno));
			exit (2);
		}
		AZ(fstat(sc->fd, &st));
		if (!S_ISREG(st.st_mode)) {
			fprintf(stderr,
			    "Error: (-sfile) \"%s\" "
			    "was not a file after opening\n", p);
			exit (2);
		}
		sc->filename = p;
		smf_initfile(sc, size, 0);
		return;
	}

	asprintf(&q, "%s/varnish.XXXXXX", p);
	assert(q != NULL);
	sc->fd = mkstemp(q);
	if (sc->fd < 0) {
		fprintf(stderr,
		    "Error: (-sfile) \"%s\" "
		    "mkstemp(%s) failed (%s)\n", p, q, strerror(errno));
		exit (2);
	}
	AZ(unlink(q));
	asprintf(&sc->filename, "%s (unlinked)", q);
	assert(sc->filename != NULL);
	free(q);
	smf_initfile(sc, size, 1);
}

static struct storage *
smf_alloc(struct stevedore *st __unused, unsigned size)
{
	struct smf *smf;

	smf = calloc(sizeof *smf, 1);
	assert(smf != NULL);
	smf->s.priv = smf;
	smf->s.ptr = malloc(size);
	assert(smf->s.ptr != NULL);
	smf->s.len = size;
	return (&smf->s);
}

static void
smf_free(struct storage *s)
{
	struct smf *smf;

	smf = s->priv;
	free(smf->s.ptr);
	free(smf);
}

struct stevedore smf_stevedore = {
	"file",
	smf_init,		/* init */
	NULL,			/* open */
	smf_alloc,
	smf_free
};
