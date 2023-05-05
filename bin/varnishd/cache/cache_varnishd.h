/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 * Stuff that should *never* be exposed to a VMOD
 */

#include "cache.h"

#include "vsb.h"

#include <sys/socket.h>

#include <string.h>
#include <limits.h>
#include <unistd.h>

#include "common/common_param.h"

#ifdef NOT_IN_A_VMOD
#  include "VSC_main.h"
#endif

/*--------------------------------------------------------------------*/

struct vfp;
struct vdp;
struct cli_proto;
struct poolparam;

/*--------------------------------------------------------------------*/

typedef enum req_fsm_nxt req_state_f(struct worker *, struct req *);
struct req_step {
	const char	*name;
	req_state_f	*func;
};

extern const struct req_step R_STP_TRANSPORT[1];
extern const struct req_step R_STP_RECV[1];

struct vxid_pool {
	uint64_t		next;
	uint32_t		count;
};

/*--------------------------------------------------------------------
 * Private part of worker threads
 */

struct worker_priv {
	unsigned		magic;
#define WORKER_PRIV_MAGIC	0x3047db99
	struct objhead		*nobjhead;
	struct objcore		*nobjcore;
	void			*nhashpriv;
	struct vxid_pool	vxid_pool[1];
	struct vcl		*vcl;
};

/*--------------------------------------------------------------------
 * HTTP Protocol connection structure
 *
 * This is the protocol independent object for a HTTP connection, used
 * both for backend and client sides.
 *
 */

struct http_conn {
	unsigned		magic;
#define HTTP_CONN_MAGIC		0x3e19edd1

	int			*rfd;
	stream_close_t		doclose;
	body_status_t		body_status;
	struct ws		*ws;
	char			*rxbuf_b;
	char			*rxbuf_e;
	char			*pipeline_b;
	char			*pipeline_e;
	ssize_t			content_length;
	void			*priv;

	/* Timeouts */
	vtim_dur		first_byte_timeout;
	vtim_dur		between_bytes_timeout;
};

enum htc_status_e {
#define HTC_STATUS(e, n, s, l) HTC_S_ ## e = n,
#include "tbl/htc.h"
};

typedef enum htc_status_e htc_complete_f(struct http_conn *);

/* -------------------------------------------------------------------*/

extern volatile struct params * cache_param;

/* -------------------------------------------------------------------
 * The VCF facility is deliberately undocumented, use at your peril.
 */

struct vcf_return {
	const char		*name;
};

#define VCF_RETURNS()	\
		VCF_RETURN(CONTINUE) \
		VCF_RETURN(DEFAULT) \
		VCF_RETURN(MISS) \
		VCF_RETURN(HIT)

#define VCF_RETURN(x) extern const struct vcf_return VCF_##x[1];
VCF_RETURNS()
#undef VCF_RETURN

typedef const struct vcf_return *vcf_func_f(
	struct req *req,
	struct objcore **oc,
	struct objcore **oc_exp,
	int state);

struct vcf {
	unsigned		magic;
#define VCF_MAGIC		0x183285d1
	vcf_func_f		*func;
	void			*priv;
};

/* Prototypes etc ----------------------------------------------------*/

/* cache_acceptor.c */
void VCA_Init(void);
void VCA_Shutdown(void);

/* cache_backend_cfg.c */
void VBE_InitCfg(void);

void VBP_Init(void);

/* cache_ban.c */

/* for stevedoes resurrecting bans */
void BAN_Hold(void);
void BAN_Release(void);
void BAN_Reload(const uint8_t *ban, unsigned len);
struct ban *BAN_FindBan(vtim_real t0);
void BAN_RefBan(struct objcore *oc, struct ban *);
vtim_real BAN_Time(const struct ban *ban);

/* cache_busyobj.c */
struct busyobj *VBO_GetBusyObj(const struct worker *, const struct req *);
void VBO_ReleaseBusyObj(struct worker *wrk, struct busyobj **busyobj);

/* cache_director.c */
int VDI_GetHdr(struct busyobj *);
VCL_IP VDI_GetIP(struct busyobj *);
void VDI_Finish(struct busyobj *bo);
stream_close_t VDI_Http1Pipe(struct req *, struct busyobj *);
void VDI_Panic(const struct director *, struct vsb *, const char *nm);
void VDI_Event(const struct director *d, enum vcl_event_e ev);
void VDI_Init(void);

/* cache_deliver_proc.c */
void VDP_Init(struct vdp_ctx *vdx, struct worker *wrk, struct vsl_log *vsl,
    struct req *req);
uint64_t VDP_Close(struct vdp_ctx *, struct objcore *, struct boc *);
void VDP_Panic(struct vsb *vsb, const struct vdp_ctx *vdc);
int VDP_Push(VRT_CTX, struct vdp_ctx *, struct ws *, const struct vdp *,
    void *priv);
int VDP_DeliverObj(struct vdp_ctx *vdc, struct objcore *oc);
extern const struct vdp VDP_gunzip;
extern const struct vdp VDP_esi;
extern const struct vdp VDP_range;


/* cache_exp.c */
vtim_real EXP_Ttl(const struct req *, const struct objcore *);
vtim_real EXP_Ttl_grace(const struct req *, const struct objcore *oc);
void EXP_RefNewObjcore(struct objcore *);
void EXP_Insert(struct worker *wrk, struct objcore *oc);
void EXP_Remove(struct objcore *);

#define EXP_Dttl(req, oc) (oc->ttl - (req->t_req - oc->t_origin))

/* cache_expire.c */

/*
 * The set of variables which control object expiry are inconveniently
 * 24 bytes long (double+3*float) and this causes alignment waste if
 * we put then in a struct.
 * These three macros operate on the struct we don't use.
 */

#define EXP_ZERO(xx)							\
	do {								\
		(xx)->t_origin = 0.0;					\
		(xx)->ttl = 0.0;					\
		(xx)->grace = 0.0;					\
		(xx)->keep = 0.0;					\
	} while (0)

#define EXP_COPY(to,fm)							\
	do {								\
		(to)->t_origin = (fm)->t_origin;			\
		(to)->ttl = (fm)->ttl;					\
		(to)->grace = (fm)->grace;				\
		(to)->keep = (fm)->keep;				\
	} while (0)

#define EXP_WHEN(to)							\
	((to)->t_origin + (to)->ttl + (to)->grace + (to)->keep)

/* cache_exp.c */
void EXP_Rearm(struct objcore *oc, vtim_real now,
    vtim_dur ttl, vtim_dur grace, vtim_dur keep);

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
void EXP_Shutdown(void);

/* cache_fetch.c */
enum vbf_fetch_mode_e {
	VBF_NORMAL = 0,
	VBF_PASS = 1,
	VBF_BACKGROUND = 2,
};
void VBF_Fetch(struct worker *wrk, struct req *req,
    struct objcore *oc, struct objcore *oldoc, enum vbf_fetch_mode_e);
const char *VBF_Get_Filter_List(struct busyobj *);
void Bereq_Rollback(VRT_CTX);

/* cache_fetch_proc.c */
void VFP_Init(void);
struct vfp_entry *VFP_Push(struct vfp_ctx *, const struct vfp *);
enum vfp_status VFP_GetStorage(struct vfp_ctx *, ssize_t *sz, uint8_t **ptr);
void VFP_Extend(const struct vfp_ctx *, ssize_t sz, enum vfp_status);
void VFP_Setup(struct vfp_ctx *vc, struct worker *wrk);
int VFP_Open(VRT_CTX, struct vfp_ctx *);
uint64_t VFP_Close(struct vfp_ctx *);

extern const struct vfp VFP_gunzip;
extern const struct vfp VFP_gzip;
extern const struct vfp VFP_testgunzip;
extern const struct vfp VFP_esi;
extern const struct vfp VFP_esi_gzip;

/* cache_http.c */
void HTTP_Init(void);

/* cache_http1_proto.c */

htc_complete_f HTTP1_Complete;
uint16_t HTTP1_DissectRequest(struct http_conn *, struct http *);
uint16_t HTTP1_DissectResponse(struct http_conn *, struct http *resp,
    const struct http *req);
unsigned HTTP1_Write(const struct worker *w, const struct http *hp, const int*);

/* cache_main.c */
vxid_t VXID_Get(const struct worker *, uint64_t marker);
extern pthread_key_t witness_key;

void THR_SetName(const char *name);
const char* THR_GetName(void);
void THR_SetBusyobj(const struct busyobj *);
struct busyobj * THR_GetBusyobj(void);
void THR_SetRequest(const struct req *);
struct req * THR_GetRequest(void);
void THR_SetWorker(const struct worker *);
struct worker * THR_GetWorker(void);
void THR_Init(void);

/* cache_lck.c */
void LCK_Init(void);

/* cache_mempool.c */
void MPL_AssertSane(const void *item);
struct mempool * MPL_New(const char *name, volatile struct poolparam *pp,
    volatile unsigned *cur_size);
void MPL_Destroy(struct mempool **mpp);
void *MPL_Get(struct mempool *mpl, unsigned *size);
void MPL_Free(struct mempool *mpl, void *item);

/* cache_obj.c */
void ObjInit(void);
struct objcore * ObjNew(const struct worker *);
void ObjDestroy(const struct worker *, struct objcore **);
int ObjGetSpace(struct worker *, struct objcore *, ssize_t *sz, uint8_t **ptr);
void ObjExtend(struct worker *, struct objcore *, ssize_t l, int final);
uint64_t ObjWaitExtend(const struct worker *, const struct objcore *,
    uint64_t l);
void ObjSetState(struct worker *, const struct objcore *,
    enum boc_state_e next);
void ObjWaitState(const struct objcore *, enum boc_state_e want);
void ObjTouch(struct worker *, struct objcore *, vtim_real now);
void ObjFreeObj(struct worker *, struct objcore *);
void ObjSlim(struct worker *, struct objcore *);
void *ObjSetAttr(struct worker *, struct objcore *, enum obj_attr,
    ssize_t len, const void *);
int ObjCopyAttr(struct worker *, struct objcore *, struct objcore *,
    enum obj_attr attr);
void ObjBocDone(struct worker *, struct objcore *, struct boc **);

int ObjSetDouble(struct worker *, struct objcore *, enum obj_attr, double);
int ObjSetU64(struct worker *, struct objcore *, enum obj_attr, uint64_t);
int ObjSetXID(struct worker *, struct objcore *, vxid_t);

void ObjSetFlag(struct worker *, struct objcore *, enum obj_flags of, int val);

void ObjSendEvent(struct worker *, struct objcore *oc, unsigned event);

#define OEV_INSERT	(1U<<1)
#define OEV_BANCHG	(1U<<2)
#define OEV_TTLCHG	(1U<<3)
#define OEV_EXPIRE	(1U<<4)

#define OEV_MASK (OEV_INSERT|OEV_BANCHG|OEV_TTLCHG|OEV_EXPIRE)

typedef void obj_event_f(struct worker *, void *priv, struct objcore *,
    unsigned);

uintptr_t ObjSubscribeEvents(obj_event_f *, void *, unsigned mask);
void ObjUnsubscribeEvents(uintptr_t *);

/* cache_panic.c */
void PAN_Init(void);
int PAN__DumpStruct(struct vsb *vsb, int block, int track, const void *ptr,
    const char *smagic, unsigned magic, const char *fmt, ...)
    v_printflike_(7,8);

#define PAN_dump_struct(vsb, ptr, magic, ...)		\
    PAN__DumpStruct(vsb, 1, 1, ptr, #magic, magic, __VA_ARGS__)

#define PAN_dump_oneline(vsb, ptr, magic, ...)		\
    PAN__DumpStruct(vsb, 0, 1, ptr, #magic, magic, __VA_ARGS__)

#define PAN_dump_once(vsb, ptr, magic, ...)		\
    PAN__DumpStruct(vsb, 1, 0, ptr, #magic, magic, __VA_ARGS__)

#define PAN_dump_once_oneline(vsb, ptr, magic, ...)		\
    PAN__DumpStruct(vsb, 0, 0, ptr, #magic, magic, __VA_ARGS__)

/* cache_pool.c */
void Pool_Init(void);
int Pool_Task(struct pool *pp, struct pool_task *task, enum task_prio prio);
int Pool_Task_Arg(struct worker *, enum task_prio, task_func_t *,
    const void *arg, size_t arg_len);
void Pool_Sumstat(const struct worker *w);
int Pool_TrySumstat(const struct worker *wrk);
void Pool_PurgeStat(unsigned nobj);
int Pool_Task_Any(struct pool_task *task, enum task_prio prio);
void pan_pool(struct vsb *);

/* cache_range.c */
int VRG_CheckBo(struct busyobj *);

/* cache_req.c */
struct req *Req_New(struct sess *);
void Req_Release(struct req *);
void Req_Rollback(VRT_CTX);
void Req_Cleanup(struct sess *sp, struct worker *wrk, struct req *req);
void Req_Fail(struct req *req, stream_close_t reason);
void Req_AcctLogCharge(struct VSC_main_wrk *, struct req *);
void Req_LogHit(struct worker *, struct req *, struct objcore *, intmax_t);
const char *Req_LogStart(const struct worker *, struct req *);

/* cache_req_body.c */
int VRB_Ignore(struct req *);
ssize_t VRB_Cache(struct req *, ssize_t maxsize);
void VRB_Free(struct req *);

/* cache_req_fsm.c [CNT] */

int Resp_Setup_Deliver(struct req *);
void Resp_Setup_Synth(struct req *);

enum req_fsm_nxt {
	REQ_FSM_MORE,
	REQ_FSM_DONE,
	REQ_FSM_DISEMBARK,
};

void CNT_Embark(struct worker *, struct req *);
enum req_fsm_nxt CNT_Request(struct req *);

/* cache_session.c */
void SES_NewPool(struct pool *, unsigned pool_no);
void SES_DestroyPool(struct pool *);
void SES_Wait(struct sess *, const struct transport *);
void SES_Ref(struct sess *sp);
void SES_Rel(struct sess *sp);

const char * HTC_Status(enum htc_status_e);
void HTC_RxInit(struct http_conn *htc, struct ws *ws);
void HTC_RxPipeline(struct http_conn *htc, char *);
enum htc_status_e HTC_RxStuff(struct http_conn *, htc_complete_f *,
    vtim_real *t1, vtim_real *t2, vtim_real ti, vtim_real tn, vtim_dur td,
    int maxbytes);

#define SESS_ATTR(UP, low, typ, len)					\
	int SES_Set_##low(const struct sess *sp, const typ *src);	\
	int SES_Reserve_##low(struct sess *sp, typ **dst, ssize_t *sz);
#include "tbl/sess_attr.h"
int SES_Set_String_Attr(struct sess *sp, enum sess_attr a, const char *src);

/* cache_shmlog.c */
extern struct VSC_main *VSC_C_main;
void VSM_Init(void);
void VSL_Setup(struct vsl_log *vsl, void *ptr, size_t len);
void VSL_ChgId(struct vsl_log *vsl, const char *typ, const char *why,
    vxid_t vxid);
void VSL_End(struct vsl_log *vsl);
void VSL_Flush(struct vsl_log *, int overflow);

/* cache_conn_pool.c */
struct conn_pool;
void VCP_Init(void);
void VCP_Panic(struct vsb *, struct conn_pool *);

/* cache_backend_poll.c */

/* cache_vary.c */
int VRY_Create(struct busyobj *bo, struct vsb **psb);
int VRY_Match(const struct req *, const uint8_t *vary);
void VRY_Prep(struct req *);
void VRY_Clear(struct req *);
enum vry_finish_flag { KEEP, DISCARD };
void VRY_Finish(struct req *req, enum vry_finish_flag);

/* cache_vcl.c */
void VCL_Bo2Ctx(struct vrt_ctx *, struct busyobj *);
void VCL_Req2Ctx(struct vrt_ctx *, struct req *);
struct vrt_ctx *VCL_Get_CliCtx(int);
struct vsb *VCL_Rel_CliCtx(struct vrt_ctx **);
void VCL_Panic(struct vsb *, const char *nm, const struct vcl *);
void VCL_Poll(void);
void VCL_Init(void);

#define VCL_MET_MAC(l,u,t,b) \
    void VCL_##l##_method(struct vcl *, struct worker *, struct req *, \
	struct busyobj *bo, void *specific);
#include "tbl/vcl_returns.h"


typedef int vcl_be_func(struct cli *, struct director *, void *);

int VCL_IterDirector(struct cli *, const char *, vcl_be_func *, void *);

/* cache_vrt.c */
void pan_privs(struct vsb *, const struct vrt_privs *);

/* cache_vrt_filter.c */
int VCL_StackVFP(struct vfp_ctx *, const struct vcl *, const char *);
int VCL_StackVDP(struct req *, const struct vcl *, const char *);
const char *resp_Get_Filter_List(struct req *req);
void VCL_VRT_Init(void);

/* cache_vrt_vcl.c */
const char *VCL_Return_Name(unsigned);
const char *VCL_Method_Name(unsigned);
void VCL_Refresh(struct vcl **);
void VCL_Recache(const struct worker *, struct vcl **);
void VCL_Ref(struct vcl *);
void VCL_Rel(struct vcl **);
VCL_BACKEND VCL_DefaultDirector(const struct vcl *);
const struct vrt_backend_probe *VCL_DefaultProbe(const struct vcl *);

/* cache_vrt_priv.c */
extern struct vrt_privs cli_task_privs[1];
void VCL_TaskEnter(struct vrt_privs *);
void VCL_TaskLeave(VRT_CTX, struct vrt_privs *);

/* cache_vrt_vmod.c */
void VMOD_Init(void);
void VMOD_Panic(struct vsb *);

#if defined(ENABLE_COVERAGE) || defined(ENABLE_SANITIZER)
#  define DONT_DLCLOSE_VMODS
#endif

/* cache_wrk.c */
void WRK_Init(void);
void WRK_AddStat(const struct worker *);
void WRK_Log(enum VSL_tag_e, const char *, ...);

/* cache_vpi.c */
extern const size_t vpi_wrk_len;
void VPI_wrk_init(struct worker *, void *, size_t);
void VPI_Panic(struct vsb *, const struct wrk_vpi *, const struct vcl *);

/* cache_ws.c */
void WS_Panic(struct vsb *, const struct ws *);
static inline int
WS_IsReserved(const struct ws *ws)
{

	return (ws->r != NULL);
}

void *WS_AtOffset(const struct ws *ws, unsigned off, unsigned len);
unsigned WS_ReservationOffset(const struct ws *ws);
unsigned WS_ReqPipeline(struct ws *, const void *b, const void *e);

/* cache_ws_common.c */
void WS_Id(const struct ws *ws, char *id);
void WS_Rollback(struct ws *, uintptr_t);

/* http1/cache_http1_pipe.c */
void V1P_Init(void);

/* cache_http2_deliver.c */
void V2D_Init(void);

/* stevedore.c */
void STV_open(void);
void STV_close(void);
const struct stevedore *STV_next(void);
int STV_BanInfoDrop(const uint8_t *ban, unsigned len);
int STV_BanInfoNew(const uint8_t *ban, unsigned len);
void STV_BanExport(const uint8_t *banlist, unsigned len);
int STV_NewObject(struct worker *, struct objcore *,
    const struct stevedore *, unsigned len);

struct stv_buffer;
struct stv_buffer *STV_AllocBuf(struct worker *wrk, const struct stevedore *stv,
    size_t size);
void STV_FreeBuf(struct worker *wrk, struct stv_buffer **pstvbuf);
void *STV_GetBufPtr(struct stv_buffer *stvbuf, size_t *psize);

/* cache_smuggler.c */
void SMUG_Init(void);
int SMUG_Fence(uint64_t);

#if WITH_PERSISTENT_STORAGE
/* storage_persistent.c */
void SMP_Ready(void);
#endif

#define FEATURE(x)	COM_FEATURE(cache_param->feature_bits, x)
#define EXPERIMENT(x)	COM_EXPERIMENT(cache_param->experimental_bits, x)
#define DO_DEBUG(x)	COM_DO_DEBUG(cache_param->debug_bits, x)

#define DSL(debug_bit, id, ...)					\
	do {							\
		if (DO_DEBUG(debug_bit))			\
			VSL(SLT_Debug, (id), __VA_ARGS__);	\
	} while (0)

#define DSLb(debug_bit, ...)					\
	do {							\
		if (DO_DEBUG(debug_bit))			\
			WRK_Log(SLT_Debug, __VA_ARGS__);	\
	} while (0)
