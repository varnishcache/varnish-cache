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
#include <queue.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/mman.h>
#include <sys/socket.h>

#include "libvarnish.h"
#include "cache.h"

#define MINPAGES		128

/*--------------------------------------------------------------------*/

struct smf {
	struct storage		s;
	struct smf_sc		*sc;

	int			alloc;
	time_t			age;

	off_t			size;
	off_t			offset;
	unsigned char		*ptr;

	TAILQ_ENTRY(smf)	order;
	TAILQ_ENTRY(smf)	status;
};

TAILQ_HEAD(smfhead, smf);

struct smf_sc {
	char			*filename;
	int			fd;
	unsigned		pagesize;
	uintmax_t		filesize;
	struct smfhead		order;
	struct smfhead		free;
	struct smfhead		used;
	pthread_mutex_t		mtx;
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
	bs = sc->pagesize;
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

	if (l < MINPAGES * sc->pagesize) {
		fprintf(stderr,
		    "Error: size too small, at least %ju needed\n",
		    (uintmax_t)MINPAGES * sc->pagesize);
		exit (2);
	}

	printf("file %s size %ju bytes (%ju fs-blocks, %ju pages)\n",
	    sc->filename, l, l / fsst.f_bsize, l / sc->pagesize);

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
	TAILQ_INIT(&sc->order);
	TAILQ_INIT(&sc->free);
	TAILQ_INIT(&sc->used);
	sc->pagesize = getpagesize();

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

/*--------------------------------------------------------------------
 * Allocate a range from the first free range that is large enough.
 */

static struct smf *
alloc_smf(struct smf_sc *sc, size_t bytes)
{
	struct smf *sp, *sp2;

	TAILQ_FOREACH(sp, &sc->free, status)
		if (sp->size >= bytes)
			break;
	if (sp == NULL)
		return (sp);

	if (sp->size == bytes) {
		TAILQ_REMOVE(&sc->free, sp, status);
		sp->alloc = 1;
		TAILQ_INSERT_TAIL(&sc->used, sp, status);
		return (sp);
	}

	/* Split from front */
	sp2 = malloc(sizeof *sp2);
	assert(sp2 != NULL);
	*sp2 = *sp;

	sp->offset += bytes;
	sp->ptr += bytes;
	sp->size -= bytes;

	sp2->size = bytes;
	sp2->alloc = 1;
	TAILQ_INSERT_BEFORE(sp, sp2, order);
	TAILQ_INSERT_TAIL(&sc->used, sp2, status);
	return (sp2);
}

/*--------------------------------------------------------------------
 * Free a range.  Attemt merge forward and backward, then sort into 
 * free list according to age.
 */

static void
free_smf(struct smf *sp)
{
	struct smf *sp2;
	struct smf_sc *sc = sp->sc;

	TAILQ_REMOVE(&sc->used, sp, status);
	sp->alloc = 0;

	sp2 = TAILQ_NEXT(sp, order);
	if (sp2 != NULL &&
	    sp2->alloc == 0 &&
	    (sp2->ptr == sp->ptr + sp->size) &&
	    (sp2->offset == sp->offset + sp->size)) {
		sp->size += sp2->size;
		TAILQ_REMOVE(&sc->order, sp2, order);
		TAILQ_REMOVE(&sc->free, sp2, status);
		free(sp2);
	}

	sp2 = TAILQ_PREV(sp, smfhead, order);
	if (sp2 != NULL &&
	    sp2->alloc == 0 &&
	    (sp->ptr == sp2->ptr + sp2->size) &&
	    (sp->offset == sp2->offset + sp2->size)) {
		sp2->size += sp->size;
		sp2->age = sp->age;
		TAILQ_REMOVE(&sc->order, sp, order);
		free(sp);
		TAILQ_REMOVE(&sc->free, sp2, status);
		sp = sp2;
	}

	TAILQ_FOREACH(sp2, &sc->free, status) {
		if (sp->age > sp2->age ||
		    (sp->age == sp2->age && sp->offset < sp2->offset)) {
			TAILQ_INSERT_BEFORE(sp2, sp, status);
			break;
		}
	}
	if (sp2 == NULL)
		TAILQ_INSERT_TAIL(&sc->free, sp, status);
}

/*--------------------------------------------------------------------
 * Trim the tail of a range.
 */

static void
trim_smf(struct smf *sp, size_t bytes)
{
	struct smf *sp2;
	struct smf_sc *sc = sp->sc;

	assert(bytes > 0);
	sp2 = malloc(sizeof *sp2);
	assert(sp2 != NULL);
	*sp2 = *sp;

	sp2->size -= bytes;
	sp->size = bytes;
	sp2->ptr += bytes;
	sp2->offset += bytes;
	TAILQ_INSERT_TAIL(&sc->used, sp2, status);
	TAILQ_INSERT_AFTER(&sc->order, sp, sp2, order);
	free_smf(sp2);
}

/*--------------------------------------------------------------------
 * Insert a newly created range as busy, then free it to do any collapses
 */

static void
new_smf(struct smf_sc *sc, unsigned char *ptr, off_t off, size_t len)
{
	struct smf *sp, *sp2;

	sp = calloc(sizeof *sp, 1);
	assert(sp != NULL);

	sp->sc = sc;

	sp->size = len;
	sp->ptr = ptr;
	sp->offset = off;

	sp->alloc = 1;

	TAILQ_FOREACH(sp2, &sc->order, order) {
		if (sp->ptr < sp2->ptr) {
			TAILQ_INSERT_BEFORE(sp2, sp, order);
			break;
		}
	}
	if (sp2 == NULL)
		TAILQ_INSERT_TAIL(&sc->order, sp, order);

	TAILQ_INSERT_HEAD(&sc->used, sp, status);

	free_smf(sp);
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

	assert(sz != 0);

	if (*fail < (uintmax_t)sc->pagesize * MINPAGES)
		return;

	if (sz > 0 && sz < *fail && sz < SIZE_T_MAX) {
		p = mmap(NULL, sz, PROT_READ|PROT_WRITE,
		    MAP_NOCORE | MAP_NOSYNC | MAP_SHARED, sc->fd, off);
		if (p != MAP_FAILED) {
			(*sum) += sz;
			new_smf(sc, p, off, sz);
			return;
		}
	}

	if (sz < *fail)
		*fail = sz;

	h = sz / 2;
	if (h > SIZE_T_MAX)
		h = SIZE_T_MAX;
	h -= (h % sc->pagesize);

	smf_open_chunk(sc, h, off, fail, sum);
	smf_open_chunk(sc, sz - h, off + h, fail, sum);
}

static void
smf_open(struct stevedore *st)
{
	struct smf_sc *sc;
	off_t fail = 1 << 30;	/* XXX: where is OFF_T_MAX ? */
	off_t sum = 0;

	sc = st->priv;

	smf_open_chunk(sc, sc->filesize, 0, &fail, &sum);
	printf("managed to mmap %ju bytes of %ju\n",
	    (uintmax_t)sum, sc->filesize);

	/* XXX */
	if (sum < MINPAGES * getpagesize())
		exit (2);
	AZ(pthread_mutex_init(&sc->mtx, NULL));
}

/*--------------------------------------------------------------------*/

static struct storage *
smf_alloc(struct stevedore *st, size_t size)
{
	struct smf *smf;
	struct smf_sc *sc = st->priv;

	size += (sc->pagesize - 1);
	size &= ~(sc->pagesize - 1);
	AZ(pthread_mutex_lock(&sc->mtx));
	smf = alloc_smf(sc, size);
	assert(smf->size == size);
	AZ(pthread_mutex_unlock(&sc->mtx));
	assert(smf != NULL);
	smf->s.space = size;
	smf->s.priv = smf;
	smf->s.ptr = smf->ptr;
	smf->s.len = 0;
	smf->s.stevedore = st;
	return (&smf->s);
}

/*--------------------------------------------------------------------*/

static void
smf_trim(struct storage *s, size_t size)
{
	struct smf *smf;
	struct smf_sc *sc;

	assert(size <= s->space);
	assert(size > 0);	/* XXX: seen */
	smf = (struct smf *)(s->priv);
	assert(size <= smf->size);
	sc = smf->sc;
	size += (sc->pagesize - 1);
	size &= ~(sc->pagesize - 1);
	if (smf->size > size) {
		AZ(pthread_mutex_lock(&sc->mtx));
		trim_smf(smf, size);
		assert(smf->size == size);
		AZ(pthread_mutex_unlock(&sc->mtx));
		smf->s.space = size;
	}
}

/*--------------------------------------------------------------------*/

static void
smf_free(struct storage *s)
{
	struct smf *smf;
	struct smf_sc *sc;

	smf = (struct smf *)(s->priv);
	sc = smf->sc;
	AZ(pthread_mutex_lock(&sc->mtx));
	free_smf(smf);
	AZ(pthread_mutex_unlock(&sc->mtx));
}

/*--------------------------------------------------------------------*/

static void
smf_send(struct storage *st, struct sess *sp, struct iovec *iov, int niov, size_t liov)
{
	struct smf *smf;
	int i;
	off_t sent;
	struct sf_hdtr sfh;

	smf = st->priv;

	memset(&sfh, 0, sizeof sfh);
	sfh.headers = iov;
	sfh.hdr_cnt = niov;
	i = sendfile(smf->sc->fd,
	    sp->fd,
	    smf->offset,
	    st->len, &sfh, &sent, 0);
	if (sent == st->len + liov)
		return;
	printf("sent i=%d sent=%ju size=%ju liov=%ju errno=%d\n",
	    i, (uintmax_t)sent, (uintmax_t)st->len, liov, errno);
	vca_close_session(sp, "remote closed");
}

/*--------------------------------------------------------------------*/

struct stevedore smf_stevedore = {
	"file",
	smf_init,
	smf_open,
	smf_alloc,
	smf_trim,
	smf_free,
	smf_send
};

#ifdef INCLUDE_TEST_DRIVER

void vca_flush(struct sess *sp) {}
void vca_close_session(struct sess *sp, const char *why) {}

#define N	100
#define M	(128*1024)

struct storage *s[N];

static void
dumpit(void)
{
	struct smf_sc *sc = smf_stevedore.priv;
	struct smf *s;

	return (0);
	printf("----------------\n");
	printf("Order:\n");
	TAILQ_FOREACH(s, &sc->order, order) {
		printf("%10p %12ju %12ju %12ju\n",
		    s, s->offset, s->size, s->offset + s->size);
	}
	printf("Used:\n");
	TAILQ_FOREACH(s, &sc->used, status) {
		printf("%10p %12ju %12ju %12ju\n",
		    s, s->offset, s->size, s->offset + s->size);
	}
	printf("Free:\n");
	TAILQ_FOREACH(s, &sc->free, status) {
		printf("%10p %12ju %12ju %12ju\n",
		    s, s->offset, s->size, s->offset + s->size);
	}
	printf("================\n");
}

int
main(int argc, char **argv)
{
	int i, j;

	setbuf(stdout, NULL);
	smf_init(&smf_stevedore, "");
	smf_open(&smf_stevedore);
	while (1) {
		dumpit();
		i = random() % N;
		do
			j = random() % M;
		while (j == 0);
		if (s[i] == NULL) {
			s[i] = smf_alloc(&smf_stevedore, j);
			printf("A %10p %12d\n", s[i], j);
		} else if (j < s[i]->space) {
			smf_trim(s[i], j);
			printf("T %10p %12d\n", s[i], j);
		} else {
			smf_free(s[i]);
			printf("D %10p\n", s[i]);
			s[i] = NULL;
		}
	}
}

#endif /* INCLUDE_TEST_DRIVER */
