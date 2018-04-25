/*-
 * Copyright (c) 2013-2015 Varnish Software AS
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
 */

struct req;
struct vfp_entry;
struct vfp_ctx;

/* Fetch processors --------------------------------------------------*/

enum vfp_status {
	VFP_ERROR = -1,
	VFP_OK = 0,
	VFP_END = 1,
	VFP_NULL = 2,
};

typedef enum vfp_status vfp_init_f(struct vfp_ctx *, struct vfp_entry *);
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
	const struct vfp	*vfp;
	void			*priv1;
	intptr_t		priv2;
	enum vfp_status		closed;
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
	struct objcore		*oc;

	struct vfp_entry_s	vfp;
	struct vfp_entry	*vfp_nxt;
	unsigned		obj_flags;
};

enum vfp_status VFP_Suck(struct vfp_ctx *, void *p, ssize_t *lp);
enum vfp_status VFP_Error(struct vfp_ctx *, const char *fmt, ...)
    v_printflike_(2, 3);
void VRT_AddVFP(VRT_CTX, const struct vfp *);
void VRT_RemoveVFP(VRT_CTX, const struct vfp *);

/* Deliver processors ------------------------------------------------*/

enum vdp_status {
	VDP_ERROR_FETCH	= -3,	/* Streaming fetch failure */
	VDP_ERROR_IO	= -2,	/* Socket error (ie remote close) */
	VDP_ERROR	= -1,	/* VDP error */
	VDP_OK		= 0,
	VDP_BREAK	= 1,	/* Stop processing (no error) */
};

enum vdp_flush {
	VDP_NULL,		/* Input buffer valid after call */
	VDP_FLUSH,		/* Input buffer will be invalidated */
	VDP_LAST,		/* Implies flush, this is the last chunk */
};

typedef int vdp_init_f(struct req *, void **priv); /* Called on VDP_push() */
/* The init callback is called at the time VDP_push is called.
 *
 * It should return VDP_OK on success. The fini function will be called if
 * defined.
 *
 * It may return VDP_ERROR if it fails the initialization. This causes a
 * canned 500 reply to be sent to the client. The fini function will not
 * be called. */

typedef void vdp_fini_f(struct req *, void **priv); /* Called on VDP_close() */

typedef int vdp_bytes_f(struct req *, enum vdp_flush, void **priv,
    const void *ptr, ssize_t len);
/* The bytes callback is called for each chunk of data to be processed. To
 * pass data down to the next layer, call VDP_Bytes().
 *
 * The flush value determines how to treat the data being processed.
 *
 * If flush==VDP_NULL, the data will not be invalidated upon return from
 * the callback function, and the pointers may be kept at any layer.
 *
 * If flush==VDP_FLUSH, the data will become invalid upon returning from
 * the callback function. The layer needs to buffer it, or pass it down to
 * the next layer, making sure to call VDP_bytes with flush>=VDP_FLUSH.
 *
 * If flush==VDP_LAST, this is the very last chunk being processed. This
 * implies VDP_FLUSH. The function needs to call down **once and only
 * once** to the next layer with flush==VDP_LAST. If the processing causes
 * the need for multiple chunks to be passed on to the next layer, only
 * the final chunk should have flush==VDP_LAST.
 */

struct vdp {
	const char		*name;
	vdp_init_f		*init;
	vdp_fini_f		*fini;
	vdp_bytes_f		*bytes;
};

struct vdp_entry {
	unsigned		magic;
#define VDP_ENTRY_MAGIC		0x353eb781
	unsigned		f_seen_last:1;
	const struct vdp	*vdp;
	void			*priv;
	VTAILQ_ENTRY(vdp_entry)	list;
};

VTAILQ_HEAD(vdp_entry_s, vdp_entry);

struct vdp_ctx {
	unsigned		magic;
#define VDP_CTX_MAGIC		0xee501df7
	struct vdp_entry_s	vdp;
	struct vdp_entry	*nxt;
	int			status;
};

int VDP_bytes(struct req *, enum vdp_flush flush, const void *ptr,
    ssize_t len);
int VDP_push(struct req *, const struct vdp *, void *priv,
    int bottom);
