/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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

#include "cache_varnishd.h"

#include <stdio.h>
#include <stdlib.h>

#include "vgz.h"
#include "vsl_priv.h"
#include "vmb.h"

#include "common/heritage.h"
#include "common/vsmw.h"

/* ------------------------------------------------------------
 * strands helpers - move elsewhere?
 */

static unsigned
strands_len(const struct strands *s)
{
	unsigned r = 0;
	int i;

	for (i = 0; i < s->n; i++) {
		if (s->p[i] == NULL || *s->p[i] == '\0')
			continue;
		r += strlen(s->p[i]);
	}

	return (r);
}

/*
 * like VRT_Strands(), but truncating instead of failing for end of buffer
 *
 * returns number of bytes including NUL
 */
static unsigned
strands_cat(char *buf, unsigned bufl, const struct strands *s)
{
	unsigned l = 0, ll;
	int i;

	/* NUL-terminated */
	assert(bufl > 0);
	bufl--;

	for (i = 0; i < s->n && bufl > 0; i++) {
		if (s->p[i] == NULL || *s->p[i] == '\0')
			continue;
		ll = vmin_t(unsigned, strlen(s->p[i]), bufl);
		memcpy(buf, s->p[i], ll);
		l += ll;
		buf += ll;
		bufl -= ll;
	}
	*buf = '\0';	/* NUL-terminated */
	return (l + 1);
}

/* These cannot be struct lock, which depends on vsm/vsl working */
static pthread_mutex_t vsl_mtx;
static pthread_mutex_t vsc_mtx;
static pthread_mutex_t vsm_mtx;

static struct VSL_head		*vsl_head;
static const uint32_t		*vsl_end;
static uint32_t			*vsl_ptr;
static unsigned			vsl_segment_n;
static ssize_t			vsl_segsize;

struct VSC_main *VSC_C_main;

static void
vsl_sanity(const struct vsl_log *vsl)
{
	AN(vsl);
	AN(vsl->wlp);
	AN(vsl->wlb);
	AN(vsl->wle);
	assert(vsl->wlb <= vsl->wlp);
	assert(vsl->wlp <= vsl->wle);
}

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

int
VSL_tag_is_masked(enum VSL_tag_e tag)
{
	return (vsl_tag_is_masked(tag));
}

/*--------------------------------------------------------------------
 * Lay down a header fields, and return pointer to the next record
 */

static inline uint32_t *
vsl_hdr(enum VSL_tag_e tag, uint32_t *p, unsigned len, vxid_t vxid)
{

	AZ((uintptr_t)p & 0x3);
	assert(tag > SLT__Bogus);
	assert(tag < SLT__Reserved);
	AZ(len & ~VSL_LENMASK);

	p[2] = vxid.vxid >> 32;
	p[1] = vxid.vxid;
	p[0] = (((unsigned)tag & VSL_IDMASK) << VSL_IDSHIFT) |
	     (VSL_VERSION_3 << VSL_VERSHIFT) |
	     len;
	return (VSL_END(p, len));
}

/*--------------------------------------------------------------------
 * Space available in a VSL buffer when accounting for overhead
 */

static unsigned
vsl_space(const struct vsl_log *vsl)
{
	ptrdiff_t mlen;

	mlen = vsl->wle - vsl->wlp;
	assert(mlen >= 0);
	if (mlen < VSL_OVERHEAD + 1)
		return (0);
	mlen -= VSL_OVERHEAD;
	mlen *= sizeof *vsl->wlp;
	if (mlen > cache_param->vsl_reclen)
		mlen = cache_param->vsl_reclen;
	return(mlen);
}

/*--------------------------------------------------------------------
 * Wrap the VSL buffer
 */

static void
vsl_wrap(void)
{

	assert(vsl_ptr >= vsl_head->log);
	assert(vsl_ptr < vsl_end);
	vsl_segment_n += VSL_SEGMENTS - (vsl_segment_n % VSL_SEGMENTS);
	assert(vsl_segment_n % VSL_SEGMENTS == 0);
	vsl_head->offset[0] = 0;
	vsl_head->log[0] = VSL_ENDMARKER;
	VWMB();
	if (vsl_ptr != vsl_head->log) {
		*vsl_ptr = VSL_WRAPMARKER;
		vsl_ptr = vsl_head->log;
	}
	vsl_head->segment_n = vsl_segment_n;
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

	err = pthread_mutex_trylock(&vsl_mtx);
	if (err == EBUSY) {
		PTOK(pthread_mutex_lock(&vsl_mtx));
		VSC_C_main->shm_cont++;
	} else {
		AZ(err);
	}
	assert(vsl_ptr < vsl_end);
	AZ((uintptr_t)vsl_ptr & 0x3);

	VSC_C_main->shm_writes++;
	VSC_C_main->shm_flushes += flushes;
	VSC_C_main->shm_records += records;
	VSC_C_main->shm_bytes +=
	    VSL_BYTES(VSL_OVERHEAD + VSL_WORDS((uint64_t)len));

	/* Wrap if necessary */
	if (VSL_END(vsl_ptr, len) >= vsl_end)
		vsl_wrap();

	p = vsl_ptr;
	vsl_ptr = VSL_END(vsl_ptr, len);
	assert(vsl_ptr < vsl_end);
	AZ((uintptr_t)vsl_ptr & 0x3);

	*vsl_ptr = VSL_ENDMARKER;

	while ((vsl_ptr - vsl_head->log) / vsl_segsize >
	    vsl_segment_n % VSL_SEGMENTS) {
		vsl_segment_n++;
		vsl_head->offset[vsl_segment_n % VSL_SEGMENTS] =
		    vsl_ptr - vsl_head->log;
	}

	PTOK(pthread_mutex_unlock(&vsl_mtx));
	/* Implicit VWMB() in mutex op ensures ENDMARKER and new table
	   values are seen before new segment number */
	vsl_head->segment_n = vsl_segment_n;

	return (p);
}

/*--------------------------------------------------------------------
 * Stick a finished record into VSL.
 */

static void
vslr(enum VSL_tag_e tag, vxid_t vxid, const char *b, unsigned len)
{
	uint32_t *p;
	unsigned mlen;

	mlen = cache_param->vsl_reclen;

	/* Truncate */
	if (len > mlen)
		len = mlen;

	p = vsl_get(len, 1, 0);

	memcpy(p + VSL_OVERHEAD, b, len);

	/*
	 * the vxid needs to be written before the barrier to
	 * ensure it is valid when vsl_hdr() marks the record
	 * ready by writing p[0]
	 */
	p[2] = vxid.vxid >> 32;
	p[1] = vxid.vxid;
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
VSLv(enum VSL_tag_e tag, vxid_t vxid, const char *fmt, va_list ap)
{
	unsigned n, mlen = cache_param->vsl_reclen;
	v_vla_(char, buf, mlen);

	AN(fmt);
	if (vsl_tag_is_masked(tag))
		return;

	if (strchr(fmt, '%') == NULL) {
		vslr(tag, vxid, fmt, strlen(fmt) + 1);
	} else {
		n = vsnprintf(buf, mlen, fmt, ap);
		n = vmin(n, mlen - 1);
		buf[n++] = '\0'; /* NUL-terminated */
		vslr(tag, vxid, buf, n);
	}

}

void
VSLs(enum VSL_tag_e tag, vxid_t vxid, const struct strands *s)
{
	unsigned n, mlen = cache_param->vsl_reclen;
	v_vla_(char, buf, mlen);

	if (vsl_tag_is_masked(tag))
		return;

	n = strands_cat(buf, mlen, s);

	vslr(tag, vxid, buf, n);
}

void
VSL(enum VSL_tag_e tag, vxid_t vxid, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	VSLv(tag, vxid, fmt, ap);
	va_end(ap);
}

/*--------------------------------------------------------------------*/

void
VSL_Flush(struct vsl_log *vsl, int overflow)
{
	uint32_t *p;
	unsigned l;

	vsl_sanity(vsl);
	l = pdiff(vsl->wlb, vsl->wlp);
	if (l == 0)
		return;

	assert(l >= 8);

	p = vsl_get(l, vsl->wlr, overflow);

	memcpy(p + VSL_OVERHEAD, vsl->wlb, l);
	p[1] = l;
	VWMB();
	p[0] = ((((unsigned)SLT__Batch & 0xff) << VSL_IDSHIFT));
	vsl->wlp = vsl->wlb;
	vsl->wlr = 0;
}

/*--------------------------------------------------------------------
 * Buffered VSLs
 */

static char *
vslb_get(struct vsl_log *vsl, enum VSL_tag_e tag, unsigned *length)
{
	unsigned mlen = cache_param->vsl_reclen;
	char *retval;

	vsl_sanity(vsl);
	if (*length < mlen)
		mlen = *length;

	if (VSL_END(vsl->wlp, mlen) > vsl->wle)
		VSL_Flush(vsl, 1);

	retval = VSL_DATA(vsl->wlp);

	/* If it still doesn't fit, truncate */
	if (VSL_END(vsl->wlp, mlen) > vsl->wle)
		mlen = vsl_space(vsl);

	vsl->wlp = vsl_hdr(tag, vsl->wlp, mlen, vsl->wid);
	vsl->wlr++;
	*length = mlen;
	return (retval);
}

static void
vslb_simple(struct vsl_log *vsl, enum VSL_tag_e tag,
    unsigned length, const char *str)
{
	char *p;

	if (length == 0)
		length = strlen(str);
	length += 1; // NUL
	p = vslb_get(vsl, tag, &length);
	memcpy(p, str, length - 1);
	p[length - 1] = '\0';

	if (DO_DEBUG(DBG_SYNCVSL))
		VSL_Flush(vsl, 0);
}

/*--------------------------------------------------------------------
 * VSL-buffered-txt
 */

void
VSLbt(struct vsl_log *vsl, enum VSL_tag_e tag, txt t)
{

	Tcheck(t);
	if (vsl_tag_is_masked(tag))
		return;

	vslb_simple(vsl, tag, Tlen(t), t.b);
}

/*--------------------------------------------------------------------
 * VSL-buffered-strands
 */
void
VSLbs(struct vsl_log *vsl, enum VSL_tag_e tag, const struct strands *s)
{
	unsigned l;
	char *p;

	if (vsl_tag_is_masked(tag))
		return;

	l = strands_len(s) + 1;
	p = vslb_get(vsl, tag, &l);

	(void)strands_cat(p, l, s);

	if (DO_DEBUG(DBG_SYNCVSL))
		VSL_Flush(vsl, 0);
}

/*--------------------------------------------------------------------
 * VSL-buffered
 */

void
VSLbv(struct vsl_log *vsl, enum VSL_tag_e tag, const char *fmt, va_list ap)
{
	char *p, *p1;
	unsigned n = 0, mlen;
	va_list ap2;

	AN(fmt);
	if (vsl_tag_is_masked(tag))
		return;

	/*
	 * If there are no printf-expansions, don't waste time expanding them
	 */
	if (strchr(fmt, '%') == NULL) {
		vslb_simple(vsl, tag, 0, fmt);
		return;
	}

	/*
	 * If the format is trivial, deal with it directly
	 */
	if (!strcmp(fmt, "%s")) {
		p1 = va_arg(ap, char *);
		vslb_simple(vsl, tag, 0, p1);
		return;
	}

	vsl_sanity(vsl);

	mlen = vsl_space(vsl);

	// First attempt, only if any space at all
	if (mlen > 0) {
		p = VSL_DATA(vsl->wlp);
		va_copy(ap2, ap);
		n = vsnprintf(p, mlen, fmt, ap2);
		va_end(ap2);
	}

	// Second attempt, if a flush might help
	if (mlen == 0 || (n + 1 > mlen && n + 1 <= cache_param->vsl_reclen)) {
		VSL_Flush(vsl, 1);
		mlen = vsl_space(vsl);
		p = VSL_DATA(vsl->wlp);
		n = vsnprintf(p, mlen, fmt, ap);
	}
	if (n + 1 < mlen)
		mlen = n + 1;
	(void)vslb_get(vsl, tag, &mlen);

	if (DO_DEBUG(DBG_SYNCVSL))
		VSL_Flush(vsl, 0);
}

void
VSLb(struct vsl_log *vsl, enum VSL_tag_e tag, const char *fmt, ...)
{
	va_list ap;

	vsl_sanity(vsl);
	va_start(ap, fmt);
	VSLbv(vsl, tag, fmt, ap);
	va_end(ap);
}

#define Tf6 "%ju.%06ju"
#define Ta6(t) (uintmax_t)floor((t)), (uintmax_t)floor((t) * 1e6) % 1000000U

void
VSLb_ts(struct vsl_log *vsl, const char *event, vtim_real first,
    vtim_real *pprev, vtim_real now)
{

	/*
	 * XXX: Make an option to turn off some unnecessary timestamp
	 * logging. This must be done carefully because some functions
	 * (e.g. V1L_Open) takes the last timestamp as its initial
	 * value for timeout calculation.
	 */
	vsl_sanity(vsl);
	AN(event);
	AN(pprev);
	assert(!isnan(now) && now != 0.);
	VSLb(vsl, SLT_Timestamp, "%s: " Tf6 " " Tf6 " " Tf6,
	    event, Ta6(now), Ta6(now - first), Ta6(now - *pprev));
	*pprev = now;
}

void
VSLb_bin(struct vsl_log *vsl, enum VSL_tag_e tag, ssize_t len, const void *ptr)
{
	unsigned mlen;
	char *p;

	vsl_sanity(vsl);
	AN(ptr);
	if (vsl_tag_is_masked(tag))
		return;
	mlen = cache_param->vsl_reclen;

	/* Truncate */
	len = vmin_t(ssize_t, len, mlen);

	assert(vsl->wlp <= vsl->wle);

	/* Flush if necessary */
	if (VSL_END(vsl->wlp, len) > vsl->wle)
		VSL_Flush(vsl, 1);
	assert(VSL_END(vsl->wlp, len) <= vsl->wle);
	p = VSL_DATA(vsl->wlp);
	memcpy(p, ptr, len);
	vsl->wlp = vsl_hdr(tag, vsl->wlp, len, vsl->wid);
	assert(vsl->wlp <= vsl->wle);
	vsl->wlr++;

	if (DO_DEBUG(DBG_SYNCVSL))
		VSL_Flush(vsl, 0);
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
	vsl->wid = NO_VXID;
	vsl_sanity(vsl);
}

/*--------------------------------------------------------------------*/

void
VSL_ChgId(struct vsl_log *vsl, const char *typ, const char *why, vxid_t vxid)
{
	vxid_t ovxid;

	vsl_sanity(vsl);
	ovxid = vsl->wid;
	VSLb(vsl, SLT_Link, "%s %ju %s", typ, VXID(vxid), why);
	VSL_End(vsl);
	vsl->wid = vxid;
	VSLb(vsl, SLT_Begin, "%s %ju %s", typ, VXID(ovxid), why);
}

/*--------------------------------------------------------------------*/

void
VSL_End(struct vsl_log *vsl)
{
	txt t;
	char p[] = "";

	vsl_sanity(vsl);
	assert(!IS_NO_VXID(vsl->wid));
	t.b = p;
	t.e = p;
	VSLbt(vsl, SLT_End, t);
	VSL_Flush(vsl, 0);
	vsl->wid = NO_VXID;
}

static void v_matchproto_(vsm_lock_f)
vsm_vsc_lock(void)
{
	PTOK(pthread_mutex_lock(&vsc_mtx));
}

static void v_matchproto_(vsm_lock_f)
vsm_vsc_unlock(void)
{
	PTOK(pthread_mutex_unlock(&vsc_mtx));
}

static void v_matchproto_(vsm_lock_f)
vsm_vsmw_lock(void)
{
	PTOK(pthread_mutex_lock(&vsm_mtx));
}

static void v_matchproto_(vsm_lock_f)
vsm_vsmw_unlock(void)
{
	PTOK(pthread_mutex_unlock(&vsm_mtx));
}

/*--------------------------------------------------------------------*/

void
VSM_Init(void)
{
	unsigned u;

	assert(UINT_MAX % VSL_SEGMENTS == VSL_SEGMENTS - 1);

	PTOK(pthread_mutex_init(&vsl_mtx, &mtxattr_errorcheck));
	PTOK(pthread_mutex_init(&vsc_mtx, &mtxattr_errorcheck));
	PTOK(pthread_mutex_init(&vsm_mtx, &mtxattr_errorcheck));

	vsc_lock = vsm_vsc_lock;
	vsc_unlock = vsm_vsc_unlock;
	vsmw_lock = vsm_vsmw_lock;
	vsmw_unlock = vsm_vsmw_unlock;

	heritage.proc_vsmw = VSMW_New(heritage.vsm_fd, 0640, "_.index");
	AN(heritage.proc_vsmw);

	VSC_C_main = VSC_main_New(NULL, NULL, "");
	AN(VSC_C_main);

	AN(heritage.proc_vsmw);
	vsl_head = VSMW_Allocf(heritage.proc_vsmw, NULL, VSL_CLASS,
	    cache_param->vsl_space, VSL_CLASS);
	AN(vsl_head);
	vsl_segsize = ((cache_param->vsl_space - sizeof *vsl_head) /
	    sizeof *vsl_end) / VSL_SEGMENTS;
	vsl_end = vsl_head->log + vsl_segsize * VSL_SEGMENTS;
	/* Make segment_n always overflow on first log wrap to make any
	   problems with regard to readers on that event visible */
	vsl_segment_n = UINT_MAX - (VSL_SEGMENTS - 1);
	AZ(vsl_segment_n % VSL_SEGMENTS);
	vsl_ptr = vsl_head->log;
	*vsl_ptr = VSL_ENDMARKER;

	memset(vsl_head, 0, sizeof *vsl_head);
	vsl_head->segsize = vsl_segsize;
	vsl_head->offset[0] = 0;
	vsl_head->segment_n = vsl_segment_n;
	for (u = 1; u < VSL_SEGMENTS; u++)
		vsl_head->offset[u] = -1;
	VWMB();
	memcpy(vsl_head->marker, VSL_HEAD_MARKER, sizeof vsl_head->marker);
}
