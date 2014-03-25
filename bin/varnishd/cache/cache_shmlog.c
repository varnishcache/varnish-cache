/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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

#include <stdio.h>
#include <stdlib.h>

#include "cache.h"
#include "common/heritage.h"

#include "cache_backend.h"	// For wrk->vbc

#include "vmb.h"
#include "vtim.h"

/* These cannot be struct lock, which depends on vsm/vsl working */
static pthread_mutex_t vsl_mtx;
static pthread_mutex_t vsm_mtx;

static struct VSL_head		*vsl_head;
static const uint32_t		*vsl_end;
static uint32_t			*vsl_ptr;
static unsigned			vsl_segment;
static ssize_t			vsl_segsize;
static unsigned			vsl_seq;

struct VSC_C_main       *VSC_C_main;

/*--------------------------------------------------------------------
 * Check if the VSL_tag is masked by parameter bitmap
 */

static inline int
vsl_tag_is_masked(enum VSL_tag_e tag)
{
	volatile uint8_t *bm = &cache_param->vsl_mask[0];
	uint8_t b;

	assert(tag > SLT__Bogus);
	assert(tag < SLT__Reserved);
	bm += ((unsigned)tag >> 3);
	b = (0x80 >> ((unsigned)tag & 7));
	return (*bm & b);
}

/*--------------------------------------------------------------------
 * Lay down a header fields, and return pointer to the next record
 */

static inline uint32_t *
vsl_hdr(enum VSL_tag_e tag, uint32_t *p, unsigned len, uint32_t vxid)
{

	assert(((uintptr_t)p & 0x3) == 0);
	assert(tag > SLT__Bogus);
	assert(tag < SLT__Reserved);
	AZ(len & ~VSL_LENMASK);

	p[1] = vxid;
	p[0] = ((((unsigned)tag & 0xff) << 24) | len);
	return (VSL_END(p, len));
}

/*--------------------------------------------------------------------
 * Wrap the VSL buffer
 */

static void
vsl_wrap(void)
{

	assert(vsl_ptr >= vsl_head->log);
	assert(vsl_ptr < vsl_end);
	vsl_head->log[0] = VSL_ENDMARKER;
	do
		vsl_seq++;
	while (vsl_seq == 0);
	vsl_head->seq = vsl_seq;
	vsl_head->segments[0] = 0;
	VWMB();
	if (vsl_ptr != vsl_head->log) {
		*vsl_ptr = VSL_WRAPMARKER;
		vsl_ptr = vsl_head->log;
	}
	vsl_segment = 0;
	vsl_head->segment = vsl_segment;
	VSC_C_main->shm_cycles++;
}

/*--------------------------------------------------------------------
 * Reserve bytes for a record, wrap if necessary
 */

static uint32_t *
vsl_get(unsigned len, unsigned records, unsigned flushes)
{
	uint32_t *p;
	int err;
	unsigned old_segment;

	err = pthread_mutex_trylock(&vsl_mtx);
	if (err == EBUSY) {
		AZ(pthread_mutex_lock(&vsl_mtx));
		VSC_C_main->shm_cont++;
	} else {
		AZ(err);
	}
	assert(vsl_ptr < vsl_end);
	assert(((uintptr_t)vsl_ptr & 0x3) == 0);

	VSC_C_main->shm_writes++;
	VSC_C_main->shm_flushes += flushes;
	VSC_C_main->shm_records += records;

	/* Wrap if necessary */
	if (VSL_END(vsl_ptr, len) >= vsl_end)
		vsl_wrap();

	p = vsl_ptr;
	vsl_ptr = VSL_END(vsl_ptr, len);
	assert(vsl_ptr < vsl_end);
	assert(((uintptr_t)vsl_ptr & 0x3) == 0);

	*vsl_ptr = VSL_ENDMARKER;

	old_segment = vsl_segment;
	while ((vsl_ptr - vsl_head->log) / vsl_segsize > vsl_segment) {
		if (vsl_segment == VSL_SEGMENTS - 1)
			break;
		vsl_segment++;
		vsl_head->segments[vsl_segment] = vsl_ptr - vsl_head->log;
	}
	if (old_segment != vsl_segment) {
		/* Write memory barrier to ensure ENDMARKER and new table
		   values are seen before new segment number */
		VWMB();
		vsl_head->segment = vsl_segment;
	}

	AZ(pthread_mutex_unlock(&vsl_mtx));

	return (p);
}

/*--------------------------------------------------------------------
 * Stick a finished record into VSL.
 */

static void
vslr(enum VSL_tag_e tag, uint32_t vxid, const char *b, unsigned len)
{
	uint32_t *p;
	unsigned mlen;

	mlen = cache_param->shm_reclen;

	/* Truncate */
	if (len > mlen)
		len = mlen;

	p = vsl_get(len, 1, 0);

	memcpy(p + 2, b, len);

	/*
	 * vsl_hdr() writes p[1] again, but we want to make sure it
	 * has hit memory because we work on the live buffer here.
	 */
	p[1] = vxid;
	VWMB();
	(void)vsl_hdr(tag, p, len, vxid);
}

/*--------------------------------------------------------------------
 * Add a unbuffered record to VSL
 *
 * NB: This variant should be used sparingly and only for low volume
 * NB: since it significantly adds to the mutex load on the VSL.
 */

void
VSL(enum VSL_tag_e tag, uint32_t vxid, const char *fmt, ...)
{
	va_list ap;
	unsigned n, mlen = cache_param->shm_reclen;
	char buf[mlen];

	AN(fmt);
	if (vsl_tag_is_masked(tag))
		return;


	if (strchr(fmt, '%') == NULL) {
		vslr(tag, vxid, fmt, strlen(fmt) + 1);
	} else {
		va_start(ap, fmt);
		n = vsnprintf(buf, mlen, fmt, ap);
		va_end(ap);
		if (n > mlen - 1)
			n = mlen - 1;
		buf[n++] = '\0'; /* NUL-terminated */
		vslr(tag, vxid, buf, n);
	}
}

/*--------------------------------------------------------------------*/

void
VSL_Flush(struct vsl_log *vsl, int overflow)
{
	uint32_t *p;
	unsigned l;

	l = pdiff(vsl->wlb, vsl->wlp);
	if (l == 0)
		return;

	assert(l >= 8);

	p = vsl_get(l, vsl->wlr, overflow);

	memcpy(p + 2, vsl->wlb, l);
	p[1] = l;
	VWMB();
	p[0] = ((((unsigned)SLT__Batch & 0xff) << 24) | 0);
	vsl->wlp = vsl->wlb;
	vsl->wlr = 0;
}

/*--------------------------------------------------------------------
 * VSL-buffered-txt
 */

void
VSLbt(struct vsl_log *vsl, enum VSL_tag_e tag, txt t)
{
	unsigned l, mlen;
	char *p;

	Tcheck(t);
	if (vsl_tag_is_masked(tag))
		return;
	mlen = cache_param->shm_reclen;

	/* Truncate */
	l = Tlen(t);
	if (l > mlen - 1)
		l = mlen - 1;

	assert(vsl->wlp < vsl->wle);

	/* Flush if necessary */
	if (VSL_END(vsl->wlp, l + 1) >= vsl->wle)
		VSL_Flush(vsl, 1);
	assert(VSL_END(vsl->wlp, l + 1) < vsl->wle);
	p = VSL_DATA(vsl->wlp);
	memcpy(p, t.b, l);
	p[l++] = '\0';		/* NUL-terminated */
	vsl->wlp = vsl_hdr(tag, vsl->wlp, l, vsl->wid);
	assert(vsl->wlp < vsl->wle);
	vsl->wlr++;

	if (DO_DEBUG(DBG_SYNCVSL))
		VSL_Flush(vsl, 0);
}

/*--------------------------------------------------------------------
 * VSL-buffered
 */

void
VSLbv(struct vsl_log *vsl, enum VSL_tag_e tag, const char *fmt, va_list ap)
{
	char *p;
	const char *u, *f;
	unsigned n, mlen;
	txt t;

	AN(fmt);
	if (vsl_tag_is_masked(tag))
		return;

	/*
	 * If there are no printf-expansions, don't waste time expanding them
	 */
	f = NULL;
	for (u = fmt; *u != '\0'; u++)
		if (*u == '%')
			f = u;
	if (f == NULL) {
		t.b = TRUST_ME(fmt);
		t.e = TRUST_ME(u);
		VSLbt(vsl, tag, t);
		return;
	}

	mlen = cache_param->shm_reclen;

	/* Flush if we cannot fit a full size record */
	if (VSL_END(vsl->wlp, mlen + 1) >= vsl->wle)
		VSL_Flush(vsl, 1);

	p = VSL_DATA(vsl->wlp);
	n = vsnprintf(p, mlen, fmt, ap);
	if (n > mlen - 1)
		n = mlen - 1;	/* we truncate long fields */
	p[n++] = '\0';		/* NUL-terminated */
	vsl->wlp = vsl_hdr(tag, vsl->wlp, n, vsl->wid);
	assert(vsl->wlp < vsl->wle);
	vsl->wlr++;

	if (DO_DEBUG(DBG_SYNCVSL))
		VSL_Flush(vsl, 0);
}

void
VSLb(struct vsl_log *vsl, enum VSL_tag_e tag, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	VSLbv(vsl, tag, fmt, ap);
	va_end(ap);
}

void
VSLb_ts(struct vsl_log *vsl, const char *event, double first, double *pprev,
    double now)
{

	/* XXX: Make an option to turn off some unnecessary timestamp
	   logging. This must be done carefully because some functions
	   (e.g. WRW_Reserve) takes the last timestamp as it's inital
	   value for timeout calculation. */
	assert(!isnan(now) && now != 0.);
	VSLb(vsl, SLT_Timestamp, "%s: %.6f %.6f %.6f",
	    event, now, now - first, now - *pprev);
	*pprev = now;
}

/*--------------------------------------------------------------------
 * Setup a VSL buffer, allocate space if none provided.
 */

void
VSL_Setup(struct vsl_log *vsl, void *ptr, size_t len)
{

	if (ptr == NULL) {
		len = cache_param->vsl_buffer;
		ptr = malloc(len);
		AN(ptr);
	}
	vsl->wlp = ptr;
	vsl->wlb = ptr;
	vsl->wle = ptr;
	vsl->wle += len / sizeof(*vsl->wle);
	vsl->wlr = 0;
	vsl->wid = 0;
}

/*--------------------------------------------------------------------*/

static void *
vsm_cleaner(void *priv)
{
	(void)priv;
	THR_SetName("vsm_cleaner");
	while (1) {
		AZ(pthread_mutex_lock(&vsm_mtx));
		VSM_common_cleaner(heritage.vsm, VSC_C_main);
		AZ(pthread_mutex_unlock(&vsm_mtx));
		VTIM_sleep(1.1);
	}
	NEEDLESS_RETURN(NULL);
}

/*--------------------------------------------------------------------*/

void
VSM_Init(void)
{
	int i;
	pthread_t tp;

	AZ(pthread_mutex_init(&vsl_mtx, NULL));
	AZ(pthread_mutex_init(&vsm_mtx, NULL));

	vsl_head = VSM_Alloc(cache_param->vsl_space, VSL_CLASS, "", "");
	AN(vsl_head);
	vsl_end = vsl_head->log +
	    (cache_param->vsl_space - sizeof *vsl_head) / sizeof *vsl_end;
	vsl_segsize = (vsl_end - vsl_head->log) / VSL_SEGMENTS;

	memset(vsl_head, 0, sizeof *vsl_head);
	memcpy(vsl_head->marker, VSL_HEAD_MARKER, sizeof vsl_head->marker);
	vsl_head->segments[0] = 0;
	for (i = 1; i < VSL_SEGMENTS; i++)
		vsl_head->segments[i] = -1;
	vsl_ptr = vsl_head->log;
	*vsl_ptr = VSL_ENDMARKER;

	VWMB();
	do
		vsl_seq = random();
	while (vsl_seq == 0);
	vsl_head->seq = vsl_seq;
	VWMB();

	VSC_C_main = VSM_Alloc(sizeof *VSC_C_main,
	    VSC_CLASS, VSC_type_main, "");
	AN(VSC_C_main);
	memset(VSC_C_main, 0, sizeof *VSC_C_main);

	AZ(pthread_create(&tp, NULL, vsm_cleaner, NULL));
}

/*--------------------------------------------------------------------*/

void *
VSM_Alloc(unsigned size, const char *class, const char *type,
    const char *ident)
{
	volatile void *p;

	AZ(pthread_mutex_lock(&vsm_mtx));
	p = VSM_common_alloc(heritage.vsm, size, class, type, ident);
	AZ(pthread_mutex_unlock(&vsm_mtx));
	return (TRUST_ME(p));
}

void
VSM_Free(void *ptr)
{

	AZ(pthread_mutex_lock(&vsm_mtx));
	VSM_common_free(heritage.vsm, ptr);
	AZ(pthread_mutex_unlock(&vsm_mtx));
}
