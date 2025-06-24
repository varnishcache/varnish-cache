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

/* called by Obj/storage to notify that the lease function (vai_lease_f) or
 * buffer function (vai_buffer_f) can be called again after return of
 * -EAGAIN or -ENOBUFS
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

#define VAI_ASSERT_LEASE(x) AZ((x) & 0x7)

/*
 * start an iteration. the ws can we used (reserved) by storage
 * the void * will be passed as the second argument to vai_notify_cb
 */
typedef vai_hdl vai_init_f(struct worker *, struct objcore *, struct ws *,
	vai_notify_cb *, void *);

/*
 * lease io vectors from storage
 *
 * vai_hdl is from vai_init_f
 * the vscarab is provided by the caller to return leases
 *
 * return:
 * -EAGAIN:	nothing available at the moment, storage will notify, no use to
 *		call again until notification
 * -ENOBUFS:	caller needs to return leases, storage will notify
 * -EPIPE:	BOS_FAILED for busy object
 * -(errno):	other problem, fatal
 *  >= 0:	number of viovs added
 */
typedef int vai_lease_f(struct worker *, vai_hdl, struct vscarab *);

/*
 * get io vectors with temporary buffers from storage
 *
 * vai_hdl is from vai_init_f
 * the vscarab needs to be initialized with the number of requested elements
 * and each iov.iov_len contings the requested sizes. all iov_base need to be
 * zero.
 *
 * after return, the vscarab can be smaller than requested if only some
 * allocation requests could be fulfilled
 *
 * return:
 * -EAGAIN:	allocation can not be fulfilled immediately, storage will notify,
 *		no use to call again until notification
 * -(errno):	other problem, fatal
 *  n:		n > 0, number of viovs filled
 */
typedef int vai_buffer_f(struct worker *, vai_hdl, struct vscarab *);

/*
 * return leases from vai_lease_f or vai_buffer_f
 */
typedef void vai_return_f(struct worker *, vai_hdl, struct vscaret *);

/*
 * finish iteration, vai_return_f must have been called on all leases
 */
typedef void vai_fini_f(struct worker *, vai_hdl *);

/*
 * vai_hdl must start with this preamble such that when cast to it, cache_obj.c
 * has access to the methods.
 *
 * The first magic is owned by storage, the second magic is owned by cache_obj.c
 * and must be initialized to VAI_HDL_PREAMBLE_MAGIC2
 *
 */

//lint -esym(768, vai_hdl_preamble::reserve)
struct vai_hdl_preamble {
	unsigned	magic;	// owned by storage
	unsigned	magic2;
#define VAI_HDL_PREAMBLE_MAGIC2	0x7a15d162
	vai_lease_f	*vai_lease;
	vai_buffer_f	*vai_buffer;
	vai_return_f	*vai_return;
	uintptr_t	reserve[4];	// abi fwd compat
	vai_fini_f	*vai_fini;
};

#define INIT_VAI_HDL(to, x) do {				\
	(void)memset(to, 0, sizeof *(to));			\
	(to)->preamble.magic = (x);				\
	(to)->preamble.magic2 = VAI_HDL_PREAMBLE_MAGIC2;	\
} while (0)

#define CHECK_VAI_HDL(obj, x) do {				\
	assert(obj->preamble.magic == (x));			\
	assert(obj->preamble.magic2 == VAI_HDL_PREAMBLE_MAGIC2);\
} while (0)

#define CAST_VAI_HDL_NOTNULL(obj, ptr, x) do {			\
	AN(ptr);						\
	(obj) = (ptr);						\
	CHECK_VAI_HDL(obj, x);					\
} while (0)

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
	/* async iteration (VAI) */
	vai_init_f	*vai_init;
};
