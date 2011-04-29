/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Linpro AS
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

typedef struct vbc *vdi_getfd_f(const struct director *, struct sess *sp);
typedef void vdi_fini_f(const struct director *);
typedef unsigned vdi_healthy(const struct director *, const struct sess *sp);

struct director {
	unsigned		magic;
#define DIRECTOR_MAGIC		0x3336351d
	const char		*name;
	char			*vcl_name;
	vdi_getfd_f		*getfd;
	vdi_fini_f		*fini;
	vdi_healthy		*healthy;
	void			*priv;
};

/*--------------------------------------------------------------------
 * List of objectheads that have recently been rejected by VCL.
 */

struct trouble {
	unsigned		magic;
#define TROUBLE_MAGIC		0x4211ab21
	uintptr_t		target;
	double			timeout;
	VTAILQ_ENTRY(trouble)	list;
};

/*--------------------------------------------------------------------
 * An instance of a backend from a VCL program.
 */

struct backend {
	unsigned		magic;
#define BACKEND_MAGIC		0x64c4c7c6

	VTAILQ_ENTRY(backend)	list;
	int			refcount;
	struct lock		mtx;

	char			*vcl_name;
	char			*ipv4_addr;
	char			*ipv6_addr;
	char			*port;

	struct sockaddr_storage	*ipv4;
	socklen_t		ipv4len;
	struct sockaddr_storage	*ipv6;
	socklen_t		ipv6len;

	unsigned		n_conn;
	VTAILQ_HEAD(, vbc)	connlist;

	struct vbp_target	*probe;
	unsigned		healthy;
	VTAILQ_HEAD(, trouble)	troublelist;

	struct vsc_vbe		*vsc;
};

/* cache_backend.c */
void VBE_ReleaseConn(struct vbc *vc);
struct backend *vdi_get_backend_if_simple(const struct director *d);

/* cache_backend_cfg.c */
extern struct lock VBE_mtx;
void VBE_DropRefConn(struct backend *);
void VBE_DropRefVcl(struct backend *);
void VBE_DropRefLocked(struct backend *b);

/* cache_backend_poll.c */
void VBP_Start(struct backend *b, struct vrt_backend_probe const *p, const char *hosthdr);
void VBP_Stop(struct backend *b, struct vrt_backend_probe const *p);

/* Init functions for directors */
typedef void dir_init_f(struct cli *, struct director **, int , const void*);
dir_init_f VRT_init_dir_simple;
dir_init_f VRT_init_dir_dns;
dir_init_f VRT_init_dir_hash;
dir_init_f VRT_init_dir_random;
dir_init_f VRT_init_dir_round_robin;
dir_init_f VRT_init_dir_client;
