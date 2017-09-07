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

#include "common.h"
#include "common/common_vsm.h"

#include "vend.h"
#include "vsmw.h"
#include "vgz.h"
#include "vmb.h"
#include "vapi/vsc_int.h"

struct vsmw *proc_vsmw;

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
VSC_Alloc(const char *nm, size_t sd, size_t sj, const unsigned char *zj,
    size_t szj, const char *fmt, va_list va)
{
	char *p;
	z_stream vz;
	struct vsc_segs *vsg;
	char buf[1024];

	if (vsc_lock != NULL)
		vsc_lock();

	if (*fmt == '\0')
		bprintf(buf, "%s", nm);
	else
		bprintf(buf, "%s.%s", nm, fmt);

	AN(proc_vsmw);
	p = VSMW_Allocv(proc_vsmw, VSC_CLASS, 8 + sd + sj, buf, va);
	AN(p);

	memset(&vz, 0, sizeof vz);
	assert(Z_OK == inflateInit2(&vz, 31));
	vz.next_in = TRUST_ME(zj);
	vz.avail_in = szj;
	vz.next_out = (void*)(p + 8 + sd);
	vz.avail_out = sj;
	assert(Z_STREAM_END == inflate(&vz, Z_FINISH));
	assert(Z_OK == inflateEnd(&vz));
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
VSC_Destroy(const char *nm, const void *p)
{
	struct vsc_segs *vsg;

	if (vsc_lock != NULL)
		vsc_lock();

	AN(proc_vsmw);
	VTAILQ_FOREACH(vsg, &vsc_seglist, list) {
		if (vsg->ptr != p)
			continue;
		assert(vsg->nm == nm);
		VSMW_Free(proc_vsmw, &vsg->seg);
		VTAILQ_REMOVE(&vsc_seglist, vsg, list);
		FREE_OBJ(vsg);
		break;
	}
	if (vsc_unlock != NULL)
		vsc_unlock();
}
