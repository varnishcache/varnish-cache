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
#include <sys/mman.h>

#include "vcl_lang.h"
#include "libvarnish.h"
#include "cache.h"

#define MINPAGES		128

/*--------------------------------------------------------------------*/

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

	if (l < MINPAGES * getpagesize()) {
		fprintf(stderr,
		    "Error: size too small, at least %ju needed\n",
		    (uintmax_t)MINPAGES * getpagesize());
		exit (2);
	}

	printf("file %s size %ju bytes (%ju fs-blocks, %ju pages)\n",
	    sc->filename, l, l / fsst.f_bsize, l / getpagesize());

	sc->filesize = l;
}

static void
smf_initfile(struct smf_sc *sc, const char *size, int newfile)
{
	smf_calcsize(sc, size, newfile);

	AZ(ftruncate(sc->fd, sc->filesize));

	/* XXX: force block allocation here or in open ? */
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
	parent->priv = sc;

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

/*--------------------------------------------------------------------*/

/*
 * XXX: This may be too aggressive and soak up too much address room.
 * XXX: On the other hand, the user, directly or implicitly asked us to 
 * XXX: use this much storage, so we should make a decent effort.
 * XXX: worst case (I think), malloc will fail.
 */

static void
smf_open_chunk(struct smf_sc *sc, off_t sz, off_t off, off_t *fail, off_t *sum)
{
	void *p;
	off_t h;

	if (*fail < getpagesize() * MINPAGES)
		return;

	if (sz < *fail && sz < SIZE_T_MAX) {
		p = mmap(NULL, sz, PROT_READ|PROT_WRITE,
		    MAP_NOCORE | MAP_NOSYNC | MAP_PRIVATE, sc->fd, off);
		if (p != MAP_FAILED) {
			printf("%12p...%12p  %12jx...%12jx %12jx\n",
			    p, (u_char *)p + sz,
			    off, off + sz, sz);
			(*sum) += sz;
			return;
		}
	}

	if (sz < *fail)
		*fail = sz;

	h = sz / 2;
	if (h > SIZE_T_MAX)
		h = SIZE_T_MAX;
	h -= (h % getpagesize());

	smf_open_chunk(sc, h, off, fail, sum);
	smf_open_chunk(sc, sz - h, off + h, fail, sum);
}

static void
smf_open(struct stevedore *st)
{
	struct smf_sc *sc;
	off_t fail = SIZE_T_MAX;
	off_t sum = 0;

	sc = st->priv;

	smf_open_chunk(sc, sc->filesize, 0, &fail, &sum);
	printf("managed to mmap %ju bytes of %ju\n",
	    (uintmax_t)sum, sc->filesize);

	/* XXX */
	if (sum < MINPAGES * getpagesize())
		exit (2);
}

/*--------------------------------------------------------------------*/

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
	smf_init,
	smf_open,
	smf_alloc,
	smf_free
};
