/*-
 * Copyright 2016 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Author: Nils Goroll <nils.goroll@uplex.de>
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
 */

VCL_BOOL shardcfg_add_backend(VRT_CTX, struct vmod_priv *priv,
    const struct sharddir *shardd, VCL_BACKEND be, VCL_STRING ident,
    VCL_DURATION rampup);
VCL_BOOL shardcfg_remove_backend(VRT_CTX, struct vmod_priv *priv,
    const struct sharddir *shardd, VCL_BACKEND be, VCL_STRING ident);
VCL_BOOL shardcfg_clear(VRT_CTX, struct vmod_priv *priv,
    const struct sharddir *shardd);
VCL_BOOL shardcfg_reconfigure(VRT_CTX, struct vmod_priv *priv,
    struct sharddir *shardd, VCL_INT replicas);
VCL_VOID shardcfg_set_warmup(struct sharddir *shardd, VCL_REAL ratio);
VCL_VOID shardcfg_set_rampup(struct sharddir *shardd,
    VCL_DURATION duration);
