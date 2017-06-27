/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 * Stuff that should *never* be exposed to a VMOD
 */

/*--------------------------------------------------------------------
 * A transport is how we talk HTTP for a given request.
 * This is different from a protocol because ESI child requests have
 * their own "protocol" to talk to the parent ESI request, which may
 * or may not, be talking a "real" HTTP protocol itself.
 */

typedef void vtr_deliver_f (struct req *, struct busyobj *, int sendbody);

struct transport {
	unsigned		magic;
#define TRANSPORT_MAGIC		0xf157f32f
	vtr_deliver_f		*deliver;
};

/* Prototypes etc ----------------------------------------------------*/

/* cache_acceptor.c */
void VCA_Init(void);
void VCA_Shutdown(void);

/* cache_backend_cfg.c */
void VBE_InitCfg(void);
void VBE_Poll(void);

/* cache_backend_tcp.c */
void VBT_Init(void);

/* cache_backend_poll.c */
void VBP_Init(void);

/* == cache_ban.c == */

/* From cache_main.c */
void BAN_Init(void);
void BAN_Compile(void);
void BAN_Shutdown(void);

/* From cache_hash.c */
void BAN_NewObjCore(struct objcore *oc);
void BAN_DestroyObj(struct objcore *oc);
int BAN_CheckObject(struct worker *, struct objcore *, struct req *);

/* cache_busyobj.c */
void VBO_Init(void);

/* cache_cli.c [CLI] */
void CLI_Init(void);
void CLI_Run(void);
void CLI_AddFuncs(struct cli_proto *p);

/* cache_expire.c */
void EXP_Init(void);

/* cache_fetch_proc.c */
void VFP_Init(void);

/* cache_http.c */
void HTTP_Init(void);

/* cache_main.c */
void THR_SetName(const char *name);
const char* THR_GetName(void);
void THR_SetBusyobj(const struct busyobj *);
struct busyobj * THR_GetBusyobj(void);
void THR_SetRequest(const struct req *);
struct req * THR_GetRequest(void);

/* cache_lck.c */
void LCK_Init(void);

/* cache_panic.c */
void PAN_Init(void);

/* cache_pool.c */
void Pool_Init(void);

/* cache_proxy.c [VPX] */
task_func_t VPX_Proto_Sess;

/* cache_shmlog.c */
void VSM_Init(void);
void VSL_Setup(struct vsl_log *vsl, void *ptr, size_t len);
void VSL_ChgId(struct vsl_log *vsl, const char *typ, const char *why,
    uint32_t vxid);
void VSL_End(struct vsl_log *vsl);

/* cache_vcl.c */
struct director *VCL_DefaultDirector(const struct vcl *);
const struct vrt_backend_probe *VCL_DefaultProbe(const struct vcl *);
void VCL_Init(void);
void VCL_Panic(struct vsb *, const struct vcl *);
void VCL_Poll(void);

/* cache_vrt.c */
void VRTPRIV_init(struct vrt_privs *privs);
void VRTPRIV_dynamic_kill(struct vrt_privs *privs, uintptr_t id);

/* cache_vrt_vmod.c */
void VMOD_Init(void);

/* storage_persistent.c */
void SMP_Init(void);
void SMP_Ready(void);

