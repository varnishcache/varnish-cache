/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "vdef.h"
#include "vrt.h"

#include "miniobj.h"
#include "vas.h"
#include "vend.h"
#include "vmb.h"
#include "vsmw.h"
#include "vqueue.h"

#include "common/heritage.h"

/*--------------------------------------------------------------------*/

struct vsc_segs {
	unsigned		magic;
#define VSC_SEGS_MAGIC		0x9b355991

	const char		*nm;
	VTAILQ_ENTRY(vsc_segs)	list;
	void			*seg;
	void			*ptr;
};

static VTAILQ_HEAD(,vsc_segs)	vsc_seglist =
    VTAILQ_HEAD_INITIALIZER(vsc_seglist);

vsc_callback_f *vsc_lock;
vsc_callback_f *vsc_unlock;

void *
VRT_VSC_Alloc(const char *nm, size_t sd, const unsigned char *jp,
    size_t sj, const char *fmt, va_list va)
{
	char *p;
	struct vsc_segs *vsg;
	char buf[1024];

	if (vsc_lock != NULL)
		vsc_lock();

	if (*fmt == '\0')
		bprintf(buf, "%s", nm);
	else
		bprintf(buf, "%s.%s", nm, fmt);

	AN(heritage.proc_vsmw);
	p = VSMW_Allocv(heritage.proc_vsmw, "Stat", 8 + sd + sj, buf, va);
	AN(p);

	memcpy(p + 8 + sd, jp, sj);
	ALLOC_OBJ(vsg, VSC_SEGS_MAGIC);
	AN(vsg);
	vsg->seg = p;
	vsg->ptr = p + 8;
	VTAILQ_INSERT_TAIL(&vsc_seglist, vsg, list);
	VWMB();
	vbe64enc(p, sd);
	vsg->nm = nm;
	if (vsc_unlock != NULL)
		vsc_unlock();
	return (p + 8);
}

void
VRT_VSC_Destroy(const char *nm, const void *p)
{
	struct vsc_segs *vsg;

	if (vsc_lock != NULL)
		vsc_lock();

	AN(heritage.proc_vsmw);
	VTAILQ_FOREACH(vsg, &vsc_seglist, list)
		if (vsg->ptr == p)
			break;
	AN(vsg);
	assert(vsg->nm == nm);
	VSMW_Free(heritage.proc_vsmw, &vsg->seg);
	VTAILQ_REMOVE(&vsc_seglist, vsg, list);
	FREE_OBJ(vsg);
	if (vsc_unlock != NULL)
		vsc_unlock();
}
