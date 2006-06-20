/*
 * $Id$
 */

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <sys/mman.h>

#include "shmlog.h"

#include "heritage.h"

static struct shmloghead *loghead;
static unsigned char *logstart, *logend;

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

	/* XXX: Lock */
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
	
	/* XXX: Unlock */
}


void
VSL(enum shmlogtag tag, unsigned id, const char *fmt, ...)
{
	va_list ap;
	unsigned char *p, *q;
	unsigned m, n;

	va_start(ap, fmt);

	/* XXX: Lock */
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
	
	/* XXX: Unlock */

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
}
