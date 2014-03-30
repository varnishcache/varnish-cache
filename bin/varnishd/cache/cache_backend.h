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
 * This is the central switch-board for backend connections and it is
 * slightly complicated by a number of optimizations.
 *
 * The data structures:
 *
 *    A vrt_backend is a definition of a backend in a VCL program.
 *
 *    A backend is a TCP destination, possibly multi-homed and it has a
 *    number of associated properties and statistics.
 *
 *    A vbc is an open TCP connection to a backend.
 *
 *    A bereq is a memory carrier for handling a HTTP transaction with
 *    a backend over a vbc.
 *
 *    A director is a piece of code that selects which backend to use,
 *    by whatever method or metric it chooses.
 *
 * The relationships:
 *
 *    Backends and directors get instantiated when VCL's are loaded,
 *    and this always happen in the CLI thread.
 *
 *    When a VCL tries to instantiate a backend, any existing backend
 *    with the same identity (== definition in VCL) will be used instead
 *    so that vbc's can be reused across VCL changes.
 *
 *    Directors disapper with the VCL that created them.
 *
 *    Backends disappear when their reference count drop to zero.
 *
 *    Backends have their host/port name looked up to addrinfo structures
 *    when they are instantiated, and we just cache that result and cycle
 *    through the entries (for multihomed backends) on failure only.
 *    XXX: add cli command to redo lookup.
 *
 *    bereq is sort of a step-child here, we just manage the pool of them.
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
typedef unsigned vdi_healthy(const struct director *, double *changed);

struct director {
	unsigned		magic;
#define DIRECTOR_MAGIC		0x3336351d
	const char		*name;
	char			*vcl_name;
	vdi_getfd_f		*getfd;
	vdi_healthy		*healthy;
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

/* cache_backend_cfg.c */
void VBE_DropRefConn(struct backend *, const struct acct_bereq *);
void VBE_DropRefVcl(struct backend *);
void VBE_DropRefLocked(struct backend *b, const struct acct_bereq *);
unsigned VBE_Healthy(const struct backend *b, double *changed);

/* cache_backend_poll.c */
void VBP_Insert(struct backend *b, struct vrt_backend_probe const *p,
    const char *hosthdr);
void VBP_Remove(struct backend *b, struct vrt_backend_probe const *p);
void VBP_Use(const struct backend *b, const struct vrt_backend_probe *p);
void VBP_Summary(struct cli *cli, const struct vbp_target *vt);
