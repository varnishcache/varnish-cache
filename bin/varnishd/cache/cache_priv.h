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

#include "VSC_main.h"

struct vfp;

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

/* cache_exp.c */
double EXP_Ttl(const struct req *, const struct objcore *);
void EXP_Insert(struct worker *wrk, struct objcore *oc);
void EXP_Remove(struct objcore *);

#define EXP_Dttl(req, oc) (oc->ttl - (req->t_req - oc->t_origin))

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

/* cache_deliver_proc.c */
void VDP_close(struct req *req);
int VDP_DeliverObj(struct req *req);

extern const struct vdp VDP_gunzip;
extern const struct vdp VDP_esi;

/* cache_expire.c */
void EXP_Init(void);

/* cache_fetch_proc.c */
void VFP_Init(void);
enum vfp_status VFP_GetStorage(struct vfp_ctx *, ssize_t *sz, uint8_t **ptr);
void VFP_Extend(const struct vfp_ctx *, ssize_t sz);
struct vfp_entry *VFP_Push(struct vfp_ctx *, const struct vfp *);
void VFP_Setup(struct vfp_ctx *vc);
int VFP_Open(struct vfp_ctx *bo);
void VFP_Close(struct vfp_ctx *bo);

extern const struct vfp VFP_gunzip;
extern const struct vfp VFP_gzip;
extern const struct vfp VFP_testgunzip;
extern const struct vfp VFP_esi;
extern const struct vfp VFP_esi_gzip;

/* cache_http.c */
void HTTP_Init(void);

/* cache_main.c */
void THR_SetName(const char *name);
const char* THR_GetName(void);
void THR_SetBusyobj(const struct busyobj *);
struct busyobj * THR_GetBusyobj(void);
void THR_SetRequest(const struct req *);
struct req * THR_GetRequest(void);
void THR_Init(void);

/* cache_lck.c */
void LCK_Init(void);

/* cache_obj.c */
void ObjInit(void);

/* cache_panic.c */
void PAN_Init(void);
int PAN_already(struct vsb *, const void *);

/* cache_pool.c */
void Pool_Init(void);
int Pool_Task(struct pool *pp, struct pool_task *task, enum task_prio prio);
int Pool_Task_Arg(struct worker *, enum task_prio, task_func_t *,
    const void *arg, size_t arg_len);
void Pool_Sumstat(const struct worker *w);
int Pool_TrySumstat(const struct worker *wrk);
void Pool_PurgeStat(unsigned nobj);
int Pool_Task_Any(struct pool_task *task, enum task_prio prio);

/* cache_proxy.c [VPX] */
task_func_t VPX_Proto_Sess;

/* cache_range.c [VRG] */
void VRG_dorange(struct req *req, const char *r);

/* cache_session.c */
void SES_NewPool(struct pool *, unsigned pool_no);
void SES_DestroyPool(struct pool *);

/* cache_shmlog.c */
extern struct VSC_main *VSC_C_main;
void VSM_Init(void);
void VSL_Setup(struct vsl_log *vsl, void *ptr, size_t len);
void VSL_ChgId(struct vsl_log *vsl, const char *typ, const char *why,
    uint32_t vxid);
void VSL_End(struct vsl_log *vsl);

/* cache_vary.c */
int VRY_Create(struct busyobj *bo, struct vsb **psb);
int VRY_Match(struct req *, const uint8_t *vary);
void VRY_Prep(struct req *);
void VRY_Clear(struct req *);
enum vry_finish_flag { KEEP, DISCARD };
void VRY_Finish(struct req *req, enum vry_finish_flag);

/* cache_vcl.c */
struct director *VCL_DefaultDirector(const struct vcl *);
const struct vrt_backend_probe *VCL_DefaultProbe(const struct vcl *);
void VCL_Init(void);
void VCL_Panic(struct vsb *, const struct vcl *);
void VCL_Poll(void);
void VCL_Ref(struct vcl *);
void VCL_Refresh(struct vcl **);
void VCL_Rel(struct vcl **);
const char *VCL_Return_Name(unsigned);


/* cache_vrt.c */
void VRTPRIV_init(struct vrt_privs *privs);
void VRTPRIV_dynamic_kill(struct vrt_privs *privs, uintptr_t id);
void pan_privs(struct vsb *, const struct vrt_privs *);

/* cache_vrt_vmod.c */
void VMOD_Init(void);
void VMOD_Panic(struct vsb *);

/* http1/cache_http1_pipe.c */
void V1P_Init(void);

/* cache_http2_deliver.c */
void V2D_Init(void);

/* stevedore.c */
void STV_open(void);
void STV_close(void);
const struct stevedore *STV_find(const char *);
const struct stevedore *STV_next(void);
int STV_BanInfoDrop(const uint8_t *ban, unsigned len);
int STV_BanInfoNew(const uint8_t *ban, unsigned len);
void STV_BanExport(const uint8_t *banlist, unsigned len);

/* storage_persistent.c */
void SMP_Ready(void);

