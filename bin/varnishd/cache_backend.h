/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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
 * $Id$
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
 *    A vbe_conn is an open TCP connection to a backend.
 *
 *    A bereq is a memory carrier for handling a HTTP transaction with
 *    a backend over a vbe_conn.
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
 *    so that vbe_conn's can be reused across VCL changes.
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
struct vbe_conn;
struct vrt_backend_probe;

/*--------------------------------------------------------------------
 * A director is a piece of code which selects one of possibly multiple
 * backends to use.
 */

typedef struct vbe_conn *vdi_getfd_f(struct sess *sp);
typedef void vdi_fini_f(struct director *d);
typedef unsigned vdi_healthy(const struct sess *sp);

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
 * An instance of a backend from a VCL program.
 */

struct backend {
	unsigned		magic;
#define BACKEND_MAGIC		0x64c4c7c6

	char			*hosthdr;
	char			*ident;
	char			*vcl_name;
	double			connect_timeout;
	double			first_byte_timeout;
	double			between_bytes_timeout;

	uint32_t		hash;

	VTAILQ_ENTRY(backend)	list;
	int			refcount;
	struct lock		mtx;

	struct sockaddr		*ipv4;
	socklen_t		ipv4len;
	struct sockaddr		*ipv6;
	socklen_t		ipv6len;

	unsigned		max_conn;
	unsigned		n_conn;
	VTAILQ_HEAD(, vbe_conn)	connlist;

	struct vbp_target	*probe;
	unsigned		healthy;
};

/* cache_backend.c */
void VBE_ReleaseConn(struct vbe_conn *vc);
struct vbe_conn *VBE_GetVbe(struct sess *sp, struct backend *bp);

/* cache_backend_cfg.c */
extern struct lock VBE_mtx;
void VBE_DropRefConn(struct backend *);
void VBE_DropRef(struct backend *);
void VBE_DropRefLocked(struct backend *b);

/* cache_backend_poll.c */
void VBP_Start(struct backend *b, struct vrt_backend_probe const *p);
void VBP_Stop(struct backend *b);
