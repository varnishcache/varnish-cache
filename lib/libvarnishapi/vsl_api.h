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
#include "vre.h"
#include "vapi/vsm.h"

#define VSL_FILE_ID			"VSL"

/*lint -esym(534, vsl_diag) */
int vsl_diag(struct VSL_data *vsl, const char *fmt, ...)
    __printflike(2, 3);
int vsl_skip(struct VSL_cursor *c, ssize_t words);

typedef void vslc_delete_f(struct VSL_cursor *);
typedef int vslc_next_f(struct VSL_cursor *);
typedef int vslc_reset_f(struct VSL_cursor *);
typedef int vslc_skip_f(struct VSL_cursor *, ssize_t words);
typedef int vslc_check_f(const struct VSL_cursor *, const struct VSLC_ptr *);

struct vslc_tbl {
	unsigned			magic;
#define VSLC_TBL_MAGIC			0x5007C0DE

	vslc_delete_f			*delete;
	vslc_next_f			*next;
	vslc_reset_f			*reset;
	vslc_skip_f			*skip;
	vslc_check_f			*check;
};

struct vslf {
	unsigned			magic;
#define VSLF_MAGIC			0x08650B39
	VTAILQ_ENTRY(vslf)		list;

	int				tag;
	vre_t				*vre;
};

typedef VTAILQ_HEAD(,vslf)		vslf_list;

struct VSL_data {
	unsigned			magic;
#define VSL_MAGIC			0x8E6C92AA

	struct vsb			*diag;

	unsigned			flags;
#define F_SEEN_ixIX			(1 << 0)

	/* Bitmaps of -ix selected tags */
	struct vbitmap			*vbm_select;
	struct vbitmap			*vbm_supress;

	/* Lists of -IX filters */
	vslf_list			vslf_select;
	vslf_list			vslf_suppress;

	int				v_opt;
};

/* vsl_query.c */
struct vslq_query;
struct vslq_query *vslq_newquery(struct VSL_data *vsl,
    enum VSL_grouping_e grouping, const char *query);
void vslq_deletequery(struct vslq_query **pquery);
int vslq_runquery(const struct vslq_query *query, struct VSL_transaction * const ptrans[]);
