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
 */

#ifdef VRT_H_INCLUDED
#  error "vrt.h included before cache.h - they are exclusive"
#endif

#ifdef CACHE_H_INCLUDED
#  error "cache.h included multiple times"
#endif

#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <sys/types.h>

#include "vdef.h"
#include "vrt.h"

#define CACHE_H_INCLUDED

#include "miniobj.h"
#include "vas.h"
#include "vqueue.h"

#include "vapi/vsl_int.h"

/*--------------------------------------------------------------------*/

enum body_status {
#define BODYSTATUS(U,l)	BS_##U,
#include "tbl/body_status.h"
};

/*--------------------------------------------------------------------*/

enum req_body_state_e {
#define REQ_BODY(U)	REQ_BODY_##U,
#include "tbl/req_body.h"
};

/*--------------------------------------------------------------------*/

enum sess_close {
	SC_NULL = 0,
#define SESS_CLOSE(nm, stat, err, desc)	SC_##nm,
#include "tbl/sess_close.h"
};

/*--------------------------------------------------------------------
 * Indices into http->hd[]
 */
enum {
#define SLTH(tag, ind, req, resp, sdesc, ldesc)	ind,
#include "tbl/vsl_tags_http.h"
};

/*--------------------------------------------------------------------*/

struct VSC_lck;
struct VSC_main;
struct VSC_main_wrk;
struct backend;
struct ban;
struct ban_proto;
struct cli;
struct http_conn;
struct mempool;
struct objcore;
struct objhead;
struct pool;
struct sess;
struct transport;
struct worker;
struct listen_sock;

#define DIGEST_LEN		32

/*--------------------------------------------------------------------*/

typedef struct {
	const char		*b;
	const char		*e;
} txt;

/*--------------------------------------------------------------------*/

enum req_step {
	R_STP_NONE = 0,
#define REQ_STEP(l, u, arg)	R_STP_##u,
#include "tbl/steps.h"
};

enum fetch_step {
	F_STP_NONE = 0,
#define FETCH_STEP(l, U, arg)	F_STP_##U,
#include "tbl/steps.h"
};

/*--------------------------------------------------------------------*/

struct lock { void *priv; };	// Opaque

/*--------------------------------------------------------------------
 * Workspace structure for quick memory allocation.
 */

struct ws {
	unsigned		magic;
#define WS_MAGIC		0x35fac554
	char			id[4];		/* identity */
	char			*s;		/* (S)tart of buffer */
	char			*f;		/* (F)ree/front pointer */
	char			*r;		/* (R)eserved length */
	char			*e;		/* (E)nd of buffer */
};

/*--------------------------------------------------------------------
 *
 */

struct http {
	unsigned		magic;
#define HTTP_MAGIC		0x6428b5c9

	uint16_t		shd;		/* Size of hd space */
	txt			*hd;
	unsigned char		*hdf;
#define HDF_FILTER		(1 << 0)	/* Filtered by Connection */

	/* NB: ->nhd and below zeroed/initialized by http_Teardown */
	uint16_t		nhd;		/* Next free hd */

	enum VSL_tag_e		logtag;		/* Must be SLT_*Method */
	struct vsl_log		*vsl;

	struct ws		*ws;
	uint16_t		status;
	uint8_t			protover;
	uint8_t			conds;		/* If-* headers present */
};

/*--------------------------------------------------------------------*/

struct acct_req {
#define ACCT(foo)	uint64_t	foo;
#include "tbl/acct_fields_req.h"
};

/*--------------------------------------------------------------------*/

struct acct_bereq {
#define ACCT(foo)	uint64_t	foo;
#include "tbl/acct_fields_bereq.h"
};

/*--------------------------------------------------------------------*/

struct vsl_log {
	uint32_t                *wlb, *wlp, *wle;
	unsigned                wlr;
	unsigned                wid;
};

/*--------------------------------------------------------------------*/

struct vxid_pool {
	uint32_t		next;
	uint32_t		count;
};

/*--------------------------------------------------------------------*/

struct vrt_privs {
	unsigned		magic;
#define VRT_PRIVS_MAGIC		0x03ba7501
	VTAILQ_HEAD(,vrt_priv)	privs;
};

/* Worker pool stuff -------------------------------------------------*/

typedef void task_func_t(struct worker *wrk, void *priv);

struct pool_task {
	VTAILQ_ENTRY(pool_task)		list;
	task_func_t			*func;
	void				*priv;
};

/*
 * tasks are taken off the queues in this order
 *
 * prios up to TASK_QUEUE_RESERVE are run from the reserve
 *
 * TASK_QUEUE_{REQ|STR} are new req's (H1/H2), and subject to queue limit.
 *
 * TASK_QUEUE_RUSH is req's returning from waiting list, they are
 * not subject to TASK_QUEUE_CLIENT because we cannot safely clean
 * them up if scheduling them fails.
 */
enum task_prio {
	TASK_QUEUE_BO,
#define TASK_QUEUE_RESERVE	TASK_QUEUE_BO
	TASK_QUEUE_RUSH,
	TASK_QUEUE_REQ,
	TASK_QUEUE_STR,
	TASK_QUEUE_VCA,
	TASK_QUEUE_END
};

#define TASK_QUEUE_CLIENT(prio) \
	(prio == TASK_QUEUE_REQ || prio == TASK_QUEUE_STR)

/*--------------------------------------------------------------------*/

struct worker {
	unsigned		magic;
#define WORKER_MAGIC		0x6391adcf
	struct pool		*pool;
	struct objhead		*nobjhead;
	struct objcore		*nobjcore;
	void			*nhashpriv;
	struct VSC_main_wrk	*stats;
	struct vsl_log		*vsl;		// borrowed from req/bo

	struct pool_task	task;

	vtim_real		lastused;
	int			strangelove;

	struct v1l		*v1l;

	pthread_cond_t		cond;

	struct vcl		*vcl;

	struct ws		aws[1];

	struct vxid_pool	vxid_pool;

	unsigned		cur_method;
	unsigned		seen_methods;
	unsigned		handling;
};

/* Stored object -----------------------------------------------------
 * This is just to encapsulate the fields owned by the stevedore
 */

struct storeobj {
	const struct stevedore	*stevedore;
	void			*priv;
	uintptr_t		priv2;
};

/* Busy Objcore structure --------------------------------------------
 *
 */

/*
 * The macro-states we expose outside the fetch code
 */
enum boc_state_e {
#define BOC_STATE(U, l)       BOS_##U,
#include "tbl/boc_state.h"
};

struct boc {
	unsigned		magic;
#define BOC_MAGIC		0x70c98476
	unsigned		refcount;
	struct lock		mtx;
	pthread_cond_t		cond;
	void			*stevedore_priv;
	enum boc_state_e	state;
	uint8_t			*vary;
	uint64_t		len_so_far;
};

/* Object core structure ---------------------------------------------
 * Objects have sideways references in the binary heap and the LRU list
 * and we want to avoid paging in a lot of objects just to move them up
 * or down the binheap or to move a unrelated object on the LRU list.
 * To avoid this we use a proxy object, objcore, to hold the relevant
 * housekeeping fields parts of an object.
 */

enum obj_attr {
#define OBJ_FIXATTR(U, l, s)	OA_##U,
#define OBJ_VARATTR(U, l)	OA_##U,
#define OBJ_AUXATTR(U, l)	OA_##U,
#include "tbl/obj_attr.h"
				OA__MAX,
};

enum obj_flags {
#define OBJ_FLAG(U, l, v)       OF_##U = v,
#include "tbl/obj_attr.h"
};

enum oc_flags {
#define OC_FLAG(U, l, v)	OC_F_##U = v,
#include "tbl/oc_flags.h"
};

enum oc_exp_flags {
#define OC_EXP_FLAG(U, l, v)	OC_EF_##U = v,
#include "tbl/oc_exp_flags.h"
};

struct objcore {
	unsigned		magic;
#define OBJCORE_MAGIC		0x4d301302
	int			refcnt;
	struct storeobj		stobj[1];
	struct objhead		*objhead;
	struct boc		*boc;
	double			timer_when;
	long			hits;

	double			t_origin;
	float			ttl;
	float			grace;
	float			keep;

	uint8_t			flags;

	uint8_t			exp_flags;

	uint16_t		oa_present;

	unsigned		timer_idx;	// XXX 4Gobj limit
	double			last_lru;
	VTAILQ_ENTRY(objcore)	hsh_list;
	VTAILQ_ENTRY(objcore)	lru_list;
	VTAILQ_ENTRY(objcore)	ban_list;
	VSTAILQ_ENTRY(objcore)	exp_list;
	struct ban		*ban;
};

/* Busy Object structure ---------------------------------------------
 *
 * The busyobj structure captures the aspects of an object related to,
 * and while it is being fetched from the backend.
 *
 * One of these aspects will be how much has been fetched, which
 * streaming delivery will make use of.
 */

enum director_state_e {
	DIR_S_NULL = 0,
	DIR_S_HDRS = 1,
	DIR_S_BODY = 2,
};

struct busyobj {
	unsigned		magic;
#define BUSYOBJ_MAGIC		0x23b95567

	char			*end;

	/*
	 * All fields from retries and down are zeroed when the busyobj
	 * is recycled.
	 */
	int			retries;
	struct req		*req;
	struct sess		*sp;
	struct worker		*wrk;

	struct vfp_ctx		*vfc;

	struct ws		ws[1];
	uintptr_t		ws_bo;
	struct http		*bereq0;
	struct http		*bereq;
	struct http		*beresp;
	struct objcore		*stale_oc;
	struct objcore		*fetch_objcore;

	struct http_conn	*htc;

	struct pool_task	fetch_task;

#define BO_FLAG(l, r, w, d) unsigned	l:1;
#include "tbl/bo_flags.h"

	/* Timeouts */
	vtim_dur		connect_timeout;
	vtim_dur		first_byte_timeout;
	vtim_dur		between_bytes_timeout;

	/* Timers */
	double			t_first;	/* First timestamp logged */
	double			t_prev;		/* Previous timestamp logged */

	/* Acct */
	struct acct_bereq	acct;

	const struct stevedore	*storage;
	const struct director	*director_req;
	const struct director	*director_resp;
	enum director_state_e	director_state;
	struct vcl		*vcl;

	struct vsl_log		vsl[1];

	uint8_t			digest[DIGEST_LEN];
	struct vrt_privs	privs[1];

	uint16_t		err_code;
	const char		*err_reason;
};


/*--------------------------------------------------------------------*/

struct req {
	unsigned		magic;
#define REQ_MAGIC		0x2751aaa1

	enum req_step		req_step;
	volatile enum req_body_state_e	req_body_status;
	enum sess_close		doclose;
	int			restarts;
	int			esi_level;
	struct req		*top;	/* esi_level == 0 request */

#define REQ_FLAG(l, r, w, d) unsigned	l:1;
#include "tbl/req_flags.h"

	uint16_t		err_code;
	const char		*err_reason;

	struct sess		*sp;
	struct worker		*wrk;
	struct pool_task	task;

	const struct transport	*transport;
	void			*transport_priv;

	VTAILQ_ENTRY(req)	w_list;

	struct objcore		*body_oc;

	/* The busy objhead we sleep on */
	struct objhead		*hash_objhead;

	/* Built Vary string */
	uint8_t			*vary_b;
	uint8_t			*vary_l;
	uint8_t			*vary_e;

	uint8_t			digest[DIGEST_LEN];

	double			d_ttl;
	double			d_grace;

	ssize_t			req_bodybytes;	/* Parsed req bodybytes */
	const struct stevedore	*storage;

	const struct director	*director_hint;
	struct vcl		*vcl;

	uintptr_t		ws_req;		/* WS above request data */

	/* Timestamps */
	double			t_first;	/* First timestamp logged */
	double			t_prev;		/* Previous timestamp logged */
	double			t_req;		/* Headers complete */

	struct http_conn	*htc;
	struct vfp_ctx		*vfc;
	const char		*client_identity;

	/* HTTP request */
	struct http		*http;
	struct http		*http0;

	/* HTTP response */
	struct http		*resp;
	intmax_t		resp_len;

	struct ws		ws[1];
	struct objcore		*objcore;
	struct objcore		*stale_oc;

	/* Deliver pipeline */
	struct vdp_ctx		*vdc;

	/* Delivery mode */
	unsigned		res_mode;
#define RES_LEN			(1<<1)
#define RES_EOF			(1<<2)
#define RES_CHUNKED		(1<<3)
#define RES_ESI			(1<<4)
#define RES_ESI_CHILD		(1<<5)
#define RES_GUNZIP		(1<<6)
#define RES_PIPE		(1<<7)

	/* Transaction VSL buffer */
	struct vsl_log		vsl[1];

	/* Temporary accounting */
	struct acct_req		acct;

	struct vrt_privs	privs[1];
};

/*--------------------------------------------------------------------
 * Struct sess is a high memory-load structure because sessions typically
 * hang around the waiter for relatively long time.
 *
 * The size goal for struct sess + struct memitem is <512 bytes
 *
 * Getting down to the next relevant size (<256 bytes because of how malloc
 * works, is not realistic without a lot of code changes.
 */

enum sess_attr {
#define SESS_ATTR(UP, low, typ, len)	SA_##UP,
	SA_TRANSPORT,
#include "tbl/sess_attr.h"
	SA_LAST
};

struct sess {
	unsigned		magic;
#define SESS_MAGIC		0x2c2f9c5a

	uint16_t		sattr[SA_LAST];
	struct listen_sock	*listen_sock;
	int			refcnt;
	int			fd;
	uint32_t		vxid;

	struct lock		mtx;

	struct pool		*pool;

	struct ws		ws[1];

	vtim_real		t_open;		/* fd accepted */
	vtim_real		t_idle;		/* fd accepted or resp sent */

	vtim_dur		timeout_idle;
};

#define SESS_TMO(sp, tmo)					\
	(isnan((sp)->tmo) ? cache_param->tmo : (sp)->tmo)

/* Prototypes etc ----------------------------------------------------*/


/* cache_ban.c */

/* for constructing bans */
struct ban_proto *BAN_Build(void);
const char *BAN_AddTest(struct ban_proto *,
    const char *, const char *, const char *);
const char *BAN_Commit(struct ban_proto *b);
void BAN_Abandon(struct ban_proto *b);

/* cache_cli.c [CLI] */
extern pthread_t cli_thread;
#define ASSERT_CLI() do {assert(pthread_self() == cli_thread);} while (0)

/* cache_http.c */
unsigned HTTP_estimate(unsigned nhttp);
void HTTP_Copy(struct http *to, const struct http * const fm);
struct http *HTTP_create(void *p, uint16_t nhttp, unsigned);
const char *http_Status2Reason(unsigned, const char **);
unsigned http_EstimateWS(const struct http *fm, unsigned how);
void http_PutResponse(struct http *to, const char *proto, uint16_t status,
    const char *response);
void http_FilterReq(struct http *to, const struct http *fm, unsigned how);
void HTTP_Encode(const struct http *fm, uint8_t *, unsigned len, unsigned how);
int HTTP_Decode(struct http *to, const uint8_t *fm);
void http_ForceHeader(struct http *to, const char *hdr, const char *val);
void http_PrintfHeader(struct http *to, const char *fmt, ...)
    v_printflike_(2, 3);
void http_TimeHeader(struct http *to, const char *fmt, double now);
void http_Proto(struct http *to);
void http_SetHeader(struct http *to, const char *hdr);
void http_SetH(struct http *to, unsigned n, const char *fm);
void http_ForceField(struct http *to, unsigned n, const char *t);
void HTTP_Setup(struct http *, struct ws *, struct vsl_log *, enum VSL_tag_e);
void http_Teardown(struct http *ht);
int http_GetHdr(const struct http *hp, const char *hdr, const char **ptr);
int http_GetHdrToken(const struct http *hp, const char *hdr,
    const char *token, const char **pb, const char **pe);
int http_GetHdrField(const struct http *hp, const char *hdr,
    const char *field, const char **ptr);
double http_GetHdrQ(const struct http *hp, const char *hdr, const char *field);
ssize_t http_GetContentLength(const struct http *hp);
uint16_t http_GetStatus(const struct http *hp);
int http_IsStatus(const struct http *hp, int);
void http_SetStatus(struct http *to, uint16_t status);
const char *http_GetMethod(const struct http *hp);
int http_HdrIs(const struct http *hp, const char *hdr, const char *val);
void http_CopyHome(const struct http *hp);
void http_Unset(struct http *hp, const char *hdr);
unsigned http_CountHdr(const struct http *hp, const char *hdr);
void http_CollectHdr(struct http *hp, const char *hdr);
void http_CollectHdrSep(struct http *hp, const char *hdr, const char *sep);
void http_VSL_log(const struct http *hp);
void HTTP_Merge(struct worker *, struct objcore *, struct http *to);
uint16_t HTTP_GetStatusPack(struct worker *, struct objcore *oc);
int HTTP_IterHdrPack(struct worker *, struct objcore *, const char **);
#define HTTP_FOREACH_PACK(wrk, oc, ptr) \
	 for ((ptr) = NULL; HTTP_IterHdrPack(wrk, oc, &(ptr));)
const char *HTTP_GetHdrPack(struct worker *, struct objcore *, const char *hdr);
enum sess_close http_DoConnection(struct http *hp);

#define HTTPH_R_PASS	(1 << 0)	/* Request (c->b) in pass mode */
#define HTTPH_R_FETCH	(1 << 1)	/* Request (c->b) for fetch */
#define HTTPH_A_INS	(1 << 2)	/* Response (b->o) for insert */
#define HTTPH_A_PASS	(1 << 3)	/* Response (b->o) for pass */

#define HTTPH(a, b, c) extern char b[];
#include "tbl/http_headers.h"

extern const char H__Status[];
extern const char H__Proto[];
extern const char H__Reason[];

/* cache_main.c */
#define VXID(u) ((u) & VSL_IDENTMASK)
uint32_t VXID_Get(struct worker *, uint32_t marker);
extern pthread_key_t witness_key;

/* cache_lck.c */

/* Internal functions, call only through macros below */
void Lck__Lock(struct lock *lck, const char *p,  int l);
void Lck__Unlock(struct lock *lck, const char *p,  int l);
int Lck__Trylock(struct lock *lck, const char *p,  int l);
void Lck__New(struct lock *lck, struct VSC_lck *, const char *);
int Lck__Held(const struct lock *lck);
int Lck__Owned(const struct lock *lck);

/* public interface: */
void Lck_Delete(struct lock *lck);
int Lck_CondWait(pthread_cond_t *cond, struct lock *lck, vtim_real);

#define Lck_New(a, b) Lck__New(a, b, #b)
#define Lck_Lock(a) Lck__Lock(a, __func__, __LINE__)
#define Lck_Unlock(a) Lck__Unlock(a, __func__, __LINE__)
#define Lck_Trylock(a) Lck__Trylock(a, __func__, __LINE__)
#define Lck_AssertHeld(a)		\
	do {				\
		assert(Lck__Held(a));	\
		assert(Lck__Owned(a));	\
	} while (0)

struct VSC_lck *Lck_CreateClass(struct vsc_seg **, const char *);
void Lck_DestroyClass(struct vsc_seg **);

#define LOCK(nam) extern struct VSC_lck *lck_##nam;
#include "tbl/locks.h"

/* cache_obj.c */

int ObjHasAttr(struct worker *, struct objcore *, enum obj_attr);
const void *ObjGetAttr(struct worker *, struct objcore *, enum obj_attr,
    ssize_t *len);

typedef int objiterate_f(void *priv, int flush, const void *ptr, ssize_t len);

int ObjIterate(struct worker *, struct objcore *,
    void *priv, objiterate_f *func, int final);

unsigned ObjGetXID(struct worker *, struct objcore *);
uint64_t ObjGetLen(struct worker *, struct objcore *);
int ObjGetDouble(struct worker *, struct objcore *, enum obj_attr, double *);
int ObjGetU32(struct worker *, struct objcore *, enum obj_attr, uint32_t *);
int ObjGetU64(struct worker *, struct objcore *, enum obj_attr, uint64_t *);
int ObjCheckFlag(struct worker *, struct objcore *, enum obj_flags of);
/* cache_session.c [SES] */

#define SESS_ATTR(UP, low, typ, len)					\
	int SES_Get_##low(const struct sess *sp, typ **dst);
#include "tbl/sess_attr.h"
const char *SES_Get_String_Attr(const struct sess *sp, enum sess_attr a);

/* cache_shmlog.c */
void VSLv(enum VSL_tag_e tag, uint32_t vxid, const char *fmt, va_list va);
void VSL(enum VSL_tag_e tag, uint32_t vxid, const char *fmt, ...)
    v_printflike_(3, 4);
void VSLbv(struct vsl_log *, enum VSL_tag_e tag, const char *fmt, va_list va);
void VSLb(struct vsl_log *, enum VSL_tag_e tag, const char *fmt, ...)
    v_printflike_(3, 4);
void VSLbt(struct vsl_log *, enum VSL_tag_e tag, txt t);
void VSLb_ts(struct vsl_log *, const char *event, double first, double *pprev,
    double now);
void VSLb_bin(struct vsl_log *, enum VSL_tag_e, ssize_t, const void*);

static inline void
VSLb_ts_req(struct req *req, const char *event, double now)
{

	if (isnan(req->t_first) || req->t_first == 0.)
		req->t_first = req->t_prev = now;
	VSLb_ts(req->vsl, event, req->t_first, &req->t_prev, now);
}

static inline void
VSLb_ts_busyobj(struct busyobj *bo, const char *event, double now)
{

	if (isnan(bo->t_first) || bo->t_first == 0.)
		bo->t_first = bo->t_prev = now;
	VSLb_ts(bo->vsl, event, bo->t_first, &bo->t_prev, now);
}

/* cache_vcl.c */
const char *VCL_Name(const struct vcl *);

/* cache_vrt.c */
/*
 * These prototypes go here, because we do not want to pollute vrt.h
 * with va_list.  VCC never generates direct calls to them.
 * XXX: We should deprecate these (ref: STRANDS)
 */
const char *VRT_String(struct ws *ws, const char *h, const char *p, va_list ap);
char *VRT_StringList(char *d, unsigned dl, const char *p, va_list ap);

/* cache_wrk.c */

typedef void *bgthread_t(struct worker *, void *priv);
void WRK_BgThread(pthread_t *thr, const char *name, bgthread_t *func,
    void *priv);

/* cache_ws.c */

void WS_Init(struct ws *ws, const char *id, void *space, unsigned len);

/* WS_Reserve(): Use WS_ReserveSize() or WS_ReserveAll() */
unsigned WS_Reserve(struct ws *ws, unsigned bytes);
unsigned WS_ReserveSize(struct ws *, unsigned);
unsigned WS_ReserveAll(struct ws *);
unsigned WS_ReserveLumps(struct ws *ws, size_t sz);
void WS_MarkOverflow(struct ws *ws);
void WS_Release(struct ws *ws, unsigned bytes);
void WS_ReleaseP(struct ws *ws, const char *ptr);
void WS_Assert(const struct ws *ws);
void WS_Reset(struct ws *ws, uintptr_t);
void *WS_Alloc(struct ws *ws, unsigned bytes);
void *WS_Copy(struct ws *ws, const void *str, int len);
uintptr_t WS_Snapshot(struct ws *ws);
int WS_Overflowed(const struct ws *ws);
void *WS_Printf(struct ws *ws, const char *fmt, ...) v_printflike_(2, 3);
int WS_Inside(const struct ws *, const void *, const void *);
void WS_Assert_Allocated(const struct ws *ws, const void *ptr, ssize_t len);

static inline char*
WS_Front(const struct ws *ws)
{
	return ws->f;
}

/* cache_rfc2616.c */
void RFC2616_Ttl(struct busyobj *, double now, double *t_origin,
    float *ttl, float *grace, float *keep);
unsigned RFC2616_Req_Gzip(const struct http *);
int RFC2616_Do_Cond(const struct req *sp);
void RFC2616_Weaken_Etag(struct http *hp);
void RFC2616_Vary_AE(struct http *hp);
void RFC2616_Response_Body(const struct worker *, const struct busyobj *);

/*
 * A normal pointer difference is signed, but we never want a negative value
 * so this little tool will make sure we don't get that.
 */

static inline unsigned
pdiff(const void *b, const void *e)
{

	assert(b <= e);
	return
	    ((unsigned)((const unsigned char *)e - (const unsigned char *)b));
}

#define Tcheck(t) do {						\
		AN((t).b);					\
		AN((t).e);					\
		assert((t).b <= (t).e);				\
	} while(0)

/*
 * unsigned length of a txt
 */

static inline unsigned
Tlen(const txt t)
{

	Tcheck(t);
	return ((unsigned)(t.e - t.b));
}

/*
 * We want to cache the most recent timestamp in wrk->lastused to avoid
 * extra timestamps in cache_pool.c.  Hide this detail with a macro
 */
#define W_TIM_real(w) ((w)->lastused = VTIM_real())

#define PAN_CheckMagic(vsb, ptr, exp)					\
	do {								\
		if ((ptr)->magic != (exp))				\
			VSB_printf((vsb),				\
			    "MAGIC at %p is 0x%08x (Should be: %s/0x%08x)\n", \
			    ptr, (ptr)->magic, #exp, exp);		\
	} while(0)
