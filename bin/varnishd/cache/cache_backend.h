/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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
 * Backend and Director APIs
 *
 * A director ("VDI") is an abstract entity which can either satisfy a
 * backend fetch request or select another director for the job.
 *
 * In theory a director does not have to talk HTTP over TCP, it can satisfy
 * the backend request using any means it wants, although this is presently
 * not implemented.
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
struct vbc;
struct vrt_backend_probe;

/*--------------------------------------------------------------------
 * A director is a piece of code which selects one of possibly multiple
 * backends to use.
 */

typedef struct vbc *vdi_getfd_f(const struct director *, struct busyobj *);
typedef unsigned vdi_healthy_f(const struct director *, double *changed);
typedef const struct director *vdi_resolve_f(const struct director *,
    struct worker *, struct busyobj *);
typedef int vdi_gethdrs_f(const struct director *, struct worker *,
    struct busyobj *);
typedef int vdi_getbody_f(const struct director *, struct worker *,
    struct busyobj *);
typedef void vdi_finish_f(const struct director *, struct worker *,
    struct busyobj *);

struct director {
	unsigned		magic;
#define DIRECTOR_MAGIC		0x3336351d
	const char		*name;
	char			*vcl_name;
	vdi_getfd_f		*getfd;
	vdi_healthy_f		*healthy;
	vdi_resolve_f		*resolve;
	vdi_gethdrs_f		*gethdrs;
	vdi_getbody_f		*getbody;
	vdi_finish_f		*finish;
	void			*priv;
};

/*--------------------------------------------------------------------
 * An instance of a backend from a VCL program.
 */

enum admin_health {
	ah_invalid = 0,
	ah_healthy,
	ah_sick,
	ah_probe
};

struct backend {
	unsigned		magic;
#define BACKEND_MAGIC		0x64c4c7c6

	VTAILQ_ENTRY(backend)	list;
	int			refcount;
	struct lock		mtx;

	char			*vcl_name;
	char			*display_name;
	char			*ipv4_addr;
	char			*ipv6_addr;
	char			*port;

	struct suckaddr		*ipv4;
	struct suckaddr		*ipv6;

	unsigned		n_conn;
	VTAILQ_HEAD(, vbc)	connlist;

	struct vbp_target	*probe;
	unsigned		healthy;
	enum admin_health	admin_health;
	double			health_changed;

	struct VSC_C_vbe	*vsc;
};

/* -------------------------------------------------------------------*/

/* Backend connection */
struct vbc {
	unsigned		magic;
#define VBC_MAGIC		0x0c5e6592
	VTAILQ_ENTRY(vbc)	list;
	struct backend		*backend;
	struct vdi_simple	*vdis;
	struct vsl_log		*vsl;
	int			fd;

	struct suckaddr		*addr;

	uint8_t			recycled;

	/* Timeouts */
	double			first_byte_timeout;
	double			between_bytes_timeout;
};

/* cache_backend.c */
void VBE_ReleaseConn(struct vbc *vc);
void VBE_UseHealth(const struct director *vdi);
void VBE_DiscardHealth(const struct director *vdi);
void VBE_CloseFd(struct vbc **vbp, const struct acct_bereq *);

/* cache_backend_cfg.c */
void VBE_DropRefConn(struct backend *, const struct acct_bereq *);
void VBE_DropRefVcl(struct backend *);
void VBE_DropRefLocked(struct backend *b, const struct acct_bereq *);
unsigned VBE_Healthy(const struct backend *b, double *changed);
void VBE_InitCfg(void);
struct backend *VBE_AddBackend(struct cli *cli, const struct vrt_backend *vb);

/* cache_backend_poll.c */
void VBP_Insert(struct backend *b, struct vrt_backend_probe const *p,
    const char *hosthdr);
void VBP_Remove(struct backend *b, struct vrt_backend_probe const *p);
void VBP_Use(const struct backend *b, const struct vrt_backend_probe *p);
void VBP_Summary(struct cli *cli, const struct vbp_target *vt);

/* cache_dir.c */
int VDI_GetHdr(struct worker *wrk, struct busyobj *bo);
int VDI_GetBody(const struct director *d, struct worker *wrk,
    struct busyobj *bo);
void VDI_Finish(const struct director *d, struct worker *wrk,
    struct busyobj *bo);
struct vbc *VDI_GetFd(const struct director *d, struct worker *wrk,
    struct busyobj *);
int VDI_Healthy(const struct director *);
void VDI_AddHostHeader(struct http *to, const struct vbc *vbc);
void VBE_Poll(void);
void VDI_Init(void);

/* cache_backend_poll.c */
void VBP_Init(void);

