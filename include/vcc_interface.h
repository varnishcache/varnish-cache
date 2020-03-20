/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2019 Varnish Software AS
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
 * Runtime support for compiled VCL programs and VMODs.
 *
 * This file contains prototypes for functions nobody but VCC may call.
 *
 * NB: When this file is changed, lib/libvcc/generate.py *MUST* be rerun.
 */

VCL_VCL VPI_vcl_get(VRT_CTX, const char *);
void VPI_vcl_rel(VRT_CTX, VCL_VCL);
void VPI_vcl_select(VRT_CTX, VCL_VCL);

/***********************************************************************
 * VPI_count() refers to this structure for coordinates into the VCL source.
 */

struct vpi_ref {
	unsigned	source;
	unsigned	offset;
	unsigned	line;
	unsigned	pos;
	const char	*token;
};

void VPI_count(VRT_CTX, unsigned);

int VPI_Vmod_Init(VRT_CTX, struct vmod **hdl, unsigned nbr, void *ptr, int len,
    const char *nm, const char *path, const char *file_id, const char *backup);
void VPI_Vmod_Unload(struct vmod **hdl);

typedef int acl_match_f(VRT_CTX, const VCL_IP);

struct vrt_acl {
	unsigned        magic;
#define VRT_ACL_MAGIC   0x78329d96
	acl_match_f     *match;
	const char	*name;
};

/* vmod object instance info */
struct vpi_ii {
	const void *			p;
	const char * const		name;
};

VCL_STRANDS VPI_BundleStrands(int, struct strands *, char const **,
    const char *f, ...);

struct vcl_sub {
	unsigned		magic;
#define VCL_SUB_MAGIC		0x12c1750b
	const unsigned		methods;	// ok &= ctx->method
	const char * const	name;
	const struct VCL_conf	*vcl_conf;
	vcl_func_f		*func;
};
