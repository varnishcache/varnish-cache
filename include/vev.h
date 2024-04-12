/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Varnish Software AS
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
 */

struct vev;
struct vev_root;

typedef int vev_cb_f(const struct vev *, int what);

struct vev {
	unsigned		magic;
#define VEV_MAGIC		0x46bbd419

	/* pub */
	const char		*name;
	int			fd;
	unsigned		fd_flags;
	unsigned		fd_events;
#define		VEV__RD		POLLIN
#define		VEV__WR		POLLOUT
#define		VEV__ERR	POLLERR
#define		VEV__HUP	POLLHUP
#define		VEV__SIG	-1
	int			sig;
	unsigned		sig_flags;
	double			timeout;
	vev_cb_f		*callback;
	siginfo_t		*siginfo;
	void			*priv;

	/* priv */
	double			__when;
	unsigned		__binheap_idx;
	unsigned		__privflags;
	struct vev_root		*__vevb;
};

struct vev_root *VEV_New(void);
void VEV_Destroy(struct vev_root **);

struct vev *VEV_Alloc(void);

int VEV_Start(struct vev_root *, struct vev *);
void VEV_Stop(struct vev_root *, struct vev *);

int VEV_Once(struct vev_root *);
int VEV_Loop(struct vev_root *);
