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
 *
 * Backend and APIs
 *
 * A backend ("VBE") is a director which talks HTTP over TCP.
 *
 * As you'll notice the terminology is a bit muddled here, but we try to
 * keep it clean on the user-facing side, where a "director" is always
 * a "pick a backend/director" functionality, and a "backend" is whatever
 * satisfies the actual request in the end.
 *
 */

struct vbp_target;
struct vrt_ctx;
struct vrt_backend_probe;
struct tcp_pool;

/*--------------------------------------------------------------------
 * An instance of a backend from a VCL program.
 */

struct backend {
	unsigned		magic;
#define BACKEND_MAGIC		0x64c4c7c6

	unsigned		n_conn;

	VTAILQ_ENTRY(backend)	list;
	struct lock		mtx;

	VRT_BACKEND_FIELDS()

	struct vbp_target	*probe;

	struct vsc_seg		*vsc_seg;
	struct VSC_vbe		*vsc;

	struct tcp_pool		*tcp_pool;

	struct director		director[1];

	vtim_real		cooled;
};

/*---------------------------------------------------------------------
 * Prototypes
 */

/* cache_backend_cfg.c */
void VBE_SetHappy(const struct backend *, uint64_t);

/* cache_backend_probe.c */
void VBP_Insert(struct backend *b, struct vrt_backend_probe const *p,
    struct tcp_pool *);
void VBP_Remove(struct backend *b);
void VBP_Control(const struct backend *b, int stop);
void VBP_Status(struct cli *cli, const struct backend *, int details);
void VBE_Connect_Error(struct VSC_vbe *, int err);
