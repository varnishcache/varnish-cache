/*
 * This patch was used to introduce struct wrk_vpi once
 *
 * Retained for reference only
 */
using "varnish.iso"

@@
idexpression struct worker *wrk;
@@

- wrk->handling
+ wrk->vpi->handling

@@
idexpression struct vrt_ctx *ctx;
expression reqbo;
@@

- ctx->handling = &reqbo->wrk->handling;
+ ctx->vpi = reqbo->wrk->vpi;

@@
idexpression struct vrt_ctx *ctx;
expression X;
@@

- *ctx->handling = X
+ ctx->vpi->handling = X

/*
 * not using idexpression for ctx behind this point
 * because I failed to teach coccinelle VRT_CTX
 * (which is not just an iso, but rather a macro
 * also used in the argument list
 */
@@
identifier F;
@@

- F(ctx->handling)
+ F(ctx->vpi)

@@
identifier F;
@@

- F(*ctx->handling)
+ F(ctx->vpi->handling)

@@
@@

- *ctx->handling
+ ctx->vpi->handling
