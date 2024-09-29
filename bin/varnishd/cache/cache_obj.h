/*-
 * Copyright (c) 2015-2016 Varnish Software AS
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

/* Methods on objcore ------------------------------------------------*/

typedef void objfree_f(struct worker *, struct objcore *);

typedef void objsetstate_f(struct worker *, const struct objcore *,
    enum boc_state_e);

typedef int objiterator_f(struct worker *, struct objcore *,
    void *priv, objiterate_f *func, int final);
typedef int objgetspace_f(struct worker *, struct objcore *,
     ssize_t *sz, uint8_t **ptr);
typedef void objextend_f(struct worker *, struct objcore *, ssize_t l);
typedef void objtrimstore_f(struct worker *, struct objcore *);
typedef void objbocdone_f(struct worker *, struct objcore *, struct boc *);
typedef void objslim_f(struct worker *, struct objcore *);
typedef const void *objgetattr_f(struct worker *, struct objcore *,
    enum obj_attr attr, ssize_t *len);
typedef void *objsetattr_f(struct worker *, struct objcore *,
    enum obj_attr attr, ssize_t len, const void *ptr);
typedef void objtouch_f(struct worker *, struct objcore *, vtim_real now);

/* called by Obj/storage to notify that the lease function (vai_lease_f) can be
 * called again after a -EAGAIN / -ENOBUFS return value
 * NOTE:
 * - the callback gets executed by an arbitrary thread
 * - WITH the boc mtx held
 * so it should never block and be efficient
 */

/* notify entry added to struct boc::vai_q_head */
struct vai_qe {
	unsigned		magic;
#define VAI_Q_MAGIC		0x573e27eb
	unsigned		flags;
#define VAI_QF_INQUEUE		(1U<<0)
	VSLIST_ENTRY(vai_qe)	list;
	vai_notify_cb		*cb;
	vai_hdl			hdl;
	void			*priv;
};

struct obj_methods {
	/* required */
	objfree_f	*objfree;
	objiterator_f	*objiterator;
	objgetspace_f	*objgetspace;
	objextend_f	*objextend;
	objgetattr_f	*objgetattr;
	objsetattr_f	*objsetattr;
	/* optional */
	objtrimstore_f	*objtrimstore;
	objbocdone_f	*objbocdone;
	objslim_f	*objslim;
	objtouch_f	*objtouch;
	objsetstate_f	*objsetstate;
};

