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
 */

/*
 * This macro can be used in .h files to isolate bits that the manager
 * should not (need to) see, such as pthread mutexes etc.
 */
#define VARNISH_CACHE_CHILD	1

#include <stdarg.h>

#include "common/common.h"

#include "vapi/vsl_int.h"
#include "vapi/vsm_int.h"

#include "waiter/waiter.h"

#include <sys/socket.h>

#include <pthread.h>
#ifdef HAVE_PTHREAD_NP_H
#include <pthread_np.h>
#endif
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <math.h>

#include "common/params.h"

/*--------------------------------------------------------------------*/

enum req_fsm_nxt {
	REQ_FSM_MORE,
	REQ_FSM_DONE,
	REQ_FSM_DISEMBARK,
};

/*--------------------------------------------------------------------*/

enum body_status {
#define BODYSTATUS(U,l)	BS_##U,
#include "tbl/body_status.h"
#undef BODYSTATUS
};

/*--------------------------------------------------------------------*/

enum req_body_state_e {
#define REQ_BODY(U)	REQ_BODY_##U,
#include <tbl/req_body.h>
#undef REQ_BODY
};

/*--------------------------------------------------------------------*/

enum sess_close {
	SC_NULL = 0,
#define SESS_CLOSE(nm, stat, err, desc)	SC_##nm,
#include "tbl/sess_close.h"
#undef SESS_CLOSE
};

/*--------------------------------------------------------------------
 * Indicies into http->hd[]
 */
enum {
#define SLTH(tag, ind, req, resp, sdesc, ldesc)	ind,
#include "tbl/vsl_tags_http.h"
#undef SLTH
};

/*--------------------------------------------------------------------*/

struct VSC_C_lck;
struct ban;
struct backend;
struct busyobj;
struct cli;
struct cli_proto;
struct director;
struct iovec;
struct mempool;
struct objcore;
struct object;
struct objhead;
struct pool;
struct poolparam;
struct transport;
struct req;
struct sess;
struct suckaddr;
struct vrt_priv;
struct vsb;
struct waitinglist;
struct worker;
struct v1l;

#define DIGEST_LEN		32

/*--------------------------------------------------------------------*/

typedef struct {
	const char		*b;
	const char		*e;
} txt;

/*--------------------------------------------------------------------*/

enum req_step {
#define REQ_STEP(l, u, arg)	R_STP_##u,
#include "tbl/steps.h"
#undef REQ_STEP
};

enum fetch_step {
#define FETCH_STEP(l, U, arg)	F_STP_##U,
#include "tbl/steps.h"
#undef FETCH_STEP
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

/*--------------------------------------------------------------------
 * VFP filter state
 */

VTAILQ_HEAD(vfp_entry_s, vfp_entry);

struct vfp_ctx {
	unsigned		magic;
#define VFP_CTX_MAGIC		0x61d9d3e5
	struct busyobj		*bo;
	struct worker		*wrk;
	struct objcore		*oc;

	int			failed;

	struct vfp_entry_s	vfp;
	struct vfp_entry	*vfp_nxt;

	struct http		*http;
	struct http		*esi_req;
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

	int			fd;
	enum sess_close		doclose;
	unsigned		maxbytes;
	unsigned		maxhdr;
	enum body_status	body_status;
	struct ws		*ws;
	char			*rxbuf_b;
	char			*rxbuf_e;
	char			*pipeline_b;
	char			*pipeline_e;
	ssize_t			content_length;
	struct vfp_ctx		vfc[1];
	void			*priv;

	/* Timeouts */
	double			first_byte_timeout;
	double			between_bytes_timeout;
};

/*--------------------------------------------------------------------*/

struct acct_req {
#define ACCT(foo)	uint64_t	foo;
#include "tbl/acct_fields_req.h"
#undef ACCT
};

/*--------------------------------------------------------------------*/

struct acct_bereq {
#define ACCT(foo)	uint64_t	foo;
#include "tbl/acct_fields_bereq.h"
#undef ACCT
};

/*--------------------------------------------------------------------*/

#define L0(t, n)
#define L1(t, n)		t n;
#define VSC_F(n,t,l,s,f,v,d,e)	L##l(t, n)
struct dstat {
	unsigned		summs;
#include "tbl/vsc_f_main.h"
};
#undef VSC_F
#undef L0
#undef L1

/*--------------------------------------------------------------------*/

struct exp {
	double			t_origin;
	float			ttl;
	float			grace;
	float			keep;
};

/*--------------------------------------------------------------------*/

struct vsl_log {
	uint32_t		*wlb, *wlp, *wle;
	unsigned		wlr;
	unsigned		wid;
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

enum task_how {
	TASK_QUEUE_BO,
	TASK_QUEUE_REQ,
	TASK_QUEUE_VCA,
	TASK_QUEUE_END
};

/*--------------------------------------------------------------------*/

struct worker {
	unsigned		magic;
#define WORKER_MAGIC		0x6391adcf
	struct pool		*pool;
	struct objhead		*nobjhead;
	struct objcore		*nobjcore;
	struct waitinglist	*nwaitinglist;
	void			*nhashpriv;
	struct dstat		stats[1];
	struct vsl_log		*vsl;		// borrowed from req/bo

	struct pool_task	task;

	double			lastused;

	struct v1l		*v1l;

	pthread_cond_t		cond;

	struct vcl		*vcl;

	struct ws		aws[1];

	struct vxid_pool	vxid_pool;

	unsigned		cur_method;
	unsigned		seen_methods;
	unsigned		handling;

	uintptr_t		stack_start;
	uintptr_t		stack_end;
};

/* LRU ---------------------------------------------------------------*/

struct lru {
	unsigned		magic;
#define LRU_MAGIC		0x3fec7bb0
	VTAILQ_HEAD(,objcore)	lru_head;
	struct lock		mtx;
	unsigned		flags;
#define LRU_F_DONTMOVE		(1<<1)
#define LRU_F_CONDEMMED		(1<<2)
	unsigned		n_objcore;
};

/* Stored object -----------------------------------------------------
 * Pointer to a stored object, and the methods it supports
 */

struct storeobj {
	unsigned		magic;
#define STOREOBJ_MAGIC		0x6faed850
	const struct stevedore	*stevedore;
	void			*priv;
	uintptr_t		priv2;
};

/* Object core structure ---------------------------------------------
 * Objects have sideways references in the binary heap and the LRU list
 * and we want to avoid paging in a lot of objects just to move them up
 * or down the binheap or to move a unrelated object on the LRU list.
 * To avoid this we use a proxy object, objcore, to hold the relevant
 * housekeeping fields parts of an object.
 */

struct objcore {
	unsigned		magic;
#define OBJCORE_MAGIC		0x4d301302
	int			refcnt;
	struct storeobj		stobj[1];
	struct objhead		*objhead;
	struct busyobj		*busyobj;
	double			timer_when;
	long			hits;

	struct exp		exp;

	uint16_t		flags;
#define OC_F_BUSY		(1<<1)
#define OC_F_PASS		(1<<2)
#define OC_F_PRIVATE		(1<<8)
#define OC_F_FAILED		(1<<9)

	uint16_t		exp_flags;
#define OC_EF_OFFLRU		(1<<4)
#define OC_EF_MOVE		(1<<10)
#define OC_EF_INSERT		(1<<11)
#define OC_EF_EXP		(1<<12)
#define OC_EF_DYING		(1<<7)

	unsigned		timer_idx;
	VTAILQ_ENTRY(objcore)	list;
	VTAILQ_ENTRY(objcore)	lru_list;
	double			last_lru;
	VTAILQ_ENTRY(objcore)	ban_list;
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

/*
 * The macro-states we expose outside the fetch code
 */
enum busyobj_state_e {
	BOS_INVALID = 0,	/* don't touch (yet) */
	BOS_REQ_DONE,		/* beresp.* can be examined */
	BOS_STREAM,		/* beresp.* can be examined */
	BOS_FINISHED,		/* object is complete */
	BOS_FAILED,		/* something went wrong */
};

enum director_state_e {
	DIR_S_NULL = 0,
	DIR_S_HDRS = 1,
	DIR_S_BODY = 2,
};

struct busyobj {
	unsigned		magic;
#define BUSYOBJ_MAGIC		0x23b95567
	struct lock		mtx;
	pthread_cond_t		cond;
	char			*end;

	/*
	 * All fields from refcount and down are zeroed when the busyobj
	 * is recycled.
	 */
	unsigned		refcount;
	int			retries;
	struct req		*req;
	struct worker		*wrk;

	uint8_t			*vary;

	struct vfp_ctx		vfc[1];

	enum busyobj_state_e	state;

	struct ws		ws[1];
	char			*ws_bo;
	struct http		*bereq0;
	struct http		*bereq;
	struct http		*beresp;
	struct objcore		*stale_oc;
	struct objcore		*fetch_objcore;

	struct http_conn	*htc;

	struct pool_task	fetch_task;

#define BO_FLAG(l, r, w, d) unsigned	l:1;
#include "tbl/bo_flags.h"
#undef BO_FLAG

	/* Timeouts */
	double			connect_timeout;
	double			first_byte_timeout;
	double			between_bytes_timeout;

	/* Timers */
	double			t_first;	/* First timestamp logged */
	double			t_prev;		/* Previous timestamp logged */

	/* Acct */
	struct acct_bereq	acct;

	const char		*storage_hint;
	const struct director	*director_req;
	const struct director	*director_resp;
	enum director_state_e	director_state;
	struct vcl		*vcl;

	struct vsl_log		vsl[1];

	uint8_t			digest[DIGEST_LEN];
	struct vrt_privs	privs[1];
};


/*--------------------------------------------------------------------*/

VTAILQ_HEAD(vdp_entry_s, vdp_entry);

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
#undef REQ_FLAG

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

	ssize_t			req_bodybytes;	/* Parsed req bodybytes */

	const struct director	*director_hint;
	struct vcl		*vcl;

	char			*ws_req;	/* WS above request data */

	/* Timestamps */
	double			t_first;	/* First timestamp logged */
	double			t_prev;		/* Previous timestamp logged */
	double			t_req;		/* Headers complete */

	struct http_conn	htc[1];
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
	struct vdp_entry_s	vdp;
	struct vdp_entry	*vdp_nxt;
	unsigned		vdp_errval;

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
#include "tbl/sess_attr.h"
#undef SESS_ATTR
	SA_LAST
};

struct sess {
	unsigned		magic;
#define SESS_MAGIC		0x2c2f9c5a

	enum sess_step		sess_step;
	struct lock		mtx;
	int			fd;
	uint32_t		vxid;

	/* Cross references ------------------------------------------*/

	struct pool		*pool;

	/* Session related fields ------------------------------------*/

	struct ws		ws[1];

	uint16_t		sattr[SA_LAST];

	/* Timestamps, all on TIM_real() timescale */
	double			t_open;		/* fd accepted */
	double			t_idle;		/* fd accepted or resp sent */

	struct vrt_privs	privs[1];

};

/* Prototypes etc ----------------------------------------------------*/

/* Cross file typedefs */

typedef enum htc_status_e htc_complete_f(struct http_conn *);

/* cache_ban.c */
struct ban *BAN_New(void);
int BAN_AddTest(struct ban *, const char *, const char *, const char *);
void BAN_Free(struct ban *b);
char *BAN_Insert(struct ban *b);
void BAN_Free_Errormsg(char *);
void BAN_NewObjCore(struct objcore *oc);
void BAN_DestroyObj(struct objcore *oc);
int BAN_CheckObject(struct worker *, struct objcore *, struct req *);
void BAN_Reload(const uint8_t *ban, unsigned len);
struct ban *BAN_TailRef(void);
void BAN_Compile(void);
struct ban *BAN_RefBan(struct objcore *oc, double t0, const struct ban *tail);
void BAN_TailDeref(struct ban **ban);
double BAN_Time(const struct ban *ban);

/* cache_busyobj.c */
struct busyobj *VBO_GetBusyObj(struct worker *, const struct req *);
void VBO_DerefBusyObj(struct worker *wrk, struct busyobj **busyobj);
void VBO_extend(struct busyobj *, ssize_t);
ssize_t VBO_waitlen(struct worker *, struct busyobj *, ssize_t l);
void VBO_setstate(struct busyobj *bo, enum busyobj_state_e next);
void VBO_waitstate(struct busyobj *bo, enum busyobj_state_e want);

/* cache_req_body.c */
int VRB_Ignore(struct req *req);
ssize_t VRB_Cache(struct req *req, ssize_t maxsize);
typedef int (req_body_iter_f)(struct req *, void *priv, void *ptr, size_t);
ssize_t VRB_Iterate(struct req *req, req_body_iter_f *func, void *priv);
void VRB_Free(struct req *req);

/* cache_req_fsm.c [CNT] */
enum req_fsm_nxt CNT_Request(struct worker *, struct req *);
void CNT_AcctLogCharge(struct dstat *, struct req *);

/* cache_cli.c [CLI] */
extern pthread_t cli_thread;
#define ASSERT_CLI() do {assert(pthread_self() == cli_thread);} while (0)

/* cache_expire.c */
void EXP_Clr(struct exp *e);

double EXP_Ttl(const struct req *, const struct exp*);
double EXP_When(const struct exp *exp);
void EXP_Insert(struct worker *wrk, struct objcore *oc);
void EXP_Inject(struct worker *wrk, struct objcore *oc, struct lru *lru);
void EXP_Rearm(struct objcore *, double now, double ttl, double grace,
    double keep);
void EXP_Touch(struct objcore *oc, double now);
int EXP_NukeOne(struct worker *wrk, struct lru *lru);

enum exp_event_e {
	EXP_INSERT,
	EXP_INJECT,
	EXP_REMOVE,
};
typedef void exp_callback_f(struct worker *, struct objcore *,
    enum exp_event_e, void *priv);

uintptr_t EXP_Register_Callback(exp_callback_f *func, void *priv);
void EXP_Deregister_Callback(uintptr_t*);

/* cache_fetch.c */
enum vbf_fetch_mode_e {
	VBF_NORMAL = 0,
	VBF_PASS = 1,
	VBF_BACKGROUND = 2,
};
void VBF_Fetch(struct worker *wrk, struct req *req,
    struct objcore *oc, struct objcore *oldoc, enum vbf_fetch_mode_e);

/* cache_gzip.c */
struct vgz;

enum vgzret_e {
	VGZ_ERROR = -1,
	VGZ_OK = 0,
	VGZ_END = 1,
	VGZ_STUCK = 2,
};

enum vgz_flag { VGZ_NORMAL, VGZ_ALIGN, VGZ_RESET, VGZ_FINISH };
// struct vgz *VGZ_NewUngzip(struct vsl_log *vsl, const char *id);
struct vgz *VGZ_NewGzip(struct vsl_log *vsl, const char *id);
void VGZ_Ibuf(struct vgz *, const void *, ssize_t len);
int VGZ_IbufEmpty(const struct vgz *vg);
void VGZ_Obuf(struct vgz *, void *, ssize_t len);
int VGZ_ObufFull(const struct vgz *vg);
enum vgzret_e VGZ_Gzip(struct vgz *, const void **, ssize_t *len,
    enum vgz_flag);
// enum vgzret_e VGZ_Gunzip(struct vgz *, const void **, ssize_t *len);
enum vgzret_e VGZ_Destroy(struct vgz **);

enum vgz_ua_e {
	VUA_UPDATE,		// Update start/stop/last bits if changed
	VUA_END_GZIP,		// Record uncompressed size
	VUA_END_GUNZIP,		// Record uncompressed size
};

void VGZ_UpdateObj(const struct vfp_ctx *, struct vgz*, enum vgz_ua_e);

/* cache_http.c */
unsigned HTTP_estimate(unsigned nhttp);
void HTTP_Copy(struct http *to, const struct http * const fm);
struct http *HTTP_create(void *p, uint16_t nhttp);
const char *http_Status2Reason(unsigned);
unsigned http_EstimateWS(const struct http *fm, unsigned how);
void http_PutResponse(struct http *to, const char *proto, uint16_t status,
    const char *response);
void http_FilterReq(struct http *to, const struct http *fm, unsigned how);
void HTTP_Encode(const struct http *fm, uint8_t *, unsigned len, unsigned how);
int HTTP_Decode(struct http *to, const uint8_t *fm);
void http_ForceHeader(struct http *to, const char *hdr, const char *val);
void http_PrintfHeader(struct http *to, const char *fmt, ...)
    __v_printflike(2, 3);
void http_TimeHeader(struct http *to, const char *fmt, double now);
void http_SetHeader(struct http *to, const char *hdr);
void http_SetH(const struct http *to, unsigned n, const char *fm);
void http_ForceField(const struct http *to, unsigned n, const char *t);
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
void http_VSL_log(const struct http *hp);
void HTTP_Merge(struct worker *, struct objcore *, struct http *to);
uint16_t HTTP_GetStatusPack(struct worker *, struct objcore *oc);
int HTTP_IterHdrPack(struct worker *, struct objcore *, const char **);
#define HTTP_FOREACH_PACK(wrk, oc, ptr) \
	 for ((ptr) = NULL; HTTP_IterHdrPack(wrk, oc, &(ptr));)
const char *HTTP_GetHdrPack(struct worker *, struct objcore *, const char *hdr);
enum sess_close http_DoConnection(struct http *hp);

/* cache_http1_proto.c */

htc_complete_f HTTP1_Complete;
uint16_t HTTP1_DissectRequest(struct http_conn *, struct http *);
uint16_t HTTP1_DissectResponse(struct http_conn *, struct http *);
unsigned HTTP1_Write(const struct worker *w, const struct http *hp, const int*);

#define HTTPH(a, b, c) extern char b[];
#include "tbl/http_headers.h"
#undef HTTPH
extern const char H__Status[];
extern const char H__Proto[];
extern const char H__Reason[];

/* cache_main.c */
#define VXID(u) ((u) & VSL_IDENTMASK)
uint32_t VXID_Get(struct worker *, uint32_t marker);
extern volatile struct params * cache_param;
extern pthread_key_t witness_key;

/* cache_lck.c */

/* Internal functions, call only through macros below */
void Lck__Lock(struct lock *lck, const char *p,  int l);
void Lck__Unlock(struct lock *lck, const char *p,  int l);
int Lck__Trylock(struct lock *lck, const char *p,  int l);
void Lck__New(struct lock *lck, struct VSC_C_lck *, const char *);
void Lck__Assert(const struct lock *lck, int held);

/* public interface: */
void Lck_Delete(struct lock *lck);
int Lck_CondWait(pthread_cond_t *cond, struct lock *lck, double);

#define Lck_New(a, b) Lck__New(a, b, #b)
#define Lck_Lock(a) Lck__Lock(a, __func__, __LINE__)
#define Lck_Unlock(a) Lck__Unlock(a, __func__, __LINE__)
#define Lck_Trylock(a) Lck__Trylock(a, __func__, __LINE__)
#define Lck_AssertHeld(a) Lck__Assert(a, 1)

#define LOCK(nam) extern struct VSC_C_lck *lck_##nam;
#include "tbl/locks.h"
#undef LOCK

/* cache_mempool.c */
void MPL_AssertSane(void *item);
struct mempool * MPL_New(const char *name, volatile struct poolparam *pp,
    volatile unsigned *cur_size);
void MPL_Destroy(struct mempool **mpp);
void *MPL_Get(struct mempool *mpl, unsigned *size);
void MPL_Free(struct mempool *mpl, void *item);

/* cache_obj.c */
enum objiter_status {
	OIS_DONE,
	OIS_DATA,
	OIS_STREAM,
	OIS_ERROR,
};
void *ObjIterBegin(struct worker *, struct objcore *);
enum objiter_status ObjIter(struct objcore *, void *, void **, ssize_t *);
void ObjIterEnd(struct objcore *, void **);
int ObjGetSpace(struct worker *, struct objcore *, ssize_t *sz, uint8_t **ptr);
void ObjExtend(struct worker *, struct objcore *, ssize_t l);
void ObjTrimStore(struct worker *, struct objcore *);
unsigned ObjGetXID(struct worker *, struct objcore *);
uint64_t ObjGetLen(struct worker *, struct objcore *oc);
void ObjUpdateMeta(struct worker *, struct objcore *);
void ObjFreeObj(struct worker *, struct objcore *);
void ObjSlim(struct worker *, struct objcore *oc);
struct lru *ObjGetLRU(const struct objcore *);
void *ObjGetattr(struct worker *wrk, struct objcore *oc, enum obj_attr attr,
    ssize_t *len);
void *ObjSetattr(struct worker *, struct objcore *, enum obj_attr attr,
    ssize_t len, const void *);
int ObjCopyAttr(struct worker *, struct objcore *, struct objcore *,
    enum obj_attr attr);

int ObjSetDouble(struct worker *, struct objcore *, enum obj_attr, double);
int ObjSetU32(struct worker *, struct objcore *, enum obj_attr, uint32_t);
int ObjSetU64(struct worker *, struct objcore *, enum obj_attr, uint64_t);

int ObjGetDouble(struct worker *, struct objcore *, enum obj_attr, double *);
int ObjGetU32(struct worker *, struct objcore *, enum obj_attr, uint32_t *);
int ObjGetU64(struct worker *, struct objcore *, enum obj_attr, uint64_t *);

int ObjCheckFlag(struct worker *, struct objcore *oc, enum obj_flags of);
void ObjSetFlag(struct worker *, struct objcore *, enum obj_flags of, int val);

/* cache_panic.c */
const char *body_status_2str(enum body_status e);
const char *sess_close_2str(enum sess_close sc, int want_desc);

/* cache_pool.c */
int Pool_Task(struct pool *pp, struct pool_task *task, enum task_how how);
int Pool_Task_Arg(struct worker *, task_func_t *,
    const void *arg, size_t arg_len);
void Pool_Sumstat(struct worker *w);
int Pool_TrySumstat(struct worker *wrk);
void Pool_PurgeStat(unsigned nobj);
int Pool_Task_Any(struct pool_task *task, enum task_how how);

/* cache_range.c [VRG] */
void VRG_dorange(struct req *req, const char *r);

/* cache_req.c */
struct req *Req_New(const struct worker *, struct sess *);
void Req_Release(struct req *);
int Req_Cleanup(struct sess *sp, struct worker *wrk, struct req *req);
void Req_Fail(struct req *req, enum sess_close reason);

/* cache_session.c [SES] */
struct sess *SES_New(struct pool *);
void SES_Close(struct sess *sp, enum sess_close reason);
void SES_Wait(struct sess *sp);
void SES_Delete(struct sess *sp, enum sess_close reason, double now);
void SES_NewPool(struct pool *pp, unsigned pool_no);
int SES_Reschedule_Req(struct req *);
task_func_t SES_Proto_Sess;
task_func_t SES_Proto_Req;

enum htc_status_e {
	HTC_S_JUNK =		-5,
	HTC_S_CLOSE =		-4,
	HTC_S_TIMEOUT =		-3,
	HTC_S_OVERFLOW =	-2,
	HTC_S_EOF =		-1,
	HTC_S_EMPTY =		 0,
	HTC_S_MORE =		 1,
	HTC_S_COMPLETE =	 2,
	HTC_S_IDLE =		 3,
};

void SES_RxInit(struct http_conn *htc, struct ws *ws,
    unsigned maxbytes, unsigned maxhdr);
void SES_RxReInit(struct http_conn *htc);
enum htc_status_e SES_RxStuff(struct http_conn *, htc_complete_f *,
    double *t1, double *t2, double ti, double tn);

#define SESS_ATTR(UP, low, typ, len)				\
	int SES_Get_##low(const struct sess *sp, typ *dst);	\
	void SES_Reserve_##low(struct sess *sp, typ *dst);
#include "tbl/sess_attr.h"
#undef SESS_ATTR
void SES_Set_String_Attr(struct sess *sp, enum sess_attr a, const char *src);
const char *SES_Get_String_Attr(const struct sess *sp, enum sess_attr a);

/* cache_shmlog.c */
extern struct VSC_C_main *VSC_C_main;
void *VSM_Alloc(unsigned size, const char *class, const char *type,
    const char *ident);
void VSM_Free(void *ptr);
#ifdef VSL_ENDMARKER
void VSL(enum VSL_tag_e tag, uint32_t vxid, const char *fmt, ...)
    __v_printflike(3, 4);
void VSLbv(struct vsl_log *, enum VSL_tag_e tag, const char *fmt, va_list va);
void VSLb(struct vsl_log *, enum VSL_tag_e tag, const char *fmt, ...)
    __v_printflike(3, 4);
void VSLbt(struct vsl_log *, enum VSL_tag_e tag, txt t);
void VSLb_ts(struct vsl_log *, const char *event, double first, double *pprev,
    double now);

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


void VSL_Flush(struct vsl_log *, int overflow);

#endif

/* cache_vary.c */
int VRY_Create(struct busyobj *bo, struct vsb **psb);
int VRY_Match(struct req *, const uint8_t *vary);
void VRY_Prep(struct req *);
void VRY_Clear(struct req *);
enum vry_finish_flag { KEEP, DISCARD };
void VRY_Finish(struct req *req, enum vry_finish_flag);

/* cache_vcl.c */
const char *VCL_Method_Name(unsigned);
const char *VCL_Name(const struct vcl *);
void VCL_Ref(struct vcl *);
void VCL_Refresh(struct vcl **);
void VCL_Rel(struct vcl **);
const char *VCL_Return_Name(unsigned);

#define VCL_MET_MAC(l,u,b) \
    void VCL_##l##_method(struct vcl *, struct worker *, struct req *, \
	struct busyobj *bo, void *specific);
#include "tbl/vcl_returns.h"
#undef VCL_MET_MAC

/* cache_vrt.c */

/*
 * These prototypes go here, because we do not want to pollute vrt.h
 * with va_list.  VCC never generates direct calls to them.
 */
const char *VRT_String(struct ws *ws, const char *h, const char *p, va_list ap);
char *VRT_StringList(char *d, unsigned dl, const char *p, va_list ap);

/* cache_wrk.c */

typedef void *bgthread_t(struct worker *, void *priv);
void WRK_BgThread(pthread_t *thr, const char *name, bgthread_t *func,
    void *priv);

/* cache_ws.c */

void WS_Init(struct ws *ws, const char *id, void *space, unsigned len);
unsigned WS_Reserve(struct ws *ws, unsigned bytes);
void WS_MarkOverflow(struct ws *ws);
void WS_Release(struct ws *ws, unsigned bytes);
void WS_ReleaseP(struct ws *ws, char *ptr);
void WS_Assert(const struct ws *ws);
void WS_Reset(struct ws *ws, char *p);
void *WS_Alloc(struct ws *ws, unsigned bytes);
void *WS_Copy(struct ws *ws, const void *str, int len);
char *WS_Snapshot(struct ws *ws);
int WS_Overflowed(const struct ws *ws);
void *WS_Printf(struct ws *ws, const char *fmt, ...) __v_printflike(2, 3);

/* cache_rfc2616.c */
void RFC2616_Ttl(struct busyobj *, double now);
unsigned RFC2616_Req_Gzip(const struct http *);
int RFC2616_Do_Cond(const struct req *sp);
void RFC2616_Weaken_Etag(struct http *hp);
void RFC2616_Vary_AE(struct http *hp);

/* stevedore.c */
int STV_NewObject(struct objcore *, struct worker *,
    const char *hint, unsigned len);
struct storage *STV_alloc(const struct stevedore *, size_t size);
void STV_trim(const struct stevedore *, struct storage *, size_t size,
    int move_ok);
void STV_free(const struct stevedore *, struct storage *st);
void STV_open(void);
void STV_close(void);
int STV_BanInfo(enum baninfo event, const uint8_t *ban, unsigned len);
void STV_BanExport(const uint8_t *bans, unsigned len);

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

static inline void
Tcheck(const txt t)
{

	AN(t.b);
	AN(t.e);
	assert(t.b <= t.e);
}

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

static inline int
FEATURE(enum feature_bits x)
{
	return (cache_param->feature_bits[(unsigned)x>>3] &
	    (0x80U >> ((unsigned)x & 7)));
}

static inline int
DO_DEBUG(enum debug_bits x)
{
	return (cache_param->debug_bits[(unsigned)x>>3] &
	    (0x80U >> ((unsigned)x & 7)));
}

#define DSL(debug_bit, id, ...)					\
	do {							\
		if (DO_DEBUG(debug_bit))			\
			VSL(SLT_Debug, (id), __VA_ARGS__);	\
	} while (0)

#ifdef VARNISHD_IS_NOT_A_VMOD
#  include "cache/cache_priv.h"
#endif

