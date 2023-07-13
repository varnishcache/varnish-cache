/*
 * IF you wonder about the stupid contents of this file,
 * DO read tools/coccinelle/README.rst
 */

/* include/vdef.h */
#define v_printflike_(f,a)
#define v_deprecated_
#define v_dont_optimize
#define v_matchproto_(xxx)
#define v_statevariable_(varname)
#define v_unused_

/* include/vrt.h */
#define VRT_CTX const struct vrt_ctx *ctx

/* include/vqueue.h */
#define VTAILQ_ENTRY(t)		unsigned
#define VSTAILQ_ENTRY(t)	unsigned

#define VTAILQ_HEAD(n, t)	unsigned
#define VSTAILQ_HEAD(n, t)	unsigned

#define VTAILQ_HEAD_INITIALIZER(t)	0
#define VSTAILQ_HEAD_INITIALIZER(t)	0

/* include/vtree.h */
#define VRBT_ENTRY(x)		unsigned
#define VRBT_HEAD(x, y)		unsigned
#define VRBT_INITIALIZER(t)	0

/* lib/libvcc/vcc_vmod.c */
#define STANZA_TBL

/* bin/varnishd/common/heritage.h */
#define ASSERT_MGT() (void)0

/* bin/varnishd/cache/cache_transport.h */
#define TRANSPORTS

/* vmod/vcc_*_if.h */
#define VPFX(a)		vmod_##a
#define VARGS(a)	arg_vmod_foo_##a
