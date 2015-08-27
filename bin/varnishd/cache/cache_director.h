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

/*--------------------------------------------------------------------
 * A director is a piece of code which selects one of possibly multiple
 * backends to use.
 */


typedef unsigned vdi_healthy_f(const struct director *, const struct busyobj *,
    double *changed);

typedef const struct director *vdi_resolve_f(const struct director *,
    struct worker *, struct busyobj *);

typedef int vdi_gethdrs_f(const struct director *, struct worker *,
    struct busyobj *);
typedef int vdi_getbody_f(const struct director *, struct worker *,
    struct busyobj *);
typedef const struct suckaddr *vdi_getip_f(const struct director *,
    struct worker *, struct busyobj *);
typedef void vdi_finish_f(const struct director *, struct worker *,
    struct busyobj *);

typedef void vdi_http1pipe_f(const struct director *, struct req *,
    struct busyobj *);

typedef void vdi_panic_f(const struct director *, struct vsb *);

struct director {
	unsigned		magic;
#define DIRECTOR_MAGIC		0x3336351d
	const char		*name;
	char			*vcl_name;
	vdi_http1pipe_f		*http1pipe;
	vdi_healthy_f		*healthy;
	vdi_resolve_f		*resolve;
	vdi_gethdrs_f		*gethdrs;
	vdi_getbody_f		*getbody;
	vdi_getip_f		*getip;
	vdi_finish_f		*finish;
	vdi_panic_f		*panic;
	void			*priv;
	const void		*priv2;
};

/* cache_director.c */

int VDI_GetHdr(struct worker *, struct busyobj *);
int VDI_GetBody(struct worker *, struct busyobj *);
const struct suckaddr *VDI_GetIP(struct worker *, struct busyobj *);

void VDI_Finish(struct worker *wrk, struct busyobj *bo);

int VDI_Http1Pipe(struct req *, struct busyobj *);

int VDI_Healthy(const struct director *, const struct busyobj *);
void VDI_Panic(const struct director *, struct vsb *, const char *nm);
