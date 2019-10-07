/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2019 Varnish Software AS
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

/* -------------------------------------------------------------------*/

struct vfp;
struct cli_proto;
struct poolparam;

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
	enum sess_close		doclose;
	enum body_status	body_status;
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

typedef enum htc_status_e htc_complete_f(struct http_conn *);

/* -------------------------------------------------------------------*/

extern volatile struct params * cache_param;

/* Prototypes etc ----------------------------------------------------*/

/* cache_acceptor.c */
void VCA_Init(void);
void VCA_Shutdown(void);

/* cache_backend_cfg.c */
void VBE_InitCfg(void);
void VBE_Poll(void);

/* cache_backend_tcp.c */
void VTP_Init(void);

/* cache_backend_poll.c */
void VBP_Init(void);

/* cache_ban.c */

/* for stevedoes resurrecting bans */
void BAN_Hold(void);
void BAN_Release(void);
void BAN_Reload(const uint8_t *ban, unsigned len);
struct ban *BAN_FindBan(double t0);
void BAN_RefBan(struct objcore *oc, struct ban *);
double BAN_Time(const struct ban *ban);

/* cache_busyobj.c */
struct busyobj *VBO_GetBusyObj(struct worker *, const struct req *);
void VBO_ReleaseBusyObj(struct worker *wrk, struct busyobj **busyobj);

/* cache_director.c */
int VDI_GetHdr(struct worker *, struct busyobj *);
int VDI_GetBody(struct worker *, struct busyobj *);
const struct suckaddr *VDI_GetIP(struct worker *, struct busyobj *);
void VDI_Finish(struct worker *wrk, struct busyobj *bo);
enum sess_close VDI_Http1Pipe(struct req *, struct busyobj *);
void VDI_Panic(const struct director *, struct vsb *, const char *nm);
void VDI_Event(const struct director *d, enum vcl_event_e ev);
void VDI_Init(void);

/* cache_exp.c */
double EXP_Ttl(const struct req *, const struct objcore *);
double EXP_Ttl_grace(const struct req *, const struct objcore *oc);
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
void EXP_Rearm(struct objcore *, double now, double ttl, double grace,
    double keep);


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

/* cache_fetch.c */
enum vbf_fetch_mode_e {
	VBF_NORMAL = 0,
	VBF_PASS = 1,
	VBF_BACKGROUND = 2,
};
void VBF_Fetch(struct worker *wrk, struct req *req,
    struct objcore *oc, struct objcore *oldoc, enum vbf_fetch_mode_e);
void Bereq_Rollback(struct busyobj *);

/* cache_fetch_proc.c */
void VFP_Init(void);
enum vfp_status VFP_GetStorage(struct vfp_ctx *, ssize_t *sz, uint8_t **ptr);
void VFP_Extend(const struct vfp_ctx *, ssize_t sz);
struct vfp_entry *VFP_Push(struct vfp_ctx *, const struct vfp *);
void VFP_Setup(struct vfp_ctx *vc, struct worker *wrk);
int VFP_Open(struct vfp_ctx *bo);
void VFP_Close(struct vfp_ctx *bo);

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
void THR_SetName(const char *name);
const char* THR_GetName(void);
void THR_SetBusyobj(const struct busyobj *);
struct busyobj * THR_GetBusyobj(void);
void THR_SetRequest(const struct req *);
struct req * THR_GetRequest(void);
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
void ObjExtend(struct worker *, struct objcore *, ssize_t l);
uint64_t ObjWaitExtend(const struct worker *, const struct objcore *,
    uint64_t l);
void ObjSetState(struct worker *, const struct objcore *,
    enum boc_state_e next);
void ObjWaitState(const struct objcore *, enum boc_state_e want);
void ObjTrimStore(struct worker *, struct objcore *);
void ObjTouch(struct worker *, struct objcore *, vtim_real now);
void ObjFreeObj(struct worker *, struct objcore *);
void ObjSlim(struct worker *, struct objcore *);
void *ObjSetAttr(struct worker *, struct objcore *, enum obj_attr,
    ssize_t len, const void *);
int ObjCopyAttr(struct worker *, struct objcore *, struct objcore *,
    enum obj_attr attr);
void ObjBocDone(struct worker *, struct objcore *, struct boc **);

int ObjSetDouble(struct worker *, struct objcore *, enum obj_attr, double);
int ObjSetU32(struct worker *, struct objcore *, enum obj_attr, uint32_t);
int ObjSetU64(struct worker *, struct objcore *, enum obj_attr, uint64_t);

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
int PAN_already(struct vsb *, const void *);
const char *body_status_2str(enum body_status e);
const char *sess_close_2str(enum sess_close sc, int want_desc);

/* cache_pool.c */
void Pool_Init(void);
int Pool_Task(struct pool *pp, struct pool_task *task, enum task_prio prio);
int Pool_Task_Arg(struct worker *, enum task_prio, task_func_t *,
    const void *arg, size_t arg_len);
void Pool_Sumstat(const struct worker *w);
int Pool_TrySumstat(const struct worker *wrk);
void Pool_PurgeStat(unsigned nobj);
int Pool_Task_Any(struct pool_task *task, enum task_prio prio);

/* cache_range.c [VRG] */
void VRG_dorange(struct req *req, const char *r);

/* cache_req.c */
struct req *Req_New(const struct worker *, struct sess *);
void Req_Release(struct req *);
void Req_Rollback(struct req *req);
void Req_Cleanup(struct sess *sp, struct worker *wrk, struct req *req);
void Req_Fail(struct req *req, enum sess_close reason);

/* cache_req_body.c */
int VRB_Ignore(struct req *);
ssize_t VRB_Cache(struct req *, ssize_t maxsize);
ssize_t VRB_Iterate(struct req *, objiterate_f *func, void *priv);
void VRB_Free(struct req *);

/* cache_req_fsm.c [CNT] */

int Resp_Setup(struct req *, unsigned);

enum req_fsm_nxt {
	REQ_FSM_MORE,
	REQ_FSM_DONE,
	REQ_FSM_DISEMBARK,
};

enum req_fsm_nxt CNT_Request(struct worker *, struct req *);

/* cache_session.c */
void SES_NewPool(struct pool *, unsigned pool_no);
void SES_DestroyPool(struct pool *);
void SES_Wait(struct sess *, const struct transport *);
void SES_Ref(struct sess *sp);
void SES_Rel(struct sess *sp);

const char * HTC_Status(enum htc_status_e);
void HTC_RxInit(struct http_conn *htc, struct ws *ws);
void HTC_RxPipeline(struct http_conn *htc, void *);
enum htc_status_e HTC_RxStuff(struct http_conn *, htc_complete_f *,
    vtim_real *t1, vtim_real *t2, vtim_real ti, vtim_real tn, vtim_dur td,
    int maxbytes);

#define SESS_ATTR(UP, low, typ, len)					\
	int SES_Set_##low(const struct sess *sp, const typ *src);	\
	int SES_Reserve_##low(struct sess *sp, typ **dst);
#include "tbl/sess_attr.h"
int SES_Set_String_Attr(struct sess *sp, enum sess_attr a, const char *src);


enum htc_status_e {
#define HTC_STATUS(e, n, s, l) HTC_S_ ## e = n,
#include "tbl/htc.h"
};

/* cache_shmlog.c */
extern struct VSC_main *VSC_C_main;
void VSM_Init(void);
void VSL_Setup(struct vsl_log *vsl, void *ptr, size_t len);
void VSL_ChgId(struct vsl_log *vsl, const char *typ, const char *why,
    uint32_t vxid);
void VSL_End(struct vsl_log *vsl);
void VSL_Flush(struct vsl_log *, int overflow);

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
void VCL_TaskEnter(const struct vcl *, struct vrt_privs *);
void VCL_TaskLeave(const struct vcl *, struct vrt_privs *);
const char *VCL_Return_Name(unsigned);
const char *VCL_Method_Name(unsigned);
void VCL_Bo2Ctx(struct vrt_ctx *, struct busyobj *);
void VCL_Req2Ctx(struct vrt_ctx *, struct req *);

#define VCL_MET_MAC(l,u,t,b) \
    void VCL_##l##_method(struct vcl *, struct worker *, struct req *, \
	struct busyobj *bo, void *specific);
#include "tbl/vcl_returns.h"


typedef int vcl_be_func(struct cli *, struct director *, void *);

int VCL_IterDirector(struct cli *, const char *, vcl_be_func *, void *);

/* cache_vrt.c */
void pan_privs(struct vsb *, const struct vrt_privs *);

/* cache_vrt_priv.c */
extern struct vrt_privs cli_task_privs[1];

/* cache_vrt_vmod.c */
void VMOD_Init(void);
void VMOD_Panic(struct vsb *);

/* cache_wrk.c */
void WRK_Init(void);

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


#if WITH_PERSISTENT_STORAGE
/* storage_persistent.c */
void SMP_Ready(void);
#endif

#define FEATURE(x)	COM_FEATURE(cache_param->feature_bits, x)
#define DO_DEBUG(x)	COM_DO_DEBUG(cache_param->debug_bits, x)

#define DSL(debug_bit, id, ...)					\
	do {							\
		if (DO_DEBUG(debug_bit))			\
			VSL(SLT_Debug, (id), __VA_ARGS__);	\
	} while (0)
