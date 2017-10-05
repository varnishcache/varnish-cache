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
 * TCP connection pools
 *
 */

struct tcp_pool;

struct vtp {
	unsigned		magic;
#define VTP_MAGIC		0x0c5e6592
	int			fd;
	VTAILQ_ENTRY(vtp)	list;
	const struct suckaddr	*addr;
	uint8_t			state;
#define VTP_STATE_AVAIL		(1<<0)
#define VTP_STATE_USED		(1<<1)
#define VTP_STATE_STOLEN	(1<<2)
#define VTP_STATE_CLEANUP	(1<<3)
	struct waited		waited[1];
	struct tcp_pool		*tcp_pool;

	pthread_cond_t		*cond;
};

/*---------------------------------------------------------------------
 * Prototypes
 */

/* cache_tcp_pool.c */
struct tcp_pool *VTP_Ref(const struct suckaddr *ip4, const struct suckaddr *ip6);
void VTP_AddRef(struct tcp_pool *);
void VTP_Rel(struct tcp_pool **tpp);
int VTP_Open(const struct tcp_pool *tp, double tmo, const struct suckaddr **sa);
void VTP_Recycle(const struct worker *, struct tcp_pool *, struct vtp **);
void VTP_Close(struct tcp_pool *tp, struct vtp **);
struct vtp *VTP_Get(struct tcp_pool *, double tmo, struct worker *);
void VTP_Wait(struct worker *, struct vtp *);
