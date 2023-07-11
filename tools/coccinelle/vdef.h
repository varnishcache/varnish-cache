/*
 * IF you wonder about the stupid contents of this file,
 * DO read tools/coccinelle/README.rst
 */

/* vdef.h */
#define v_printflike_(f,a)
#define v_deprecated_
#define v_dont_optimize
#define v_matchproto_(xxx)
#define v_statevariable_(varname)
#define v_unused_

/* vrt.h */
#define VRT_CTX	const struct vrt_ctx *ctx

/* vcc_if.h */
#define VPFX(a)	vmod_##a

/* vqueue.h */
#define VTAILQ_ENTRY(x) unsigned
#define VSTAILQ_ENTRY(x) unsigned
#define VTAILQ_HEAD(x, y) unsigned
#define VSTAILQ_HEAD(x, y) unsigned
#define VRBT_ENTRY(x) unsigned
#define VRBT_HEAD(x, y) unsigned

/* lib/libvcc/vcc_vmod.c */
#define STANZA_TBL
