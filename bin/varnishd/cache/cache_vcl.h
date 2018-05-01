/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2016 Varnish Software AS
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
 * NB: This is a private .h file for cache_vcl*.c
 * NB: No other code should include this file.
 *
 */

struct vfp_filter;

VTAILQ_HEAD(vfp_filter_head, vfp_filter);


struct vcl {
	unsigned		magic;
#define VCL_MAGIC		0x214188f2
	VTAILQ_ENTRY(vcl)	list;
	void			*dlh;
	const struct VCL_conf	*conf;
	char			state[8];
	char			*loaded_name;
	unsigned		busy;
	unsigned		discard;
	const char		*temp;
	pthread_rwlock_t	temp_rwl;
	VTAILQ_HEAD(,vcldir)	director_list;
	VTAILQ_HEAD(,vclref)	ref_list;
	int			nrefs;
	struct vcl		*label;
	int			nlabels;
	struct vfp_filter_head	vfps;
};

struct vclref {
	unsigned		magic;
#define VCLREF_MAGIC		0x47fb6848
	const struct vcl	*vcl;
	VTAILQ_ENTRY(vclref)	list;
	char			desc[32];
};

extern struct lock		vcl_mtx;
extern struct vcl		*vcl_active; /* protected by vcl_mtx */
struct vcl *vcl_find(const char *);
void vcl_get(struct vcl **, struct vcl *);

extern const char * const VCL_TEMP_INIT;
extern const char * const VCL_TEMP_COLD;
extern const char * const VCL_TEMP_WARM;
extern const char * const VCL_TEMP_BUSY;
extern const char * const VCL_TEMP_COOLING;
extern const char * const VCL_TEMP_LABEL;

/*
 * NB: The COOLING temperature is neither COLD nor WARM.
 * And LABEL is not a temperature, it's a different kind of VCL.
 */
#define VCL_WARM(v) ((v)->temp == VCL_TEMP_WARM || (v)->temp == VCL_TEMP_BUSY)
#define VCL_COLD(v) ((v)->temp == VCL_TEMP_INIT || (v)->temp == VCL_TEMP_COLD)


