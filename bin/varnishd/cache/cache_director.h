/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2018 Varnish Software AS
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
 *
 * This is the private implementation of directors.
 * You are not supposed to need anything here.
 *
 */

struct vcldir {
	unsigned			magic;
#define VCLDIR_MAGIC			0xbf726c7d
	int				refcnt;
	unsigned			flags;
#define VDIR_FLG_NOREFCNT		1
	struct lock			dlck;
	struct director			*dir;
	struct vcl			*vcl;
	const struct vdi_methods	*methods;
	VTAILQ_ENTRY(vcldir)		directors_list;
	VTAILQ_ENTRY(vcldir)		resigning_list;
	const struct vdi_ahealth	*admin_health;
	vtim_real			health_changed;
	char				*cli_name;
};

#define VBE_AHEALTH_LIST					\
	VBE_AHEALTH(healthy,	HEALTHY,	1)		\
	VBE_AHEALTH(sick,	SICK,		0)		\
	VBE_AHEALTH(auto,	AUTO,		-1)		\
	VBE_AHEALTH(deleted,	DELETED,	0)

#define VBE_AHEALTH(l,u,h) extern const struct vdi_ahealth * const VDI_AH_##u;
VBE_AHEALTH_LIST
#undef VBE_AHEALTH
