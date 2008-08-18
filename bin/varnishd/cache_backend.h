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
struct vrt_backend_probe;

/* Backend indstance */
struct backend {
	unsigned		magic;
#define BACKEND_MAGIC		0x64c4c7c6

	char			*hosthdr;
	char			*ident;
	char			*vcl_name;
	double			connect_timeout;

	uint32_t		hash;

	VTAILQ_ENTRY(backend)	list;
	int			refcount;
	pthread_mutex_t		mtx;

	struct sockaddr		*ipv4;
	socklen_t		ipv4len;
	struct sockaddr		*ipv6;
	socklen_t		ipv6len;

	VTAILQ_HEAD(, vbe_conn)	connlist;

	struct vbp_target	*probe;
	unsigned		healthy;
};

/* cache_backend.c */
void VBE_ReleaseConn(struct vbe_conn *vc);

/* cache_backend_cfg.c */
extern MTX VBE_mtx;
void VBE_DropRefLocked(struct backend *b);

/* cache_backend_poll.c */
void VBP_Start(struct backend *b, struct vrt_backend_probe const *p);
void VBP_Stop(struct backend *b);
