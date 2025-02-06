/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 * Outgoing TCP|UDS connection pools
 *
 */

struct conn_pool;
struct pfd;

#define PFD_STATE_AVAIL		(1<<0)
#define PFD_STATE_USED		(1<<1)
#define PFD_STATE_STOLEN	(1<<2)
#define PFD_STATE_CLEANUP	(1<<3)

/*---------------------------------------------------------------------
 */

unsigned PFD_State(const struct pfd *);
int *PFD_Fd(struct pfd *);
vtim_dur PFD_Age(const struct pfd *);
uint64_t PFD_Reused(const struct pfd *);
void PFD_LocalName(const struct pfd *, char *, unsigned, char *, unsigned);
void PFD_RemoteName(const struct pfd *, char *, unsigned, char *, unsigned);

/*---------------------------------------------------------------------
 * Prototypes
 */

struct conn_pool *VCP_Ref(const struct vrt_endpoint *, const char *ident);
	/*
	 * Get a reference to a connection pool. Either one or both of ipv4 or
	 * ipv6 arg must be non-NULL, or uds must be non-NULL. If recycling
	 * is to be used, the ident pointer distinguishes the pool from
	 * other pools with same {ipv4, ipv6, uds}.
	 */

void VCP_AddRef(struct conn_pool *);
	/*
	 * Get another reference to an already referenced connection pool.
	 */

void VCP_Rel(struct conn_pool **);
	/*
	 * Release reference to a connection pool.  When last reference
	 * is released the pool is destroyed and all cached connections
	 * closed.
	 */

int VCP_Open(struct conn_pool *, vtim_dur tmo, VCL_IP *, int*);
	/*
	 * Open a new connection and return the address used.
	 * errno will be returned in the last argument.
	 */

void VCP_Close(struct pfd **);
	/*
	 * Close a connection.
	 */

void VCP_Recycle(const struct worker *, struct pfd **);
	/*
	 * Recycle an open connection.
	 */

struct pfd *VCP_Get(struct conn_pool *, vtim_dur tmo, struct worker *,
    unsigned force_fresh, int *err);
	/*
	 * Get a (possibly) recycled connection.
	 * errno will be stored in err
	 */

int VCP_Wait(struct worker *, struct pfd *, vtim_real tmo);
	/*
	 * If the connection was recycled (state != VCP_STATE_USED) call this
	 * function before attempting to receive on the connection.
	 */

VCL_IP VCP_GetIp(struct pfd *);

