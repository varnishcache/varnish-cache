/*-
 * Copyright (c) 2008-2011 Varnish Software AS
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

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vtc.h"
#include "vtc_log.h"

#include "vtim.h"
#include "vbt.h"

static pthread_mutex_t	vtclog_mtx;
static char		*vtclog_buf;
static unsigned		vtclog_left;

static pthread_key_t log_key;
static double t0;

void
vtc_log_set_cmd(struct vtclog *vl, const struct cmds *cmds)
{
	AN(cmds);
	vl->cmds = cmds;
}

/**********************************************************************/

#define GET_VL(vl)					\
	do {						\
		CHECK_OBJ_NOTNULL(vl, VTCLOG_MAGIC);	\
		PTOK(pthread_mutex_lock(&vl->mtx));	\
		vl->act = 1;				\
		VSB_clear(vl->vsb);			\
	} while(0)

#define REL_VL(vl)					\
	do {						\
		AZ(VSB_finish(vl->vsb));		\
		vtc_log_emit(vl);			\
		VSB_clear(vl->vsb);			\
		vl->act = 0;				\
		PTOK(pthread_mutex_unlock(&vl->mtx));	\
	} while(0)


struct vtclog *
vtc_logopen(const char *fmt, ...)
{
	struct vtclog *vl;
	va_list ap;
	char buf[BUFSIZ];

	va_start(ap, fmt);
	vbprintf(buf, fmt, ap);
	va_end(ap);

	ALLOC_OBJ(vl, VTCLOG_MAGIC);
	AN(vl);
	REPLACE(vl->id, buf);
	vl->vsb = VSB_new_auto();
	PTOK(pthread_mutex_init(&vl->mtx, NULL));
	PTOK(pthread_setspecific(log_key, vl));
	return (vl);
}

void
vtc_logclose(void *arg)
{
	struct vtclog *vl;

	CAST_OBJ_NOTNULL(vl, arg, VTCLOG_MAGIC);
	if (pthread_getspecific(log_key) == vl)
		PTOK(pthread_setspecific(log_key, NULL));
	VSB_destroy(&vl->vsb);
	PTOK(pthread_mutex_destroy(&vl->mtx));
	REPLACE(vl->id, NULL);
	FREE_OBJ(vl);
}

static void v_noreturn_
vtc_logfail(void)
{

	vtc_error = 2;
	if (!pthread_equal(pthread_self(), vtc_thread))
		pthread_exit(NULL);
	else
		exit(fail_out());
}

static const char * const lead[] = {
	"----",
	"*   ",
	"**  ",
	"*** ",
	"****"
};

#define NLEAD (sizeof(lead)/sizeof(lead[0]))

static void
vtc_leadinv(const struct vtclog *vl, int lvl, const char *fmt, va_list ap)
{

	assert(lvl < (int)NLEAD);
	assert(lvl >= 0);
	VSB_printf(vl->vsb, "%s %-5s ",
	    lead[lvl < 0 ? 1: lvl], vl->id);
	if (fmt != NULL)
		(void)VSB_vprintf(vl->vsb, fmt, ap);
}

static void
vtc_leadin(const struct vtclog *vl, int lvl, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vtc_leadinv(vl, lvl, fmt, ap);
	va_end(ap);
}

static void
vtc_log_emit(const struct vtclog *vl)
{
	unsigned l;
	int i;
	int t_this;
	static int t_last = -1;

	l = VSB_len(vl->vsb);
	if (l == 0)
		return;
	t_this = (int)round((VTIM_mono() - t0) * 1000);
	PTOK(pthread_mutex_lock(&vtclog_mtx));
	if (t_last != t_this) {
		assert(vtclog_left > 25);
		i = snprintf(vtclog_buf, vtclog_left,
		    "**** dT    %d.%03d\n", t_this / 1000, t_this % 1000);
		t_last = t_this;
		vtclog_buf += i;
		vtclog_left -= i;
	}
	assert(vtclog_left > l);
	memcpy(vtclog_buf, VSB_data(vl->vsb), l);
	vtclog_buf += l;
	*vtclog_buf = '\0';
	vtclog_left -= l;
	PTOK(pthread_mutex_unlock(&vtclog_mtx));
}

void
vtc_fatal(struct vtclog *vl, const char *fmt, ...)
{

	GET_VL(vl);
	va_list ap;
	va_start(ap, fmt);
	vtc_leadinv(vl, 0, fmt, ap);
	VSB_putc(vl->vsb, '\n');
	va_end(ap);
	REL_VL(vl);

	vtc_logfail();
}

void
vtc_log(struct vtclog *vl, int lvl, const char *fmt, ...)
{

	GET_VL(vl);
	va_list ap;
	va_start(ap, fmt);
	if (lvl >= 0) {
		vtc_leadinv(vl, lvl, fmt, ap);
		VSB_putc(vl->vsb, '\n');
	}
	va_end(ap);
	REL_VL(vl);

	if (lvl == 0)
		vtc_logfail();
}

/**********************************************************************
 * Dump a string
 */

#define MAX_DUMP 8192

void
vtc_dump(struct vtclog *vl, int lvl, const char *pfx, const char *str, int len)
{
	char buf[64];
	int quote = VSB_QUOTE_UNSAFE | VSB_QUOTE_ESCHEX;

	AN(pfx);
	GET_VL(vl);
	if (str == NULL)
		vtc_leadin(vl, lvl, "%s(null)\n", pfx);
	else {
		bprintf(buf, "%s %-5s %s|",
		    lead[lvl < 0 ? 1: lvl], vl->id, pfx);
		if (len < 0)
			len = strlen(str);
		else if (str[0] == 0x1f && (uint8_t)str[1] == 0x8b)
			quote = VSB_QUOTE_HEX; // Dump gzip data in HEX
		VSB_quote_pfx(vl->vsb, buf, str,
		    len > MAX_DUMP ? MAX_DUMP : len, quote);
		if (quote == VSB_QUOTE_HEX)
			VSB_putc(vl->vsb, '\n');
		if (len > MAX_DUMP)
			VSB_printf(vl->vsb, "%s [...] (%d)\n",
			    buf, len - MAX_DUMP);
	}
	REL_VL(vl);
	if (lvl == 0)
		vtc_logfail();
}

/**********************************************************************
 * Hexdump
 */

void
vtc_hexdump(struct vtclog *vl, int lvl, const char *pfx,
    const void *ptr, unsigned len)
{
	int nl = 1;
	unsigned l;
	const uint8_t *ss = ptr;

	AN(pfx);
	GET_VL(vl);
	if (ss == NULL)
		vtc_leadin(vl, lvl, "%s(null)\n", pfx);
	else {
		for (l = 0; l < len; l++, ss++) {
			if (l > 512) {
				VSB_cat(vl->vsb, "...");
				break;
			}
			if (nl) {
				vtc_leadin(vl, lvl, "%s| ", pfx);
				nl = 0;
			}
			VSB_printf(vl->vsb, " %02x", *ss);
			if ((l & 0xf) == 0xf) {
				VSB_cat(vl->vsb, "\n");
				nl = 1;
			}
		}
	}
	if (!nl)
		VSB_cat(vl->vsb, "\n");
	REL_VL(vl);
	if (lvl == 0)
		vtc_logfail();
}

/**********************************************************************/

static void v_noreturn_
vtc_log_VAS_Fail(const char *func, const char *file, int line,
    const char *cond, enum vas_e why)
{
	char buf[4096] = "";
	struct vtclog *vl;
	int e = errno;

	(void)why;

	if (VBT_dump(sizeof buf, buf) < 0) {
		bprintf(buf, "Failed to print backtrace: %d (%s)\n",
		    errno, strerror(errno));
	}

	vl = pthread_getspecific(log_key);
	if (vl == NULL || vl->act) {
		fprintf(stderr,
		    "Assert error in %s(), %s line %d:\n"
		    "  Condition(%s) not true. (errno=%d %s)\n"
		    "%s\n",
		    func, file, line, cond, e, strerror(e), buf);
	} else
		vtc_fatal(vl, "Assert error in %s(), %s line %d:"
		    "  Condition(%s) not true."
		    "  Errno=%d %s\n"
		    "%s\n",
		    func, file, line, cond, e, strerror(e), buf);
	abort();
}

/**********************************************************************/

void
vtc_loginit(char *buf, unsigned buflen)
{

	VAS_Fail_Func = vtc_log_VAS_Fail;
	t0 = VTIM_mono();
	vtclog_buf = buf;
	vtclog_left = buflen;
	PTOK(pthread_mutex_init(&vtclog_mtx, NULL));
	PTOK(pthread_key_create(&log_key, NULL));
}
