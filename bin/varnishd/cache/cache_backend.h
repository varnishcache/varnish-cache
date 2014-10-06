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
struct vbc;
struct vrt_backend_probe;

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
	struct vbe_dir	*vdis;
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

/* cache_backend_cfg.c */
void VBE_DropRefConn(struct backend *, const struct acct_bereq *);
void VBE_DropRefVcl(struct backend *);
void VBE_DropRefLocked(struct backend *b, const struct acct_bereq *);
unsigned VBE_Healthy(const struct backend *b, double *changed);
void VBE_InitCfg(void);
struct backend *VBE_AddBackend(struct cli *cli, const struct vrt_backend *vb);
void VBE_Poll(void);

/* cache_backend_poll.c */
void VBP_Insert(struct backend *b, struct vrt_backend_probe const *p,
    const char *hosthdr);
void VBP_Remove(struct backend *b, struct vrt_backend_probe const *p);
void VBP_Use(const struct backend *b, const struct vrt_backend_probe *p);
void VBP_Summary(struct cli *cli, const struct vbp_target *vt);

/* cache_backend_poll.c */
void VBP_Init(void);

