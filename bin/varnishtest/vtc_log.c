/*
 * Copyright (c) 2008-2010 Redpill Linpro AS
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

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <stdarg.h>

#include "libvarnish.h"
#include "vsb.h"
#include "miniobj.h"

#include "vtc.h"

int vtc_verbosity = 0;

static struct vsb	*vtclog_full;
static pthread_mutex_t	vtclog_mtx;

struct vtclog {
	unsigned	magic;
#define VTCLOG_MAGIC	0x82731202
	const char	*id;
	struct vsb	*vsb;
	pthread_mutex_t	mtx;
};

/**********************************************************************/

void
vtc_loginit()
{

	vtclog_full = vsb_newauto();
	AN(vtclog_full);
	AZ(pthread_mutex_init(&vtclog_mtx, NULL));
}

void
vtc_logreset()
{

	vsb_clear(vtclog_full);
}

const char *
vtc_logfull(void)
{
	vsb_finish(vtclog_full);
	AZ(vsb_overflowed(vtclog_full));
	return (vsb_data(vtclog_full));
}

/**********************************************************************/


struct vtclog *
vtc_logopen(const char *id)
{
	struct vtclog *vl;

	ALLOC_OBJ(vl, VTCLOG_MAGIC);
	AN(vl);
	vl->id = id;
	vl->vsb = vsb_newauto();
	AZ(pthread_mutex_init(&vl->mtx, NULL));
	return (vl);
}

void
vtc_logclose(struct vtclog *vl)
{

	CHECK_OBJ_NOTNULL(vl, VTCLOG_MAGIC);
	vsb_delete(vl->vsb);
	AZ(pthread_mutex_destroy(&vl->mtx));
	FREE_OBJ(vl);
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
vtc_log_emit(const struct vtclog *vl, unsigned lvl)
{
	if (vtc_stop && lvl == 0)
		return;
	AZ(pthread_mutex_lock(&vtclog_mtx));
	vsb_cat(vtclog_full, vsb_data(vl->vsb));
	AZ(pthread_mutex_unlock(&vtclog_mtx));

	if (lvl <= vtc_verbosity)
		(void)fputs(vsb_data(vl->vsb), stdout);
}

//lint -e{818}
void
vtc_log(struct vtclog *vl, unsigned lvl, const char *fmt, ...)
{

	CHECK_OBJ_NOTNULL(vl, VTCLOG_MAGIC);
	AZ(pthread_mutex_lock(&vl->mtx));
	assert(lvl < NLEAD);
	vsb_clear(vl->vsb);
	vsb_printf(vl->vsb, "%s %-4s ", lead[lvl], vl->id);
	va_list ap;
	va_start(ap, fmt);
	(void)vsb_vprintf(vl->vsb, fmt, ap);
	va_end(ap);
	vsb_putc(vl->vsb, '\n');
	vsb_finish(vl->vsb);
	AZ(vsb_overflowed(vl->vsb));

	vtc_log_emit(vl, lvl);

	vsb_clear(vl->vsb);
	AZ(pthread_mutex_unlock(&vl->mtx));
	if (lvl == 0) {
		vtc_error = 1;
		if (pthread_self() != vtc_thread)
			pthread_exit(NULL);
	}
}

/**********************************************************************
 * Dump a string
 */

//lint -e{818}
void
vtc_dump(struct vtclog *vl, unsigned lvl, const char *pfx, const char *str)
{
	int nl = 1;
	unsigned l;

	CHECK_OBJ_NOTNULL(vl, VTCLOG_MAGIC);
	assert(lvl < NLEAD);
	AZ(pthread_mutex_lock(&vl->mtx));
	vsb_clear(vl->vsb);
	if (pfx == NULL)
		pfx = "";
	if (str == NULL)
		vsb_printf(vl->vsb, "%s %-4s %s(null)\n",
		    lead[lvl], vl->id, pfx);
	else {
		l = 0;
		for(; *str != '\0'; str++) {
			if (++l > 512) {
				vsb_printf(vl->vsb, "...");
				break;
			}
			if (nl) {
				vsb_printf(vl->vsb, "%s %-4s %s| ",
				    lead[lvl], vl->id, pfx);
				nl = 0;
			}
			if (*str == '\r')
				vsb_printf(vl->vsb, "\\r");
			else if (*str == '\t')
				vsb_printf(vl->vsb, "\\t");
			else if (*str == '\n') {
				vsb_printf(vl->vsb, "\\n\n");
				nl = 1;
			} else if (*str < 0x20 || *str > 0x7e)
				vsb_printf(vl->vsb, "\\x%02x", *str);
			else
				vsb_printf(vl->vsb, "%c", *str);
		}
	}
	if (!nl)
		vsb_printf(vl->vsb, "\n");
	vsb_finish(vl->vsb);
	AZ(vsb_overflowed(vl->vsb));

	vtc_log_emit(vl, lvl);

	vsb_clear(vl->vsb);
	AZ(pthread_mutex_unlock(&vl->mtx));
	if (lvl == 0) {
		vtc_error = 1;
		if (pthread_self() != vtc_thread)
			pthread_exit(NULL);
	}
}
