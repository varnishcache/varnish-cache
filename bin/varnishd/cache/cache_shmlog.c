/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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

#include "cache_backend.h"	// For w->vbc

#include "vmb.h"
#include "vtim.h"

/* These cannot be struct lock, which depends on vsm/vsl working */
static pthread_mutex_t vsl_mtx;
static pthread_mutex_t vsm_mtx;

static uint32_t			*vsl_start;
static const uint32_t		*vsl_end;
static uint32_t			*vsl_ptr;

struct VSC_C_main       *VSC_C_main;

static inline uint32_t
vsl_w0(uint32_t type, uint32_t length)
{

	assert(length < 0x10000);
        return (((type & 0xff) << 24) | length);
}

/*--------------------------------------------------------------------*/

static inline void
vsl_hdr(enum VSL_tag_e tag, uint32_t *p, unsigned len, unsigned id)
{

	assert(((uintptr_t)p & 0x3) == 0);

	p[1] = id;
	VMB();
	p[0] = vsl_w0(tag, len);
}

/*--------------------------------------------------------------------*/

static void
vsl_wrap(void)
{

	assert(vsl_ptr >= vsl_start + 1);
	assert(vsl_ptr < vsl_end);
	vsl_start[1] = VSL_ENDMARKER;
	do
		vsl_start[0]++;
	while (vsl_start[0] == 0);
	VWMB();
	if (vsl_ptr != vsl_start + 1) {
		*vsl_ptr = VSL_WRAPMARKER;
		vsl_ptr = vsl_start + 1;
	}
	VSC_C_main->shm_cycles++;
}

/*--------------------------------------------------------------------
 * Reserve bytes for a record, wrap if necessary
 */

static uint32_t *
vsl_get(unsigned len, unsigned records, unsigned flushes)
{
	uint32_t *p;

	if (pthread_mutex_trylock(&vsl_mtx)) {
		AZ(pthread_mutex_lock(&vsl_mtx));
		VSC_C_main->shm_cont++;
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

	*vsl_ptr = VSL_ENDMARKER;

	assert(vsl_ptr < vsl_end);
	assert(((uintptr_t)vsl_ptr & 0x3) == 0);
	AZ(pthread_mutex_unlock(&vsl_mtx));

	return (p);
}

/*--------------------------------------------------------------------
 * This variant copies a byte-range directly to the log, without
 * taking the detour over sprintf()
 */

static void
VSLR(enum VSL_tag_e tag, int id, const char *b, unsigned len)
{
	uint32_t *p;
	unsigned mlen;

	mlen = cache_param->shm_reclen;

	/* Truncate */
	if (len > mlen)
		len = mlen;

	p = vsl_get(len, 1, 0);

	memcpy(p + 2, b, len);
	vsl_hdr(tag, p, len, id);
}

/*--------------------------------------------------------------------*/

void
VSL(enum VSL_tag_e tag, int id, const char *fmt, ...)
{
	va_list ap;
	unsigned n, mlen = cache_param->shm_reclen;
	char buf[mlen];

	/*
	 * XXX: consider formatting into a stack buffer then move into
	 * XXX: shmlog with VSLR().
	 */
	AN(fmt);
	va_start(ap, fmt);

	if (strchr(fmt, '%') == NULL) {
		VSLR(tag, id, fmt, strlen(fmt));
	} else {
		n = vsnprintf(buf, mlen, fmt, ap);
		if (n > mlen)
			n = mlen;
		VSLR(tag, id, buf, n);
	}
	va_end(ap);
}

/*--------------------------------------------------------------------*/

void
WSL_Flush(struct worker *w, int overflow)
{
	uint32_t *p;
	unsigned l;

	l = pdiff(w->wlb, w->wlp);
	if (l == 0)
		return;

	assert(l >= 8);

	p = vsl_get(l - 8, w->wlr, overflow);

	memcpy(p + 1, w->wlb + 1, l - 4);
	VWMB();
	p[0] = w->wlb[0];
	w->wlp = w->wlb;
	w->wlr = 0;
}

/*--------------------------------------------------------------------*/

void
WSLR(struct worker *w, enum VSL_tag_e tag, int id, txt t)
{
	unsigned l, mlen;

	Tcheck(t);
	mlen = cache_param->shm_reclen;

	/* Truncate */
	l = Tlen(t);
	if (l > mlen) {
		l = mlen;
		t.e = t.b + l;
	}

	assert(w->wlp < w->wle);

	/* Wrap if necessary */
	if (VSL_END(w->wlp, l) >= w->wle)
		WSL_Flush(w, 1);
	assert (VSL_END(w->wlp, l) < w->wle);
	memcpy(VSL_DATA(w->wlp), t.b, l);
	vsl_hdr(tag, w->wlp, l, id);
	w->wlp = VSL_END(w->wlp, l);
	assert(w->wlp < w->wle);
	w->wlr++;
	if (cache_param->diag_bitmap & 0x10000)
		WSL_Flush(w, 0);
}

/*--------------------------------------------------------------------*/

static void
wsl(struct worker *w, enum VSL_tag_e tag, int id, const char *fmt, va_list ap)
{
	char *p;
	unsigned n, mlen;
	txt t;

	AN(fmt);
	mlen = cache_param->shm_reclen;

	if (strchr(fmt, '%') == NULL) {
		t.b = TRUST_ME(fmt);
		t.e = strchr(t.b, '\0');
		WSLR(w, tag, id, t);
	} else {
		assert(w->wlp < w->wle);

		/* Wrap if we cannot fit a full size record */
		if (VSL_END(w->wlp, mlen) >= w->wle)
			WSL_Flush(w, 1);

		p = VSL_DATA(w->wlp);
		n = vsnprintf(p, mlen, fmt, ap);
		if (n > mlen)
			n = mlen;	/* we truncate long fields */
		vsl_hdr(tag, w->wlp, n, id);
		w->wlp = VSL_END(w->wlp, n);
		assert(w->wlp < w->wle);
		w->wlr++;
	}
	if (cache_param->diag_bitmap & 0x10000)
		WSL_Flush(w, 0);
}

/*--------------------------------------------------------------------*/

void
WSL(struct worker *w, enum VSL_tag_e tag, int id, const char *fmt, ...)
{
	va_list ap;

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	AN(fmt);
	va_start(ap, fmt);
	wsl(w, tag, id, fmt, ap);
	va_end(ap);
}


/*--------------------------------------------------------------------*/

void
WSLB(struct worker *w, enum VSL_tag_e tag, const char *fmt, ...)
{
	va_list ap;

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(w->busyobj, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(w->busyobj->vbc, VBC_MAGIC);
	AN(fmt);
	va_start(ap, fmt);
	wsl(w, tag, w->busyobj->vbc->vsl_id, fmt, ap);
	va_end(ap);
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
	uint32_t *vsl_log_start;
	pthread_t tp;

	AZ(pthread_mutex_init(&vsl_mtx, NULL));
	AZ(pthread_mutex_init(&vsm_mtx, NULL));

	vsl_log_start = VSM_Alloc(cache_param->vsl_space, VSL_CLASS, "", "");
	AN(vsl_log_start);
	vsl_log_start[1] = VSL_ENDMARKER;
	VWMB();
	do
		*vsl_log_start = random() & 0xffff;
	while (*vsl_log_start == 0);
	VWMB();

	vsl_start = vsl_log_start;
	vsl_end = vsl_start +
	    cache_param->vsl_space / (unsigned)sizeof *vsl_end;
	vsl_ptr = vsl_start + 1;

	VSC_C_main = VSM_Alloc(sizeof *VSC_C_main,
	    VSC_CLASS, VSC_TYPE_MAIN, "");
	AN(VSC_C_main);

	vsl_wrap();
	// VSM_head->starttime = (intmax_t)VTIM_real();
	memset(VSC_C_main, 0, sizeof *VSC_C_main);
	// VSM_head->child_pid = getpid();

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
