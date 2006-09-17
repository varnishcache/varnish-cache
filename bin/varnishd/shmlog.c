/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdarg.h>
#include <sys/mman.h>

#include "shmlog.h"
#include "cache.h"

#include "heritage.h"

#define LOCKSHM(foo)					\
	do {						\
		if (pthread_mutex_trylock(foo)) {	\
			AZ(pthread_mutex_lock(foo));	\
			VSL_stats->shm_cont++;		\
		}					\
	} while (0);
		
#define UNLOCKSHM(foo)	AZ(pthread_mutex_unlock(foo))

#ifndef MAP_HASSEMAPHORE
#define MAP_HASSEMAPHORE 0 /* XXX Linux */
#endif

#ifndef MAP_NOSYNC
#define MAP_NOSYNC 0 /* XXX Linux */
#endif

struct varnish_stats *VSL_stats;
static struct shmloghead *loghead;
static unsigned char *logstart;
static MTX vsl_mtx;

/*
 * This variant copies a byte-range directly to the log, without
 * taking the detour over sprintf()
 */

static void
vsl_wrap(void)
{

	*logstart = SLT_ENDMARKER;
	logstart[loghead->ptr] = SLT_WRAPMARKER;
	loghead->ptr = 0;
}
 
void
VSLR(enum shmlogtag tag, unsigned id, const char *b, const char *e)
{
	unsigned char *p;
	unsigned l;

	assert(b != NULL);
	if (e == NULL)
		e = strchr(b, '\0');

	/* Truncate */
	l = e - b;
	if (l > 255) {
		l = 255;
		e = b + l;
	}

	/* Only hold the lock while we find our space */
	LOCKSHM(&vsl_mtx);
	assert(loghead->ptr < loghead->size);

	/* Wrap if necessary */
	if (loghead->ptr + 5 + l + 1 > loghead->size)
		vsl_wrap();
	p = logstart + loghead->ptr;
	loghead->ptr += 5 + l;
	p[5 + l] = SLT_ENDMARKER;
	assert(loghead->ptr < loghead->size);
	UNLOCKSHM(&vsl_mtx);

	p[1] = l & 0xff;
	p[2] = (id >> 8) & 0xff;
	p[3] = id & 0xff;
	memcpy(p + 4, b, l);
	p[4 + l] = '\0';
	/* XXX: memory barrier */
	p[0] = tag;
}


void
VSL(enum shmlogtag tag, unsigned id, const char *fmt, ...)
{
	va_list ap;
	unsigned char *p;
	unsigned n;

	va_start(ap, fmt);

	p = strchr(fmt, '%');
	if (p == NULL) {
		VSLR(tag, id, fmt, NULL);
		return;
	}

	LOCKSHM(&vsl_mtx);
	assert(loghead->ptr < loghead->size);

	/* Wrap if we cannot fit a full size record */
	if (loghead->ptr + 5 + 255 + 1 > loghead->size) 
		vsl_wrap();

	p = logstart + loghead->ptr;
	n = 0;
	if (fmt != NULL) {
		n = vsnprintf((char *)(p + 4), 256, fmt, ap);
		if (n > 255)
			n = 255; 	/* we truncate long fields */
	}
	p[1] = n & 0xff;
	p[2] = (id >> 8) & 0xff;
	p[3] = id & 0xff;
	p[4 + n] = '\0';;
	p[5 + n] = SLT_ENDMARKER;
	p[0] = tag;

	loghead->ptr += 5 + n;
	assert(loghead->ptr < loghead->size);
	
	UNLOCKSHM(&vsl_mtx);

	va_end(ap);
}

void
VSL_Init(void)
{

	assert(loghead->magic == SHMLOGHEAD_MAGIC);
	assert(loghead->hdrsize == sizeof *loghead);
	/* XXX more check sanity of loghead  ? */
	logstart = (unsigned char *)loghead + loghead->start;
	MTX_INIT(&vsl_mtx);
	loghead->starttime = time(NULL);
	memset(VSL_stats, 0, sizeof *VSL_stats);
}

/*--------------------------------------------------------------------*/

static int
vsl_goodold(int fd)
{
	struct shmloghead slh;
	int i;

	memset(&slh, 0, sizeof slh);	/* XXX: for flexelint */
	i = read(fd, &slh, sizeof slh);
	if (i != sizeof slh)
		return (0);
	if (slh.magic != SHMLOGHEAD_MAGIC)
		return (0);
	if (slh.hdrsize != sizeof slh)
		return (0);
	if (slh.start != sizeof slh + sizeof *params)
		return (0);
	/* XXX more checks */
	heritage.vsl_size = slh.size + slh.start;
	return (1);
}

static void
vsl_buildnew(const char *fn, unsigned size)
{
	struct shmloghead slh;
	int i;

	(void)unlink(fn);
	heritage.vsl_fd = open(fn, O_RDWR | O_CREAT, 0644);
	if (heritage.vsl_fd < 0) {
		fprintf(stderr, "Could not open %s: %s\n",
		    fn, strerror(errno));
		exit (1);
	}

	memset(&slh, 0, sizeof slh);
	slh.magic = SHMLOGHEAD_MAGIC;
	slh.hdrsize = sizeof slh;
	slh.size = size;
	slh.ptr = 0;
	slh.start = sizeof slh + sizeof *params;
	i = write(heritage.vsl_fd, &slh, sizeof slh);
	xxxassert(i == sizeof slh);
	heritage.vsl_size = slh.start + size;
	AZ(ftruncate(heritage.vsl_fd, (off_t)heritage.vsl_size));
}

void
VSL_MgtInit(const char *fn, unsigned size)
{
	int i;
	struct params *pp;

	i = open(fn, O_RDWR, 0644);
	if (i >= 0 && vsl_goodold(i)) {
		fprintf(stderr, "Using old SHMFILE\n");
		heritage.vsl_fd = i;
	} else {
		fprintf(stderr, "Creating new SHMFILE\n");
		(void)close(i);
		vsl_buildnew(fn, size);
	}

	loghead = mmap(NULL, heritage.vsl_size,
	    PROT_READ|PROT_WRITE,
	    MAP_HASSEMAPHORE | MAP_NOSYNC | MAP_SHARED,
	    heritage.vsl_fd, 0);
	xxxassert(loghead != MAP_FAILED);
	VSL_stats = &loghead->stats;
	pp = (void *)(loghead + 1);
	memcpy(pp, params, sizeof *pp);
	params = pp;
}
