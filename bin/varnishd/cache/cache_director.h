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
 * Director APIs
 *
 * A director ("VDI") is an abstract entity which can either satisfy a
 * backend fetch request or select another director for the job.
 *
 * In theory a director does not have to talk HTTP over TCP, it can satisfy
 * the backend request using any means it wants, although this is presently
 * not implemented.
 *
 */

struct vcldir;

typedef VCL_BOOL vdi_healthy_f(VRT_CTX, VCL_BACKEND, VCL_TIME *);
typedef VCL_BACKEND vdi_resolve_f(VRT_CTX, VCL_BACKEND);

typedef int vdi_gethdrs_f(VCL_BACKEND, struct worker *, struct busyobj *);
typedef int vdi_getbody_f(VCL_BACKEND, struct worker *, struct busyobj *);
typedef const struct suckaddr *vdi_getip_f(VCL_BACKEND,
    struct worker *, struct busyobj *);
typedef void vdi_finish_f(VCL_BACKEND, struct worker *, struct busyobj *);

typedef enum sess_close vdi_http1pipe_f(VCL_BACKEND, struct req *,
    struct busyobj *);

typedef void vdi_event_f(VCL_BACKEND, enum vcl_event_e);

typedef void vdi_destroy_f(VCL_BACKEND);

typedef void vdi_panic_f(VCL_BACKEND, struct vsb *);

typedef void vdi_list_f(VCL_BACKEND, struct vsb *, int, int);

struct director_methods {
	unsigned			magic;
#define DIRECTOR_METHODS_MAGIC		0x4ec0c4bb
	const char			*type;
	vdi_http1pipe_f			*http1pipe;
	vdi_healthy_f			*healthy;
	vdi_resolve_f			*resolve;
	vdi_gethdrs_f			*gethdrs;
	vdi_getbody_f			*getbody;
	vdi_getip_f			*getip;
	vdi_finish_f			*finish;
	vdi_event_f			*event;
	vdi_destroy_f			*destroy;
	vdi_panic_f			*panic;
	vdi_list_f			*list;
};

struct director {
	unsigned			magic;
#define DIRECTOR_MAGIC			0x3336351d
	const struct director_methods	*methods;
	char				*vcl_name;

	void				*priv;
	const void			*priv2;

	/* Internal Housekeeping fields */

	struct vcldir			*vdir;

	char				*cli_name;

	unsigned			health;
	const struct vdi_ahealth	*admin_health;
	double				health_changed;
};


/* cache_vcl.c */
int VRT_AddDirector(VRT_CTX, struct director *, const char *, ...)
    v_printflike_(3, 4);

void VRT_DelDirector(struct director *);

/* cache_director.c */

#define VBE_AHEALTH_LIST					\
	VBE_AHEALTH(healthy,	HEALTHY,	1)		\
	VBE_AHEALTH(sick,	SICK,		0)		\
	VBE_AHEALTH(probe,	PROBE,		-1)		\
	VBE_AHEALTH(deleted,	DELETED,	0)

#define VBE_AHEALTH(l,u,h) extern const struct vdi_ahealth * const VDI_AH_##u;
VBE_AHEALTH_LIST
#undef VBE_AHEALTH

const char *VDI_Ahealth(const struct director *d);
