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

struct busyobj;
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
	const char	*name;
	vfp_init_f	*init;
	vfp_pull_f	*pull;
	vfp_fini_f	*fini;
	const void	*priv1;
	intptr_t	priv2;
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


extern const struct vfp vfp_gunzip;
extern const struct vfp vfp_gzip;
extern const struct vfp vfp_testgunzip;
extern const struct vfp vfp_esi;
extern const struct vfp vfp_esi_gzip;

struct vfp_entry *VFP_Push(struct vfp_ctx *, const struct vfp *, int top);
void VFP_Setup(struct vfp_ctx *vc);
int VFP_Open(struct vfp_ctx *bo);
void VFP_Close(struct vfp_ctx *bo);
enum vfp_status VFP_Suck(struct vfp_ctx *, void *p, ssize_t *lp);
enum vfp_status VFP_Error(struct vfp_ctx *, const char *fmt, ...)
    __v_printflike(2, 3);

/* cache_fetch_proc.c */
enum vfp_status VFP_GetStorage(struct vfp_ctx *, ssize_t *sz, uint8_t **ptr);

/* Deliver processors ------------------------------------------------*/

enum vdp_action {
	VDP_INIT,		/* Happens on VDP_push() */
	VDP_FINI,		/* Happens on VDP_pop() */
	VDP_NULL,		/* Input buffer valid after call */
	VDP_FLUSH,		/* Input buffer will be invalidated */
};

typedef int vdp_bytes(struct req *, enum vdp_action, void **priv,
    const void *ptr, ssize_t len);

struct vdp_entry {
	unsigned		magic;
#define VDP_ENTRY_MAGIC		0x353eb781
	vdp_bytes		*func;
	void			*priv;
	VTAILQ_ENTRY(vdp_entry)	list;
};

int VDP_bytes(struct req *, enum vdp_action act, const void *ptr, ssize_t len);
void VDP_push(struct req *, vdp_bytes *func, void *priv, int bottom);
void VDP_close(struct req *req);
enum objiter_status VDP_DeliverObj(struct req *req);

vdp_bytes VDP_gunzip;
vdp_bytes VDP_ESI;
