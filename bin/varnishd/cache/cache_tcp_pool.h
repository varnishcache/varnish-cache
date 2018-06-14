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
 * Outgoing TCP connection pools
 *
 */

struct tcp_pool;
struct pfd;
#define PFD_STATE_AVAIL		(1<<0)
#define PFD_STATE_USED		(1<<1)
#define PFD_STATE_STOLEN	(1<<2)
#define PFD_STATE_CLEANUP	(1<<3)

/*---------------------------------------------------------------------
 */

unsigned PFD_State(const struct pfd *);
int *PFD_Fd(struct pfd *);
void PFD_LocalName(const struct pfd *, char *, unsigned, char *, unsigned);
void PFD_RemoteName(const struct pfd *, char *, unsigned, char *, unsigned);

/*---------------------------------------------------------------------

 * Prototypes
 */

struct VSC_vbe;

struct tcp_pool *VTP_Ref(const struct suckaddr *ip4, const struct suckaddr *ip6,
    const char *uds, const void *id);
	/*
	 * Get a reference to a TCP pool. Either one or both of ip4 or
	 * ip6 arg must be non-NULL, or uds must be non-NULL. If recycling
	 * is to be used, the id pointer distinguishes the pool per
	 * protocol.
	 */

void VTP_AddRef(struct tcp_pool *);
	/*
	 * Get another reference to an already referenced TCP pool.
	 */

void VTP_Rel(struct tcp_pool **);
	/*
	 * Release reference to a TCP pool.  When last reference is released
	 * the pool is destroyed and all cached connections closed.
	 */

int VTP_Open(struct tcp_pool *, double tmo, const void **, int*);
	/*
	 * Open a new connection and return the adress used.
	 * errno will be returned in the last argument.
	 */

void VTP_Close(struct pfd **);
	/*
	 * Close a connection.
	 */

void VTP_Recycle(const struct worker *, struct pfd **);
	/*
	 * Recycle an open connection.
	 */

struct pfd *VTP_Get(struct tcp_pool *, double tmo, struct worker *,
    unsigned force_fresh, int *err);
	/*
	 * Get a (possibly) recycled connection.
	 * errno will be stored in err
	 */

int VTP_Wait(struct worker *, struct pfd *, double tmo);
	/*
	 * If the connection was recycled (state != VTP_STATE_USED) call this
	 * function before attempting to receive on the connection.
	 */

const struct suckaddr *VTP_getip(struct pfd *);

