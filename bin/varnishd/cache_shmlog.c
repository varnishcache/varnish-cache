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
	assert(e != NULL);

	/* Truncate */
	l = e - b;
	if (l > 255) {
		l = 255;
		e = b + l;
	}

	AZ(pthread_mutex_lock(&vsl_mutex));
	assert(loghead->ptr < loghead->size);

	/* Wrap if necessary */
	if (loghead->ptr + 5 + l > loghead->size)
		vsl_wrap();
	p = logstart + loghead->ptr;
	p[1] = l;
	p[2] = id >> 8;
	p[3] = id & 0xff;
	memcpy(p + 4, b, l);
	p[4 + l] = SLT_ENDMARKER;
	p[0] = tag;

	loghead->ptr += 4 + l;
	assert(loghead->ptr < loghead->size);
	AZ(pthread_mutex_unlock(&vsl_mutex));
}


void
VSL(enum shmlogtag tag, unsigned id, const char *fmt, ...)
{
	va_list ap;
	unsigned char *p;
	unsigned m, n;

	va_start(ap, fmt);

	AZ(pthread_mutex_lock(&vsl_mutex));
	assert(loghead->ptr < loghead->size);

	/*
	 * Wrap early if we approach the end 
	 * 32 is arbitraryly larger than minimum of 5.
	 */
	if (loghead->ptr + 32 > loghead->size) 
		vsl_wrap();

	p = logstart + loghead->ptr;
	n = 0;
	if (fmt != NULL) {
		while (1) {
			/* We need 4 four header + 1 for ENDMARKER */
			m = loghead->size - (loghead->ptr + 5);
			if (m > 256)
				m = 256;
			n = vsnprintf((char *)p + 4, m, fmt, ap);
			if (n >= 255)
				n = 255; 	/* we truncate long fields */
			if (n < m)
				break;
			vsl_wrap();
			continue;	/* Try again */
		}
	}
	p[1] = n;
	p[2] = id >> 8;
	p[3] = id & 0xff;
	p[4 + n] = SLT_ENDMARKER;
	p[0] = tag;

	loghead->ptr += 4 + n;
	assert(loghead->ptr < loghead->size);
	
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

	unlink(fn);
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
	}
	heritage.vsl_size = slh.size + slh.start;

	/*
	 * Call VSL_Init so that we get a VSL_stats pointer in the
	 * management process as well.
	 */
	VSL_Init();
	memset(VSL_stats, 0, sizeof *VSL_stats);
}

