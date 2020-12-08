/*-
 * Copyright (c) 2019 Varnish Software AS
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
 * VCL/VMOD symbol table
 */

struct vclprog;
struct vmodfile;
struct vjsn_val;
struct vclstate;

struct vmoddep {
	unsigned		magic;
#define VMODDEP_MAGIC		0xc1490542
	VTAILQ_ENTRY(vmoddep)	lfrom;
	struct vmodfile		*to;
	VTAILQ_ENTRY(vmoddep)	lto;
};

struct vcldep {
	unsigned		magic;
#define VCLDEP_MAGIC		0xa9a17dc2
	struct vclprog		*from;
	VTAILQ_ENTRY(vcldep)	lfrom;
	struct vclprog		*to;
	VTAILQ_ENTRY(vcldep)	lto;
	const struct vjsn_val	*vj;
};

struct vclprog {
	unsigned		magic;
#define VCLPROG_MAGIC		0x9ac09fea
	VTAILQ_ENTRY(vclprog)	list;
	char			*name;
	char			*fname;
	unsigned		warm;
	const struct vclstate	*state;
	double			go_cold;
	struct vjsn		*symtab;
	VTAILQ_HEAD(, vcldep)	dfrom;
	VTAILQ_HEAD(, vcldep)	dto;
	int			nto;
	int			loaded;
	VTAILQ_HEAD(, vmoddep)	vmods;
	unsigned		discard;
	VTAILQ_ENTRY(vclprog)	discard_list;
};

struct vmodfile {
	unsigned		magic;
#define VMODFILE_MAGIC		0xffa1a0d5
	char			*fname;
	VTAILQ_ENTRY(vmodfile)	list;
	VTAILQ_HEAD(, vmoddep)	vcls;
};

extern VTAILQ_HEAD(vclproghead, vclprog)	vclhead;
extern VTAILQ_HEAD(vmodfilehead, vmodfile)	vmodhead;

struct vclprog *mcf_vcl_byname(const char *name);
struct vcldep *mgt_vcl_dep_add(struct vclprog *vp_from, struct vclprog *vp_to);
int mcf_is_label(const struct vclprog *vp);

void mgt_vcl_symtab_clean(struct vclprog *vp);
void mgt_vcl_symtab(struct vclprog *vp, const char *input);
void mcf_vcl_symtab(struct cli *cli, const char * const *av, void *priv);

