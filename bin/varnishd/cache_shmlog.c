/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Redpill Linpro AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>

#include "shmlog.h"
#include "cache.h"

static pthread_mutex_t vsl_mtx;

#define LOCKSHM(foo)					\
	do {						\
		if (pthread_mutex_trylock(foo)) {	\
			AZ(pthread_mutex_lock(foo));	\
			VSL_stats->shm_cont++;		\
		}					\
	} while (0);

#define UNLOCKSHM(foo)	AZ(pthread_mutex_unlock(foo))

static void
vsl_wrap(void)
{

	assert(vsl_log_nxt < vsl_log_end);
	vsl_log_start[1] = SLT_ENDMARKER;
	MEMORY_BARRIER();
	*vsl_log_nxt = SLT_WRAPMARKER;
	MEMORY_BARRIER();
	vsl_log_start[0]++;
	vsl_log_nxt = vsl_log_start + 1;
	VSL_stats->shm_cycles++;
}

static void
vsl_hdr(enum shmlogtag tag, unsigned char *p, unsigned len, unsigned id)
{

	assert(vsl_log_nxt + SHMLOG_NEXTTAG + len < vsl_log_end);
	assert(len < 0x10000);
	p[__SHMLOG_LEN_HIGH] = (len >> 8) & 0xff;
	p[__SHMLOG_LEN_LOW] = len & 0xff;
	p[__SHMLOG_ID_HIGH] = (id >> 24) & 0xff;
	p[__SHMLOG_ID_MEDHIGH] = (id >> 16) & 0xff;
	p[__SHMLOG_ID_MEDLOW] = (id >> 8) & 0xff;
	p[__SHMLOG_ID_LOW] = id & 0xff;
	p[SHMLOG_DATA + len] = '\0';
	MEMORY_BARRIER();
	p[SHMLOG_TAG] = tag;
}

static uint8_t *
vsl_get(unsigned len)
{
	uint8_t *p;

	assert(vsl_log_nxt < vsl_log_end);

	/* Wrap if necessary */
	if (vsl_log_nxt + SHMLOG_NEXTTAG + len + 1 >= vsl_log_end) /* XXX: + 1 ?? */
		vsl_wrap();
	p = vsl_log_nxt;

	vsl_log_nxt += SHMLOG_NEXTTAG + len;
	assert(vsl_log_nxt < vsl_log_end);
	*vsl_log_nxt = SLT_ENDMARKER;
	return (p);
}

/*--------------------------------------------------------------------
 * This variant copies a byte-range directly to the log, without
 * taking the detour over sprintf()
 */

static void
VSLR(enum shmlogtag tag, int id, txt t)
{
	unsigned char *p;
	unsigned l, mlen;

	Tcheck(t);
	mlen = params->shm_reclen;

	/* Truncate */
	l = Tlen(t);
	if (l > mlen) {
		l = mlen;
		t.e = t.b + l;
	}

	/* Only hold the lock while we find our space */
	LOCKSHM(&vsl_mtx);
	VSL_stats->shm_writes++;
	VSL_stats->shm_records++;
	p = vsl_get(l);
	UNLOCKSHM(&vsl_mtx);

	memcpy(p + SHMLOG_DATA, t.b, l);
	vsl_hdr(tag, p, l, id);
}

/*--------------------------------------------------------------------*/

void
VSL(enum shmlogtag tag, int id, const char *fmt, ...)
{
	va_list ap;
	unsigned char *p;
	unsigned n, mlen;
	txt t;

	AN(fmt);
	va_start(ap, fmt);
	mlen = params->shm_reclen;

	if (strchr(fmt, '%') == NULL) {
		t.b = TRUST_ME(fmt);
		t.e = strchr(t.b, '\0');
		VSLR(tag, id, t);
	} else {
		LOCKSHM(&vsl_mtx);
		VSL_stats->shm_writes++;
		VSL_stats->shm_records++;
		assert(vsl_log_nxt < vsl_log_end);

		/* Wrap if we cannot fit a full size record */
		if (vsl_log_nxt + SHMLOG_NEXTTAG + mlen + 1 >= vsl_log_end)
			vsl_wrap();

		p = vsl_log_nxt;
		/* +1 for the NUL */
		n = vsnprintf((char *)(p + SHMLOG_DATA), mlen + 1L, fmt, ap);
		if (n > mlen)
			n = mlen;		/* we truncate long fields */

		vsl_log_nxt += SHMLOG_NEXTTAG + n;
		assert(vsl_log_nxt < vsl_log_end);
		*vsl_log_nxt = SLT_ENDMARKER;

		UNLOCKSHM(&vsl_mtx);

		vsl_hdr(tag, p, n, id);
	}
	va_end(ap);
}

/*--------------------------------------------------------------------*/

void
WSL_Flush(struct worker *w, int overflow)
{
	uint8_t *p;
	unsigned l;

	l = pdiff(w->wlb, w->wlp);
	if (l == 0)
		return;
	LOCKSHM(&vsl_mtx);
	VSL_stats->shm_flushes += overflow;
	VSL_stats->shm_writes++;
	VSL_stats->shm_records += w->wlr;
	p = vsl_get(l);
	UNLOCKSHM(&vsl_mtx);

	memcpy(p + 1, w->wlb + 1, l - 1);
	MEMORY_BARRIER();
	p[0] = w->wlb[0];
	w->wlp = w->wlb;
	w->wlr = 0;
}

/*--------------------------------------------------------------------*/

void
WSLR(struct worker *w, enum shmlogtag tag, int id, txt t)
{
	unsigned char *p;
	unsigned l, mlen;

	Tcheck(t);
	mlen = params->shm_reclen;

	/* Truncate */
	l = Tlen(t);
	if (l > mlen) {
		l = mlen;
		t.e = t.b + l;
	}

	assert(w->wlp < w->wle);

	/* Wrap if necessary */
	if (w->wlp + SHMLOG_NEXTTAG + l + 1 >= w->wle)
		WSL_Flush(w, 1);
	p = w->wlp;
	w->wlp += SHMLOG_NEXTTAG + l;
	assert(w->wlp < w->wle);
	memcpy(p + SHMLOG_DATA, t.b, l);
	vsl_hdr(tag, p, l, id);
	w->wlr++;
	if (params->diag_bitmap & 0x10000)
		WSL_Flush(w, 0);
}

/*--------------------------------------------------------------------*/

void
WSL(struct worker *w, enum shmlogtag tag, int id, const char *fmt, ...)
{
	va_list ap;
	unsigned char *p;
	unsigned n, mlen;
	txt t;

	AN(fmt);
	va_start(ap, fmt);
	mlen = params->shm_reclen;

	if (strchr(fmt, '%') == NULL) {
		t.b = TRUST_ME(fmt);
		t.e = strchr(t.b, '\0');
		WSLR(w, tag, id, t);
	} else {
		assert(w->wlp < w->wle);

		/* Wrap if we cannot fit a full size record */
		if (w->wlp + SHMLOG_NEXTTAG + mlen + 1 >= w->wle)
			WSL_Flush(w, 1);

		p = w->wlp;
		/* +1 for the NUL */
		n = vsnprintf((char *)(p + SHMLOG_DATA), mlen + 1L, fmt, ap);
		if (n > mlen)
			n = mlen;	/* we truncate long fields */
		vsl_hdr(tag, p, n, id);
		w->wlp += SHMLOG_NEXTTAG + n;
		assert(w->wlp < w->wle);
		w->wlr++;
	}
	va_end(ap);
	if (params->diag_bitmap & 0x10000)
		WSL_Flush(w, 0);
}

/*--------------------------------------------------------------------*/

void
VSL_Init(void)
{

	AZ(pthread_mutex_init(&vsl_mtx, NULL));
	loghead->starttime = TIM_real();
	loghead->panicstr[0] = '\0';
	memset(VSL_stats, 0, sizeof *VSL_stats);
	loghead->child_pid = getpid();
}
