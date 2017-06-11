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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "common.h"
#include "common/common_vsm.h"

#include "vend.h"
#include "vgz.h"
#include "vmb.h"
#include "vapi/vsc_int.h"

/*--------------------------------------------------------------------*/

void *VSM_Alloc(unsigned size, const char *class, const char *type,
    const char *ident);
void VSM_Free(void *ptr);

/*--------------------------------------------------------------------*/

struct vsc_segs {
	unsigned		magic;
#define VSC_SEGS_MAGIC		0x9b355991

	VTAILQ_ENTRY(vsc_segs)	list;
	void			*seg;
	void			*ptr;
};

static VTAILQ_HEAD(,vsc_segs)	vsc_seglist =
    VTAILQ_HEAD_INITIALIZER(vsc_seglist);

void *
VSC_Alloc(const char *nm, size_t sd,
    size_t sj, const unsigned char *zj, size_t szj,
    const char *fmt, va_list va)
{
	char *p;
	z_stream vz;
	struct vsc_segs *vsg;

	(void)nm;
	(void)fmt;
	(void)va;


	p = VSM_Alloc(8 + sd + sj, VSC_CLASS, nm, fmt);
	AN(p);

	memset(p, 0, sd);

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
	return (p + 8);
}

void
VSC_Destroy(const char *nm, const void *p)
{
	struct vsc_segs *vsg;

	(void)nm;
	VTAILQ_FOREACH(vsg, &vsc_seglist, list) {
		if (vsg->ptr != p)
			continue;
		VSM_Free(vsg->seg);
		VTAILQ_REMOVE(&vsc_seglist, vsg, list);
		FREE_OBJ(vsg);
		return;
	}
	WRONG("Freeing unknown VSC");
}
