/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <pthread.h>

#include "libvarnish.h"
#include "shmlog.h"

#include "heritage.h"

struct varnish_stats *VSL_stats;

static struct shmloghead *loghead;
static unsigned char *logstart, *logend;
static pthread_mutex_t vsl_mutex;

/*
 * This variant copies a byte-range directly to the log, without
 * taking the detour over sprintf()
 */
 
void
VSLR(enum shmlogtag tag, unsigned id, const char *b, const char *e)
{
	unsigned char *p, *q;

	assert(b != NULL);
	if (e == NULL)
		e = strchr(b, '\0');
	assert(e != NULL);

	/* Truncate */
	if (e - b > 255)
		e = b + 255;

	AZ(pthread_mutex_lock(&vsl_mutex));
	q = NULL;
	p = logstart + loghead->ptr;
	assert(p < logend);

	/* Wrap if necessary */
	if (p + 4 + (e - b) > logend) {
		q = p;
		p = logstart;
		*p = SLT_ENDMARKER;
	}
	p[1] = e - b;
	p[2] = id >> 8;
	p[3] = id & 0xff;
	memcpy(p + 4, b, e - b);
	p[0] = tag;

	if (q != NULL)
		*q = SLT_WRAPMARKER;

	loghead->ptr = (p + 4 + (e - b)) - logstart;
	
	AZ(pthread_mutex_unlock(&vsl_mutex));
}


void
VSL(enum shmlogtag tag, unsigned id, const char *fmt, ...)
{
	va_list ap;
	unsigned char *p, *q;
	unsigned m, n;

	va_start(ap, fmt);

	AZ(pthread_mutex_lock(&vsl_mutex));
	q = NULL;
	p = logstart + loghead->ptr;
	assert(p < logend);

	/*
	 * Wrap early if we approach the end 
	 * 32 is arbitraryly larger than minimum of 4.
	 */
	if (p + 32 > logend) {
		q = p;
		p = logstart;
		*p = SLT_ENDMARKER;
	}
	n = 0;
	if (fmt != NULL) {
		while (1) {
			m = logend - (p + 4);
			if (m > 256)
				m = 256;
			n = vsnprintf((char *)p + 4, m, fmt, ap);
			if (n >= 255)
				n = 255; 	/* we truncate long fields */
			if (n < m)
				break;
			/* wraparound */
			assert(q == NULL);
			q = p;
			p = logstart;
			*p = SLT_ENDMARKER;
			continue;	/* Try again */
		}
	}
	p[1] = n;
	p[2] = id >> 8;
	p[3] = id & 0xff;
	p[0] = tag;

	if (q != NULL)
		*q = SLT_WRAPMARKER;

	loghead->ptr = (p + 4 + n) - logstart;
	
	AZ(pthread_mutex_unlock(&vsl_mutex));

	va_end(ap);
}

void
VSL_Init(void)
{

	loghead = mmap(NULL, heritage.vsl_size,
	    PROT_READ|PROT_WRITE,
	    MAP_HASSEMAPHORE | MAP_NOSYNC | MAP_SHARED,
	    heritage.vsl_fd, 0);
	assert(loghead != MAP_FAILED);

	/* XXX check sanity of loghead */
	logstart = (unsigned char *)loghead + loghead->start;
	logend = logstart + loghead->size;
	VSL_stats = &loghead->stats;
	AZ(pthread_mutex_init(&vsl_mutex, NULL));
}

/*--------------------------------------------------------------------*/

void
VSL_MgtInit(const char *fn, unsigned size)
{
	struct shmloghead slh;
	int i;

	heritage.vsl_fd = open(fn, O_RDWR | O_CREAT, 0600);
	if (heritage.vsl_fd < 0) {
		fprintf(stderr, "Could not open %s: %s\n",
		    fn, strerror(errno));
		exit (1);
	}
	i = read(heritage.vsl_fd, &slh, sizeof slh);
	if (i != sizeof slh || slh.magic != SHMLOGHEAD_MAGIC) {
		/* XXX more checks */

		slh.magic = SHMLOGHEAD_MAGIC;
		slh.size = size;
		slh.ptr = 0;
		slh.start = sizeof slh;
		AZ(lseek(heritage.vsl_fd, 0, SEEK_SET));
		i = write(heritage.vsl_fd, &slh, sizeof slh);
		assert(i == sizeof slh);
		AZ(ftruncate(heritage.vsl_fd, sizeof slh + size));
		heritage.vsl_size = slh.size + slh.start;
	} else {
		heritage.vsl_size = slh.size + slh.start;
	}

	/*
	 * Call VSL_Init so that we get a VSL_stats pointer in the
	 * management process as well.
	 */
	VSL_Init();
	memset(VSL_stats, 0, sizeof *VSL_stats);
}

