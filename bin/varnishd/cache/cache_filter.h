/*-
 * Copyright (c) 2013-2015 Varnish Software AS
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

struct req;
struct vfp_entry;
struct vfp_ctx;
struct vdp_ctx;
struct vdp_entry;

/* Fetch processors --------------------------------------------------*/

#define VFP_DEBUG(ctx, fmt, ...)					\
	do {								\
		if (!DO_DEBUG(DBG_PROCESSORS))				\
			break;						\
		VSLb((ctx)->wrk->vsl, SLT_Debug, "VFP:%s:%d: " fmt,	\
		    __func__, __LINE__, __VA_ARGS__);			\
	} while (0)

enum vfp_status {
	VFP_ERROR = -1,
	VFP_OK = 0,
	VFP_END = 1,
	VFP_NULL = 2,	// signal bypass, never returned by VFP_Suck()
};

typedef enum vfp_status vfp_init_f(VRT_CTX, struct vfp_ctx *,
    struct vfp_entry *);
typedef enum vfp_status
    vfp_pull_f(struct vfp_ctx *, struct vfp_entry *, void *ptr, ssize_t *len);
typedef void vfp_fini_f(struct vfp_ctx *, struct vfp_entry *);

struct vfp {
	const char		*name;
	vfp_init_f		*init;
	vfp_pull_f		*pull;
	vfp_fini_f		*fini;
	const void		*priv1;
};

struct vfp_entry {
	unsigned		magic;
#define VFP_ENTRY_MAGIC		0xbe32a027
	enum vfp_status		closed;
	const struct vfp	*vfp;
	void			*priv1;	// XXX ambiguous with priv1 in struct vfp
	ssize_t			priv2;
	VTAILQ_ENTRY(vfp_entry)	list;
	uint64_t		calls;
	uint64_t		bytes_out;
};

/*--------------------------------------------------------------------
 * VFP filter state
 */

VTAILQ_HEAD(vfp_entry_s, vfp_entry);

struct vfp_ctx {
	unsigned		magic;
#define VFP_CTX_MAGIC		0x61d9d3e5
	int			failed;
	struct http		*req;
	struct http		*resp;
	struct worker		*wrk;
	struct objcore		*oc; // Only first filter, if at all

	struct vfp_entry_s	vfp;
	struct vfp_entry	*vfp_nxt;
	unsigned		obj_flags;
};

enum vfp_status VFP_Suck(struct vfp_ctx *, void *p, ssize_t *lp);
enum vfp_status VFP_Error(struct vfp_ctx *, const char *fmt, ...)
    v_printflike_(2, 3);

void v_deprecated_ VRT_AddVFP(VRT_CTX, const struct vfp *);
void v_deprecated_ VRT_RemoveVFP(VRT_CTX, const struct vfp *);

/* Deliver processors ------------------------------------------------*/

enum vdp_action {
	VDP_NULL,		/* Input buffer valid after call */
	VDP_FLUSH,		/* Input buffer will be invalidated */
	VDP_END,		/* Last buffer or after, implies VDP_FLUSH */
};


typedef int vdp_init_f(VRT_CTX, struct vdp_ctx *, void **priv);
/*
 * Return value:
 *	negative:	Error - abandon delivery
 *	zero:		OK
 *	positive:	Don't push this VDP anyway
 */

typedef int vdp_fini_f(struct vdp_ctx *, void **priv);
typedef int vdp_bytes_f(struct vdp_ctx *, enum vdp_action, void **priv,
    const void *ptr, ssize_t len);

/*
 * ============================================================
 * vdpio io-vector interface
 */
typedef int vdpio_init_f(VRT_CTX, struct vdp_ctx *, void **priv, int capacity);
/*
 * the vdpio_init_f() are called front (object iterator) to back (consumer).
 *
 * each init function returns the minimum number of io vectors (vscarab
 * capacity) that it requires the next filter to accept. This capacity is
 * passed to the next init function such that it can allocate sufficient
 * space to fulfil the requirement of the previous filter.
 *
 * Return values:
 *   < 0 : Error
 *  == 0 ; NOOP, do not push this filter
 *  >= 1 : capacity requirement
 *
 * typedef is shared with upgrade
 */

typedef int vdpio_lease_f(struct vdp_ctx *, struct vdp_entry *, struct vscarab *scarab);
/*
 * vdpio_lease_f() returns leases provided by this filter layer in the vscarab
 * probided by the caller.
 *
 * called via vdpio_pull(): the last filter is called first by delivery. Each
 * filter calls the previous layer for leases. The first filter calls storage.
 *
 * return values are as for ObjVAIlease()
 *
 * Other notable differences to vdp_bytes_f:
 * - responsible for updating (struct vdp_entry).bytes_in and .calls
 *
 */

typedef void vdpio_fini_f(struct vdp_ctx *, void **priv);

struct vdp {
	const char		*name;
	vdp_init_f		*init;
	vdp_bytes_f		*bytes;
	vdp_fini_f		*fini;
	const void		*priv1;

	vdpio_init_f		*io_init;
	vdpio_init_f		*io_upgrade;
	vdpio_lease_f		*io_lease;
	vdpio_fini_f		*io_fini;
};

struct vdp_entry {
	unsigned		magic;
#define VDP_ENTRY_MAGIC		0x353eb781
	enum vdp_action		end;	// VDP_NULL or VDP_END
	const struct vdp	*vdp;
	void			*priv;
	VTAILQ_ENTRY(vdp_entry)	list;
	uint64_t		calls;
	uint64_t		bytes_in;
};

VTAILQ_HEAD(vdp_entry_s, vdp_entry);

struct vdp_ctx {
	unsigned		magic;
#define VDP_CTX_MAGIC		0xee501df7
	int			retval;		// vdpio: error or capacity
	uint64_t		bytes_done;	// not used with vdpio
	struct vdp_entry_s	vdp;
	struct vdp_entry	*nxt;		// not needed for vdpio
	struct worker		*wrk;
	struct vsl_log		*vsl;
	// NULL'ed after the first filter has been pushed
	struct objcore		*oc;
	// NULL'ed for delivery
	struct http		*hp;
	intmax_t		*clen;
	// only for vdpio
	vai_hdl			vai_hdl;
	struct vscaret		*scaret;
};

int VDP_bytes(struct vdp_ctx *, enum vdp_action act, const void *, ssize_t);

/*
 * vdpe == NULL: get lesaes from the last layer
 * vdpe != NULL: get leases from the previous layer or storage
 *
 * conversely to VDP_bytes, vdpio calls happen back (delivery) to front (storage)
 *
 * ends up in a tail call to the previous layer to save stack space
 */
static inline int
vdpio_pull(struct vdp_ctx *vdc, struct vdp_entry *vdpe, struct vscarab *scarab)
{

	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);

	if (vdpe == NULL)
		vdpe = VTAILQ_LAST(&vdc->vdp, vdp_entry_s);
	else {
		CHECK_OBJ(vdpe, VDP_ENTRY_MAGIC);
		vdpe = VTAILQ_PREV(vdpe, vdp_entry_s, list);
	}

	if (vdpe != NULL)
		return (vdpe->vdp->io_lease(vdc, vdpe, scarab));
	else
		return (ObjVAIlease(vdc->wrk, vdc->vai_hdl, scarab));
}

uint64_t VDPIO_Close1(struct vdp_ctx *, struct vdp_entry *vdpe);

/*
 * ============================================================
 * VDPIO helpers
 */

/*
 * l bytes have been written to buf. save these to out and checkpoint buf for
 * the remaining free space
 */
static inline void
iovec_collect(struct iovec *buf, struct iovec *out, size_t l)
{
	if (out->iov_base == NULL)
		out->iov_base = buf->iov_base;
	assert((char *)out->iov_base + out->iov_len == buf->iov_base);
	out->iov_len += l;
	buf->iov_base = (char *)buf->iov_base + l;
	buf->iov_len -= l;
}

/*
 * return a single lease via the vdc vscaret
 */
static inline
void vdpio_return_lease(const struct vdp_ctx *vdc, uint64_t lease)
{
	struct vscaret *scaret;

	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	scaret = vdc->scaret;
	VSCARET_CHECK_NOTNULL(scaret);

	if (scaret->used == scaret->capacity)
		ObjVAIreturn(vdc->wrk, vdc->vai_hdl, scaret);
	VSCARET_ADD(scaret, lease);
}

/*
 * add all leases from the vscarab to the vscaret
 */
static inline
void vdpio_return_vscarab(const struct vdp_ctx *vdc, struct vscarab *scarab)
{
	struct viov *v;

	VSCARAB_CHECK_NOTNULL(scarab);
	VSCARAB_FOREACH(v, scarab)
		vdpio_return_lease(vdc, v->lease);
	VSCARAB_INIT(scarab, scarab->capacity);
}

/*
 * return used up iovs (len == 0)
 * move remaining to the beginning of the scarab
 */
static inline void
vdpio_consolidate_vscarab(const struct vdp_ctx *vdc, struct vscarab *scarab)
{
	struct viov *v, *f = NULL;

	VSCARAB_CHECK_NOTNULL(scarab);
	VSCARAB_FOREACH(v, scarab) {
		if (v->iov.iov_len == 0) {
			AN(v->iov.iov_base);
			vdpio_return_lease(vdc, v->lease);
			if (f == NULL)
				f = v;
			continue;
		}
		else if (f == NULL)
			continue;
		memmove(f, v, scarab->used - (v - scarab->s) * sizeof (*v));
		break;
	}
	if (f != NULL)
		scarab->used = f - scarab->s;
}

// Lifecycle management in cache_deliver_proc.c
int VDPIO_Init(struct vdp_ctx *vdc, struct objcore *oc, struct ws *ws,
    vai_notify_cb *notify_cb, void *notify_priv, struct vscaret *scaret);
void VDPIO_Return(const struct vdp_ctx *vdc);
void VDPIO_Fini(struct vdp_ctx *vdc);

void v_deprecated_ VRT_AddVDP(VRT_CTX, const struct vdp *);
void v_deprecated_ VRT_RemoveVDP(VRT_CTX, const struct vdp *);

/* Registry functions -------------------------------------------------*/
const char *VRT_AddFilter(VRT_CTX, const struct vfp *, const struct vdp *);
void VRT_RemoveFilter(VRT_CTX, const struct vfp *, const struct vdp *);
