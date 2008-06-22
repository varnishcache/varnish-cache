/*
 * Copyright (c) 2006-2008 Linpro AS
 * All rights reserved.
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
 *
 * $Id$
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <pthread.h>
#include <stdarg.h>

#include "libvarnish.h"
#include "vsb.h"
#include "miniobj.h"

#include "vtc.h"

int vtc_verbosity = 3;

struct vtclog {
	unsigned	magic;
#define VTCLOG_MAGIC	0x82731202
	const char	*id;
	struct vsb	*vsb;
	pthread_mutex_t	mtx;
};

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

static const char *lead[] = {
	"----",
	"#   ",
	"##  ",
	"### ",
	"####"
};

#define NLEAD (sizeof(lead)/sizeof(lead[0]))

void
vtc_log(struct vtclog *vl, unsigned lvl, const char *fmt, ...)
{

	assert(lvl < NLEAD);
	if (lvl > vtc_verbosity)
		return;
	AZ(pthread_mutex_lock(&vl->mtx));
	vsb_printf(vl->vsb, "%s %-4s ", lead[lvl], vl->id);
	va_list ap;
	va_start(ap, fmt);
	(void)vsb_vprintf(vl->vsb, fmt, ap);
	va_end(ap);
	vsb_putc(vl->vsb, '\n');
	vsb_finish(vl->vsb);
	AZ(vsb_overflowed(vl->vsb));
	(void)fputs(vsb_data(vl->vsb), stdout);
	vsb_clear(vl->vsb);
	AZ(pthread_mutex_unlock(&vl->mtx));
	if (lvl == 0)
		exit (1);
}

/**********************************************************************
 * Dump a string
 */

void
vtc_dump(struct vtclog *vl, unsigned lvl, const char *pfx, const char *str)
{
	int nl = 1;

	assert(lvl < NLEAD);
	if (lvl > vtc_verbosity)
		return;
	if (pfx == NULL)
		pfx = "";
	AZ(pthread_mutex_lock(&vl->mtx));
	if (str == NULL) 
		vsb_printf(vl->vsb, "%s %-4s %s(null)\n",
		    lead[lvl], vl->id, pfx);
	else
		for(; *str; str++) {
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
	if (!nl)
		vsb_printf(vl->vsb, "\n");
	vsb_finish(vl->vsb);
	AZ(vsb_overflowed(vl->vsb));
	(void)fputs(vsb_data(vl->vsb), stdout);
	vsb_clear(vl->vsb);
	AZ(pthread_mutex_unlock(&vl->mtx));
	if (lvl == 0)
		exit (1);
}
