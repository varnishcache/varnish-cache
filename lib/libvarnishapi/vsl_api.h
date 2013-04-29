/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2013 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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
 */

#include "vdef.h"
#include "vqueue.h"
#include "vapi/vsm.h"

#define VSL_FILE_HEAD			"VSL"

int vsl_diag(struct VSL_data *vsl, const char *fmt, ...)
    __printflike(2, 3);

typedef void vslc_delete_f(void *);
typedef int vslc_next_f(void *);
typedef int vslc_reset_f(void *);

struct vslc {
	struct VSL_cursor		c;
	unsigned			magic;
#define VSLC_MAGIC			0x5007C0DE

	vslc_delete_f			*delete;
	vslc_next_f			*next;
	vslc_reset_f			*reset;
};

struct VSL_data {
	unsigned			magic;
#undef VSL_MAGIC
#define VSL_MAGIC			0x8E6C92AA

	struct vsb			*diag;

	unsigned			flags;
#define F_SEEN_ix			(1 << 0)

	/* Bitmaps of -ix selected tags */
	struct vbitmap			*vbm_select;
	struct vbitmap			*vbm_supress;
};
