/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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

#include "common/common.h"

#include "cache/cache_filter.h"
#include "vapi/vsl_int.h"

#include <sys/socket.h>

#include <pthread.h>
#ifdef HAVE_PTHREAD_NP_H
#include <pthread_np.h>
#endif
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <math.h>

#if defined(HAVE_EPOLL_CTL)
#include <sys/epoll.h>
#endif

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
#define SESS_CLOSE(nm, desc)	SC_##nm,
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

struct SHA256Context;
struct VSC_C_lck;
struct ban;
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
struct req;
struct sess;
struct sesspool;
struct vbc;
struct vrt_backend;
struct vsb;
struct waitinglist;
struct worker;
struct wrw;

#define DIGEST_LEN		32

/*--------------------------------------------------------------------*/

typedef struct {
	char			*b;
	char			*e;
} txt;

/*--------------------------------------------------------------------*/

enum sess_step {
#define SESS_STEP(l, u)		S_STP_##u,
#include "tbl/steps.h"
#undef SESS_STEP
};

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

struct vfp_entry {
	unsigned		magic;
#define VFP_ENTRY_MAGIC		0xbe32a027
	const struct vfp	*vfp;
	void			*priv1;
	intptr_t		priv2;
	enum vfp_status		closed;
	VTAILQ_ENTRY(vfp_entry)	list;
	uint64_t		calls;
	uint64_t		bytes_out;
};

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
	struct vsl_log		*vsl;
	unsigned		maxbytes;
	unsigned		maxhdr;
	struct ws		*ws;
	txt			rxbuf;
	txt			pipeline;
	ssize_t			content_length;
	enum body_status	body_status;
	struct vfp_ctx		vfc[1];
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
#define VSC_F(n,t,l,f,v,e,d)	L##l(t, n)
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

struct wrk_accept {
	unsigned		magic;
#define WRK_ACCEPT_MAGIC	0x8c4b4d59

	/* Accept stuff */
	struct sockaddr_storage	acceptaddr;
	socklen_t		acceptaddrlen;
	int			acceptsock;
	struct listen_sock	*acceptlsock;
};

/* Worker pool stuff -------------------------------------------------*/

typedef void pool_func_t(struct worker *wrk, void *priv);

struct pool_task {
	VTAILQ_ENTRY(pool_task)		list;
	pool_func_t			*func;
	void				*priv;
};

enum pool_how {
	POOL_NO_QUEUE,
	POOL_QUEUE_FRONT,
	POOL_QUEUE_BACK
};

/*--------------------------------------------------------------------*/

struct worker {
	unsigned		magic;
#define WORKER_MAGIC		0x6391adcf
	struct pool		*pool;
	struct objhead		*nobjhead;
	struct objcore		*nobjcore;
	struct waitinglist	*nwaitinglist;
	struct busyobj		*nbo;
	void			*nhashpriv;
	struct dstat		stats[1];
	struct vsl_log		*vsl;		// borrowed from req/bo

	struct pool_task	task;

	double			lastused;

	struct wrw		*wrw;

	pthread_cond_t		cond;

	struct VCL_conf		*vcl;

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
	struct stevedore	*stevedore;
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
	struct vbc		*vbc;
	struct http		*bereq0;
	struct http		*bereq;
	struct http		*beresp;
	struct objcore		*ims_oc;
	struct objcore		*fetch_objcore;

	struct http_conn	htc[1];

	struct pool_task	fetch_task;

	enum sess_close		doclose;

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
	struct VCL_conf		*vcl;

	struct vsl_log		vsl[1];

	struct vsb		*synth_body;

	uint8_t			digest[DIGEST_LEN];
};


/*--------------------------------------------------------------------*/

struct req {
	unsigned		magic;
#define REQ_MAGIC		0x2751aaa1

	int			restarts;
	int			esi_level;
	int			disable_esi;
	uint8_t			hash_ignore_busy;
	uint8_t			hash_always_miss;

	struct sess		*sp;
	struct worker		*wrk;
	enum req_step		req_step;
	VTAILQ_ENTRY(req)	w_list;

	volatile enum req_body_state_e	req_body_status;
	struct objcore		*body_oc;

	/* The busy objhead we sleep on */
	struct objhead		*hash_objhead;

	/* Built Vary string */
	uint8_t			*vary_b;
	uint8_t			*vary_l;
	uint8_t			*vary_e;

	uint8_t			digest[DIGEST_LEN];

	enum sess_close		doclose;
	double			d_ttl;

	unsigned char		wantbody;
	uint64_t		req_bodybytes;	/* Parsed req bodybytes */

	uint64_t		resp_hdrbytes;	/* Scheduled resp hdrbytes */
	uint64_t		resp_bodybytes; /* Scheduled resp bodybytes */

	uint16_t		err_code;
	const char		*err_reason;

	const struct director	*director_hint;
	struct VCL_conf		*vcl;

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
	struct http		*resp;

	struct ws		ws[1];
	struct objcore		*objcore;
	struct objcore		*ims_oc;
	/* Lookup stuff */
	struct SHA256Context	*sha256ctx;

	/* ESI delivery stuff */
	int			gzip_resp;
	ssize_t			l_crc;
	uint32_t		crc;

	/* Delivery mode */
	unsigned		res_mode;
#define RES_LEN			(1<<1)
#define RES_EOF			(1<<2)
#define RES_CHUNKED		(1<<3)
#define RES_ESI			(1<<4)
#define RES_ESI_CHILD		(1<<5)
#define RES_GUNZIP		(1<<6)
#define RES_PIPE		(1<<7)

	/* Deliver pipeline */
#define	N_VDPS			5
	vdp_bytes		*vdps[N_VDPS];
	int			vdp_nxt;

	/* Range */
	ssize_t			range_low;
	ssize_t			range_high;
	ssize_t			range_off;

	/* Gunzip */
	struct vgz		*vgz;

	/* Transaction VSL buffer */
	struct vsl_log		vsl[1];

	/* Temporary accounting */
	struct acct_req		acct;

	/* Synth content in vcl_synth */
	struct vsb		*synth_body;
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

struct sess {
	unsigned		magic;
#define SESS_MAGIC		0x2c2f9c5a

	enum sess_step		sess_step;
	struct lock		mtx;
	int			fd;
	enum sess_close		reason;
	uint32_t		vxid;

	/* Cross references ------------------------------------------*/

	struct sesspool		*sesspool;

	struct pool_task	task;
	VTAILQ_ENTRY(sess)	list;

	/* Session related fields ------------------------------------*/

	struct ws		ws[1];

	/*
	 * This gets quite involved, but we don't want to waste space
	 * on up to 4 pointers of 8 bytes in struct sess.
	 */
	char			*addrs;
#define sess_remote_addr(sp) \
	((struct suckaddr *)(void*)((sp)->addrs))
#define sess_local_addr(sp) \
	((struct suckaddr *)(void*)((sp)->addrs + vsa_suckaddr_len))

	/* formatted ascii client address */
	char			*client_addr_str;
	char			*client_port_str;


	/* Timestamps, all on TIM_real() timescale */
	double			t_open;		/* fd accepted */
	double			t_idle;		/* fd accepted or resp sent */

#if defined(HAVE_EPOLL_CTL)
	struct epoll_event ev;
#endif
};

/* Prototypes etc ----------------------------------------------------*/

/* cache_acceptor.c */
void VCA_Init(void);
void VCA_Shutdown(void);
int VCA_Accept(struct listen_sock *ls, struct wrk_accept *wa);
const char *VCA_SetupSess(struct worker *w, struct sess *sp);
void VCA_FailSess(struct worker *w);

/* cache_ban.c */
struct ban *BAN_New(void);
int BAN_AddTest(struct ban *, const char *, const char *, const char *);
void BAN_Free(struct ban *b);
char *BAN_Insert(struct ban *b);
void BAN_Free_Errormsg(char *);
void BAN_Init(void);
void BAN_Shutdown(void);
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
void VBO_Init(void);
struct busyobj *VBO_GetBusyObj(struct worker *, const struct req *);
void VBO_DerefBusyObj(struct worker *wrk, struct busyobj **busyobj);
void VBO_Free(struct busyobj **vbo);
void VBO_extend(struct busyobj *, ssize_t);
ssize_t VBO_waitlen(struct worker *, struct busyobj *, ssize_t l);
void VBO_setstate(struct busyobj *bo, enum busyobj_state_e next);
void VBO_waitstate(struct busyobj *bo, enum busyobj_state_e want);


/* cache_http1_fetch.c [V1F] */
int V1F_fetch_hdr(struct worker *wrk, struct busyobj *bo);
void V1F_Setup_Fetch(struct vfp_ctx *vfc, struct http_conn *htc);

/* cache_http1_fsm.c [HTTP1] */
typedef int (req_body_iter_f)(struct req *, void *priv, void *ptr, size_t);
void HTTP1_Session(struct worker *, struct req *);
extern const int HTTP1_Req[3];
extern const int HTTP1_Resp[3];

/* cache_http1_deliver.c */
unsigned V1D_FlushReleaseAcct(struct req *req);
void V1D_Deliver(struct req *, struct busyobj *);
void V1D_Deliver_Synth(struct req *req);

/* cache_req_body.c */
int VRB_Ignore(struct req *req);
int VRB_Cache(struct req *req, ssize_t maxsize);
int VRB_Iterate(struct req *req, req_body_iter_f *func, void *priv);
void VRB_Free(struct req *req);

static inline int
VDP_bytes(struct req *req, enum vdp_action act, const void *ptr, ssize_t len)
{
	int i, retval;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	assert(act > VDP_NULL || len > 0);
	/* Call the present layer, while pointing to the next layer down */
	i = req->vdp_nxt--;
	assert(i >= 0 && i < N_VDPS);
	retval = req->vdps[i](req, act, ptr, len);
	req->vdp_nxt++;
	return (retval);
}

static inline void
VDP_push(struct req *req, vdp_bytes *func)
{
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AN(func);

	/* Push another layer */
	assert(req->vdp_nxt >= 0);
	assert(req->vdp_nxt + 1 < N_VDPS);
	req->vdps[++req->vdp_nxt] = func;
}

static inline void
VDP_pop(struct req *req, vdp_bytes *func)
{
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	/* Pop top layer */
	assert(req->vdp_nxt >= 1);
	assert(req->vdp_nxt < N_VDPS);
	assert(req->vdps[req->vdp_nxt] == func);
	req->vdp_nxt--;
}

/* cache_req_fsm.c [CNT] */
enum req_fsm_nxt CNT_Request(struct worker *, struct req *);
void CNT_AcctLogCharge(struct dstat *, struct req *);

/* cache_cli.c [CLI] */
void CLI_Init(void);
void CLI_Run(void);
void CLI_AddFuncs(struct cli_proto *p);
extern pthread_t cli_thread;
#define ASSERT_CLI() do {assert(pthread_self() == cli_thread);} while (0)

/* cache_expire.c */
void EXP_Clr(struct exp *e);

double EXP_Ttl(const struct req *, const struct exp*);
double EXP_When(const struct exp *exp);
void EXP_Insert(struct objcore *oc);
void EXP_Inject(struct objcore *oc, struct lru *lru);
void EXP_Init(void);
void EXP_Rearm(struct objcore *, double now, double ttl, double grace,
    double keep);
void EXP_Touch(struct objcore *oc, double now);
int EXP_NukeOne(struct worker *wrk, struct lru *lru);

/* cache_fetch.c */
enum vbf_fetch_mode_e {
	VBF_NORMAL = 0,
	VBF_PASS = 1,
	VBF_BACKGROUND = 2,
};
void VBF_Fetch(struct worker *wrk, struct req *req,
    struct objcore *oc, struct objcore *oldoc, enum vbf_fetch_mode_e);

/* cache_fetch_proc.c */
enum vfp_status VFP_GetStorage(struct vfp_ctx *, ssize_t *sz, uint8_t **ptr);
void VFP_Init(void);

/* cache_gzip.c */
struct vgz;

enum vgzret_e {
	VGZ_ERROR = -1,
	VGZ_OK = 0,
	VGZ_END = 1,
	VGZ_STUCK = 2,
};

enum vgz_flag { VGZ_NORMAL, VGZ_ALIGN, VGZ_RESET, VGZ_FINISH };
struct vgz *VGZ_NewUngzip(struct vsl_log *vsl, const char *id);
struct vgz *VGZ_NewGzip(struct vsl_log *vsl, const char *id);
void VGZ_Ibuf(struct vgz *, const void *, ssize_t len);
int VGZ_IbufEmpty(const struct vgz *vg);
void VGZ_Obuf(struct vgz *, void *, ssize_t len);
int VGZ_ObufFull(const struct vgz *vg);
enum vgzret_e VGZ_Gzip(struct vgz *, const void **, ssize_t *len,
    enum vgz_flag);
enum vgzret_e VGZ_Gunzip(struct vgz *, const void **, ssize_t *len);
enum vgzret_e VGZ_Destroy(struct vgz **);
void VGZ_UpdateObj(const struct vfp_ctx *, const struct vgz*);
vdp_bytes VDP_gunzip;

int VGZ_WrwInit(struct vgz *vg);
enum vgzret_e VGZ_WrwGunzip(struct req *, struct vgz *, const void *ibuf,
    ssize_t ibufl);
void VGZ_WrwFlush(struct req *, struct vgz *vg);

/* cache_http.c */
unsigned HTTP_estimate(unsigned nhttp);
void HTTP_Copy(struct http *to, const struct http * const fm);
struct http *HTTP_create(void *p, uint16_t nhttp);
const char *http_Status2Reason(unsigned);
unsigned http_EstimateWS(const struct http *fm, unsigned how);
void HTTP_Init(void);
void http_PutResponse(struct http *to, const char *proto, uint16_t status,
    const char *response);
void http_FilterReq(struct http *to, const struct http *fm, unsigned how);
void HTTP_Encode(const struct http *fm, uint8_t *, unsigned len, unsigned how);
int HTTP_Decode(struct http *to, uint8_t *fm);
void http_ForceHeader(struct http *to, const char *hdr, const char *val);
void http_PrintfHeader(struct http *to, const char *fmt, ...)
    __printflike(2, 3);
void http_SetHeader(struct http *to, const char *hdr);
void http_SetH(const struct http *to, unsigned n, const char *fm);
void http_ForceField(const struct http *to, unsigned n, const char *t);
void HTTP_Setup(struct http *, struct ws *, struct vsl_log *, enum VSL_tag_e);
void http_Teardown(struct http *ht);
int http_GetHdr(const struct http *hp, const char *hdr, char **ptr);
int http_GetHdrToken(const struct http *hp, const char *hdr,
    const char *token, char **ptr);
int http_GetHdrField(const struct http *hp, const char *hdr,
    const char *field, char **ptr);
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
const char *HTTP_GetHdrPack(struct worker *, struct objcore *,
    const char *hdr);
enum sess_close http_DoConnection(struct http *hp);

/* cache_http1_proto.c */

enum http1_status_e {
	HTTP1_ALL_WHITESPACE =	-3,
	HTTP1_OVERFLOW =	-2,
	HTTP1_ERROR_EOF =	-1,
	HTTP1_NEED_MORE =	 0,
	HTTP1_COMPLETE =	 1
};

void HTTP1_Init(struct http_conn *htc, struct ws *ws, int fd, struct vsl_log *,
    unsigned maxbytes, unsigned maxhdr);
enum http1_status_e HTTP1_Reinit(struct http_conn *htc);
enum http1_status_e HTTP1_Rx(struct http_conn *htc);
ssize_t HTTP1_Read(struct http_conn *htc, void *d, size_t len);
enum http1_status_e HTTP1_Complete(struct http_conn *htc);
uint16_t HTTP1_DissectRequest(struct http_conn *htc, struct http *hp);
uint16_t HTTP1_DissectResponse(struct http *sp, struct http_conn *htc);
unsigned HTTP1_Write(const struct worker *w, const struct http *hp, const int*);

#define HTTPH(a, b, c) extern char b[];
#include "tbl/http_headers.h"
#undef HTTPH

/* cache_main.c */
#define VXID(u) ((u) & VSL_IDENTMASK)
uint32_t VXID_Get(struct worker *, uint32_t marker);
extern volatile struct params * cache_param;
void THR_SetName(const char *name);
const char* THR_GetName(void);
void THR_SetBusyobj(const struct busyobj *);
struct busyobj * THR_GetBusyobj(void);
void THR_SetRequest(const struct req *);
struct req * THR_GetRequest(void);

/* cache_lck.c */

/* Internal functions, call only through macros below */
void Lck__Lock(struct lock *lck, const char *p, const char *f, int l);
void Lck__Unlock(struct lock *lck, const char *p, const char *f, int l);
int Lck__Trylock(struct lock *lck, const char *p, const char *f, int l);
void Lck__New(struct lock *lck, struct VSC_C_lck *, const char *);
void Lck__Assert(const struct lock *lck, int held);

/* public interface: */
void LCK_Init(void);
void Lck_Delete(struct lock *lck);
int Lck_CondWait(pthread_cond_t *cond, struct lock *lck, double);

#define Lck_New(a, b) Lck__New(a, b, #b)
#define Lck_Lock(a) Lck__Lock(a, __func__, __FILE__, __LINE__)
#define Lck_Unlock(a) Lck__Unlock(a, __func__, __FILE__, __LINE__)
#define Lck_Trylock(a) Lck__Trylock(a, __func__, __FILE__, __LINE__)
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
void PAN_Init(void);
const char *body_status_2str(enum body_status e);
const char *sess_close_2str(enum sess_close sc, int want_desc);

/* cache_pipe.c */
void Pipe_Init(void);
void PipeRequest(struct req *req, struct busyobj *bo);

/* cache_pool.c */
void Pool_Init(void);
void Pool_Accept(void);
void Pool_Work_Thread(struct pool *, struct worker *w);
int Pool_Task(struct pool *pp, struct pool_task *task, enum pool_how how);
void Pool_Sumstat(struct worker *w);
void Pool_PurgeStat(unsigned nobj);

#define WRW_IsReleased(w)	((w)->wrw == NULL)
int WRW_Error(const struct worker *w);
void WRW_Chunked(const struct worker *w);
void WRW_EndChunk(const struct worker *w);
void WRW_Reserve(struct worker *w, int *fd, struct vsl_log *, double t0);
unsigned WRW_Flush(const struct worker *w);
unsigned WRW_FlushRelease(struct worker *w, uint64_t *pacc);
unsigned WRW_Write(const struct worker *w, const void *ptr, int len);
unsigned WRW_WriteH(const struct worker *w, const txt *hh, const char *suf);

/* cache_session.c [SES] */
void SES_Close(struct sess *sp, enum sess_close reason);
void SES_Delete(struct sess *sp, enum sess_close reason, double now);
struct sesspool *SES_NewPool(struct pool *pp, unsigned pool_no);
void SES_DeletePool(struct sesspool *sp);
int SES_ScheduleReq(struct req *);
struct req *SES_GetReq(const struct worker *, struct sess *);
void SES_Handle(struct sess *sp, double now);
void SES_ReleaseReq(struct req *);
pool_func_t SES_pool_accept_task;


/* cache_shmlog.c */
extern struct VSC_C_main *VSC_C_main;
void VSM_Init(void);
void *VSM_Alloc(unsigned size, const char *class, const char *type,
    const char *ident);
void VSL_Setup(struct vsl_log *vsl, void *ptr, size_t len);
void VSL_ChgId(struct vsl_log *vsl, const char *typ, const char *why,
    uint32_t vxid);
void VSL_End(struct vsl_log *vsl);
void VSM_Free(void *ptr);
#ifdef VSL_ENDMARKER
void VSL(enum VSL_tag_e tag, uint32_t vxid, const char *fmt, ...)
    __printflike(3, 4);
void VSLbv(struct vsl_log *, enum VSL_tag_e tag, const char *fmt, va_list va);
void VSLb(struct vsl_log *, enum VSL_tag_e tag, const char *fmt, ...)
    __printflike(3, 4);
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
void VCL_Init(void);
void VCL_Refresh(struct VCL_conf **vcc);
void VCL_Ref(struct VCL_conf *vcc);
void VCL_Rel(struct VCL_conf **vcc);
void VCL_Poll(void);
const char *VCL_Return_Name(unsigned);
const char *VCL_Method_Name(unsigned);

#define VCL_MET_MAC(l,u,b) \
    void VCL_##l##_method(struct VCL_conf *, struct worker *, struct req *, \
	struct busyobj *bo, struct ws *);
#include "tbl/vcl_returns.h"
#undef VCL_MET_MAC

/* cache_vrt.c */

/*
 * These prototypes go here, because we do not want to pollute vrt.h
 * with va_list.  VCC never generates direct calls to them.
 */
const char *VRT_String(struct ws *ws, const char *h, const char *p, va_list ap);
char *VRT_StringList(char *d, unsigned dl, const char *p, va_list ap);

void ESI_Deliver(struct req *);
void ESI_DeliverChild(struct req *);

/* cache_vrt_vmod.c */
void VMOD_Init(void);

/* cache_waiter.c */
void WAIT_Enter(struct sess *sp);
void WAIT_Init(void);
const char *WAIT_GetName(void);
void WAIT_Write_Session(struct sess *sp, int fd);

/* cache_wrk.c */

void WRK_Thread(struct pool *qp, size_t stacksize, unsigned thread_workspace);
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
char *WS_Alloc(struct ws *ws, unsigned bytes);
void *WS_Copy(struct ws *ws, const void *str, int len);
char *WS_Snapshot(struct ws *ws);
int WS_Overflowed(const struct ws *ws);
void *WS_Printf(struct ws *ws, const char *fmt, ...) __printflike(2, 3);

/* cache_rfc2616.c */
void RFC2616_Ttl(struct busyobj *, double now);
unsigned RFC2616_Req_Gzip(const struct http *);
int RFC2616_Do_Cond(const struct req *sp);
void RFC2616_Weaken_Etag(struct http *hp);
void RFC2616_Vary_AE(struct http *hp);

/* stevedore.c */
int STV_NewObject(struct objcore *, struct worker *,
    const char *hint, unsigned len);
struct storage *STV_alloc(struct stevedore *, size_t size);
void STV_trim(struct storage *st, size_t size, int move_ok);
void STV_free(struct storage *st);
void STV_open(void);
void STV_close(void);
int STV_BanInfo(enum baninfo event, const uint8_t *ban, unsigned len);
void STV_BanExport(const uint8_t *bans, unsigned len);

/* storage_persistent.c */
void SMP_Init(void);
void SMP_Ready(void);

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

static inline void
Tadd(txt *t, const char *p, int l)
{
	Tcheck(*t);

	if (l <= 0) {
	} if (t->b + l < t->e) {
		memcpy(t->b, p, l);
		t->b += l;
	} else {
		t->b = t->e;
	}
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
