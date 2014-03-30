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

/*--------------------------------------------------------------------*/

/*
 * NB: HDR_STATUS is only used in cache_http.c, everybody else uses the
 * http->status integer field.
 */

enum {
	/* Fields from the first line of HTTP proto */
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
struct http_conn;
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
struct objiter;

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
 * Ban info event types
 */

/* NB: remember to update http_Copy() if you add fields */
struct http {
	unsigned		magic;
#define HTTP_MAGIC		0x6428b5c9

	enum VSL_tag_e		logtag;		/* Must be SLT_*Method */
	struct vsl_log		*vsl;

	struct ws		*ws;
	txt			*hd;
	unsigned char		*hdf;
#define HDF_FILTER		(1 << 0)	/* Filtered by Connection */
#define HDF_MARKER		(1 << 1)	/* Marker bit */
	uint16_t		shd;		/* Size of hd space */
	uint16_t		nhd;		/* Next free hd */
	uint16_t		status;
	uint8_t			protover;
	uint8_t			conds;		/* If-* headers present */
};

/*--------------------------------------------------------------------
 * HTTP Protocol connection structure
 *
 * This is the protocol independent object for a HTTP connection, used
 * both for backend and client sides.
 *
 */

typedef ssize_t htc_read(struct http_conn *, void *, size_t);

struct http_conn {
	unsigned		magic;
#define HTTP_CONN_MAGIC		0x3e19edd1
	htc_read		*read;

	int			fd;
	struct vsl_log		*vsl;
	unsigned		maxbytes;
	unsigned		maxhdr;
	struct ws		*ws;
	txt			rxbuf;
	txt			pipeline;
	enum body_status	body_status;
};

/*--------------------------------------------------------------------*/

struct acct_req {
#define ACCT(foo)	uint64_t	foo;
#include "tbl/acct_fields_req.h"
#undef ACCT
};

/*--------------------------------------------------------------------*/

struct acct_bereq {
#define ACCT(foo)	ssize_t		foo;
#include "tbl/acct_fields_bereq.h"
#undef ACCT
};

/*--------------------------------------------------------------------*/

#define L0(t, n)
#define L1(t, n)		t n;
#define VSC_F(n,t,l,f,v,e,d)	L##l(t, n)
struct dstat {
#include "tbl/vsc_f_main.h"
};
#undef VSC_F
#undef L0
#undef L1

/* Fetch processors --------------------------------------------------*/

enum vfp_status {
	VFP_ERROR = -1,
	VFP_OK = 0,
	VFP_END = 1,
};
typedef enum vfp_status
    vfp_pull_f(struct busyobj *bo, void *p, ssize_t *len, intptr_t *priv);

extern vfp_pull_f vfp_gunzip_pull;
extern vfp_pull_f vfp_gzip_pull;
extern vfp_pull_f vfp_testgunzip_pull;
extern vfp_pull_f vfp_esi_pull;
extern vfp_pull_f vfp_esi_gzip_pull;

/* Deliver processors ------------------------------------------------*/

enum vdp_action {
	VDP_NULL,
	VDP_FLUSH,
	VDP_FINISH,
};
typedef int vdp_bytes(struct req *, enum vdp_action, const void *ptr,
    ssize_t len);

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
	struct dstat		stats;

	struct pool_task	task;

	double			lastused;

	struct wrw		*wrw;

	pthread_cond_t		cond;

	struct VCL_conf		*vcl;

	struct ws		aws[1];

	struct vxid_pool	vxid_pool;

	unsigned		cur_method;
	unsigned		handling;
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

/* Storage -----------------------------------------------------------*/

struct storage {
	unsigned		magic;
#define STORAGE_MAGIC		0x1a4e51c0


	VTAILQ_ENTRY(storage)	list;
	struct stevedore	*stevedore;
	void			*priv;

	unsigned char		*ptr;
	unsigned		len;
	unsigned		space;
};

/* Object core structure ---------------------------------------------
 * Objects have sideways references in the binary heap and the LRU list
 * and we want to avoid paging in a lot of objects just to move them up
 * or down the binheap or to move a unrelated object on the LRU list.
 * To avoid this we use a proxy object, objcore, to hold the relevant
 * housekeeping fields parts of an object.
 */

typedef struct object *getobj_f(struct dstat *ds, struct objcore *oc);
typedef unsigned getxid_f(struct dstat *ds, struct objcore *oc);
typedef void updatemeta_f(struct objcore *oc);
typedef void freeobj_f(struct objcore *oc);
typedef struct lru *getlru_f(const struct objcore *oc);

struct objcore_methods {
	getobj_f	*getobj;
	getxid_f	*getxid;
	updatemeta_f	*updatemeta;
	freeobj_f	*freeobj;
	getlru_f	*getlru;
};

struct objcore {
	unsigned		magic;
#define OBJCORE_MAGIC		0x4d301302
	int			refcnt;
	struct objcore_methods	*methods;
	void			*priv;
	uintptr_t		priv2;
	struct objhead		*objhead;
	struct busyobj		*busyobj;
	double			timer_when;

	uint16_t		flags;
#define OC_F_BUSY		(1<<1)
#define OC_F_PASS		(1<<2)
#define OC_F_PRIV		(1<<5)		/* Stevedore private flag */
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

static inline unsigned
oc_getxid(struct dstat *ds, struct objcore *oc)
{
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	AN(oc->methods);
	AN(oc->methods->getxid);
	return (oc->methods->getxid(ds, oc));
}

static inline struct object *
oc_getobj(struct dstat *ds, struct objcore *oc)
{

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	// AZ(oc->flags & OC_F_BUSY);
	AN(oc->methods);
	AN(oc->methods->getobj);
	return (oc->methods->getobj(ds, oc));
}

static inline void
oc_updatemeta(struct objcore *oc)
{

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AN(oc->methods);
	if (oc->methods->updatemeta != NULL)
		oc->methods->updatemeta(oc);
}

static inline void
oc_freeobj(struct objcore *oc)
{

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AN(oc->methods);
	AN(oc->methods->freeobj);
	oc->methods->freeobj(oc);
}

static inline struct lru *
oc_getlru(const struct objcore *oc)
{

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AN(oc->methods);
	AN(oc->methods->getlru);
	return (oc->methods->getlru(oc));
}

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
	double			t_fetch;
	struct req		*req;

	uint8_t			*vary;

#define N_VFPS			5
	vfp_pull_f		*vfps[N_VFPS];
	intptr_t		vfps_priv[N_VFPS];
	int			vfp_nxt;

	int			failed;
	enum busyobj_state_e	state;

	struct ws		ws[1];
	struct vbc		*vbc;
	struct http		*bereq0;
	struct http		*bereq;
	struct http		*beresp;
	struct object		*ims_obj;
	struct objcore		*fetch_objcore;
	struct object		*fetch_obj;

	struct exp		exp;
	struct http_conn	htc;

	struct pool_task	fetch_task;

	char			*h_content_length;

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
	struct director		*director;
	struct VCL_conf		*vcl;

	struct vsl_log		vsl[1];
	struct dstat		*stats;

	/* Workspace for object only needed during fetch */
	struct ws		ws_o[1];

	struct vsb		*synth_body;
};

/* Object structure --------------------------------------------------*/

VTAILQ_HEAD(storagehead, storage);

struct object {
	unsigned		magic;
#define OBJECT_MAGIC		0x32851d42
	uint32_t		vxid;
	struct storage		*objstore;
	struct objcore		*objcore;

	uint8_t			*vary;

	unsigned		gziped:1;
	unsigned		changed_gzip:1;

	/* Bit positions in the gzip stream */
	ssize_t			gzip_start;
	ssize_t			gzip_last;
	ssize_t			gzip_stop;

	ssize_t			len;

	struct exp		exp;

	/* VCL only variables */
	double			last_modified;

	struct http		*http;

	struct storagehead	store;

	struct storage		*esidata;

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
	struct storagehead	body;

	struct {
		ssize_t			bytes_done;
		ssize_t			bytes_yet;
	}				h1;	/* HTTP1 specific */

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

	struct director		*director_hint;
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
	struct object		*obj;
	struct objcore		*objcore;
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

	/* Synth content in vcl_error */
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

/* cache_backend.c */
void VBE_UseHealth(const struct director *vdi);
void VBE_DiscardHealth(const struct director *vdi);


struct vbc *VDI_GetFd(struct busyobj *);
int VDI_Healthy(const struct director *);
void VDI_CloseFd(struct vbc **vbp, const struct acct_bereq *);
void VDI_RecycleFd(struct vbc **vbp, const struct acct_bereq *);
void VDI_AddHostHeader(struct http *to, const struct vbc *vbc);
void VBE_Poll(void);
void VDI_Init(void);

/* cache_backend_cfg.c */
void VBE_InitCfg(void);
struct backend *VBE_AddBackend(struct cli *cli, const struct vrt_backend *vb);

/* cache_backend_poll.c */
void VBP_Init(void);

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
int BAN_CheckObject(struct object *o, struct req *sp);
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
ssize_t VBO_waitlen(struct busyobj *bo, ssize_t l);
void VBO_setstate(struct busyobj *bo, enum busyobj_state_e next);
void VBO_waitstate(struct busyobj *bo, enum busyobj_state_e want);


/* cache_http1_fetch.c [V1F] */
int V1F_fetch_hdr(struct worker *wrk, struct busyobj *bo, struct req *req);
ssize_t V1F_Setup_Fetch(struct busyobj *bo);

/* cache_http1_fsm.c [HTTP1] */
typedef int (req_body_iter_f)(struct req *, void *priv, void *ptr, size_t);
void HTTP1_Session(struct worker *, struct req *);
int HTTP1_DiscardReqBody(struct req *req);
int HTTP1_CacheReqBody(struct req *req, ssize_t maxsize);
int HTTP1_IterateReqBody(struct req *req, req_body_iter_f *func, void *priv);

/* cache_http1_deliver.c */
unsigned V1D_FlushReleaseAcct(struct req *req);
void V1D_Deliver(struct req *);
void V1D_Deliver_Synth(struct req *req);


static inline int
VDP_bytes(struct req *req, enum vdp_action act, const void *ptr, ssize_t len)
{
	int i, retval;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

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

/* cache_expiry.c */
void EXP_Clr(struct exp *e);

double EXP_Ttl(const struct req *, const struct object*);
void EXP_Insert(struct objcore *oc);
void EXP_Inject(struct objcore *oc, struct lru *lru, double when);
void EXP_Init(void);
void EXP_Rearm(struct object *o, double now, double ttl, double grace,
    double keep);
void EXP_Touch(struct objcore *oc, double now);
int EXP_NukeOne(struct busyobj *, struct lru *lru);
void EXP_NukeLRU(struct worker *wrk, struct vsl_log *vsl, struct lru *lru);

/* cache_fetch.c */
enum vbf_fetch_mode_e {
	VBF_NORMAL = 0,
	VBF_PASS = 1,
	VBF_BACKGROUND = 2,
};
void VBF_Fetch(struct worker *wrk, struct req *req,
    struct objcore *oc, struct object *oldobj, enum vbf_fetch_mode_e);

/* cache_fetch_proc.c */
struct storage *VFP_GetStorage(struct busyobj *, ssize_t sz);
enum vfp_status VFP_Error(struct busyobj *, const char *fmt, ...)
    __printflike(2, 3);
void VFP_Init(void);
void VFP_Fetch_Body(struct busyobj *bo, ssize_t est);
void VFP_Push(struct busyobj *, vfp_pull_f *func, intptr_t priv);
enum vfp_status VFP_Suck(struct busyobj *, void *p, ssize_t *lp);
extern char vfp_init[];
extern char vfp_fini[];

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
enum vgzret_e VGZ_Gzip(struct vgz *, const void **, size_t *len, enum vgz_flag);
enum vgzret_e VGZ_Gunzip(struct vgz *, const void **, size_t *len);
enum vgzret_e VGZ_Destroy(struct vgz **);
void VGZ_UpdateObj(const struct vgz*, struct object *);
vdp_bytes VDP_gunzip;

int VGZ_WrwInit(struct vgz *vg);
enum vgzret_e VGZ_WrwGunzip(struct req *, struct vgz *, const void *ibuf,
    ssize_t ibufl);
void VGZ_WrwFlush(struct req *, struct vgz *vg);

/* cache_http.c */
unsigned HTTP_estimate(unsigned nhttp);
void HTTP_Copy(struct http *to, const struct http * const fm);
struct http *HTTP_create(void *p, uint16_t nhttp);
const char *http_StatusMessage(unsigned);
unsigned http_EstimateWS(const struct http *fm, unsigned how, uint16_t *nhd);
void HTTP_Init(void);
void http_ClrHeader(struct http *to);
void http_SetResp(struct http *to, const char *proto, uint16_t status,
    const char *response);
void http_FilterReq(struct http *to, const struct http *fm, unsigned how);
void http_FilterResp(const struct http *fm, struct http *to, unsigned how);
void http_PutProtocol(const struct http *to, const char *protocol);
void http_PutStatus(struct http *to, uint16_t status);
void http_ForceHeader(struct http *to, const char *hdr, const char *val);
void http_PutResponse(const struct http *to, const char *response);
void http_PrintfHeader(struct http *to, const char *fmt, ...)
    __printflike(2, 3);
void http_SetHeader(struct http *to, const char *hdr);
void http_SetH(const struct http *to, unsigned n, const char *fm);
void http_ForceGet(const struct http *to);
void HTTP_Setup(struct http *, struct ws *, struct vsl_log *, enum VSL_tag_e);
void http_Teardown(struct http *ht);
int http_GetHdr(const struct http *hp, const char *hdr, char **ptr);
int http_GetHdrData(const struct http *hp, const char *hdr,
    const char *field, char **ptr);
int http_GetHdrField(const struct http *hp, const char *hdr,
    const char *field, char **ptr);
double http_GetHdrQ(const struct http *hp, const char *hdr, const char *field);
uint16_t http_GetStatus(const struct http *hp);
const char *http_GetReq(const struct http *hp);
int http_HdrIs(const struct http *hp, const char *hdr, const char *val);
int http_IsHdr(const txt *hh, const char *hdr);
enum sess_close http_DoConnection(const struct http *);
void http_CopyHome(const struct http *hp);
void http_Unset(struct http *hp, const char *hdr);
void http_CollectHdr(struct http *hp, const char *hdr);
void http_VSL_log(const struct http *hp);
void http_Merge(const struct http *fm, struct http *to, int not_ce);

/* cache_http1_proto.c */

enum htc_status_e {
	HTTP1_ALL_WHITESPACE =	-3,
	HTTP1_OVERFLOW =	-2,
	HTTP1_ERROR_EOF =	-1,
	HTTP1_NEED_MORE =	 0,
	HTTP1_COMPLETE =	 1
};

void HTTP1_Init(struct http_conn *htc, struct ws *ws, int fd, struct vsl_log *,
    unsigned maxbytes, unsigned maxhdr);
enum htc_status_e HTTP1_Reinit(struct http_conn *htc);
enum htc_status_e HTTP1_Rx(struct http_conn *htc);
ssize_t HTTP1_Read(struct http_conn *htc, void *d, size_t len);
enum htc_status_e HTTP1_Complete(struct http_conn *htc);
uint16_t HTTP1_DissectRequest(struct req *);
uint16_t HTTP1_DissectResponse(struct http *sp, const struct http_conn *htc);
unsigned HTTP1_Write(const struct worker *w, const struct http *hp, int resp);

#define HTTPH(a, b, c) extern char b[];
#include "tbl/http_headers.h"
#undef HTTPH

/* cache_main.c */
uint32_t VXID_Get(struct vxid_pool *v);
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
struct objiter *ObjIterBegin(struct worker *, struct object *);
enum objiter_status ObjIter(struct objiter *, void **, ssize_t *);
void ObjIterEnd(struct objiter **);

/* cache_panic.c */
void PAN_Init(void);
const char *body_status_2str(enum body_status e);
const char *reqbody_status_2str(enum req_body_state_e e);
const char *sess_close_2str(enum sess_close sc, int want_desc);

/* cache_pipe.c */
void Pipe_Init(void);
void PipeRequest(struct req *req, struct busyobj *bo);

/* cache_pool.c */
void Pool_Init(void);
void Pool_Accept(void);
void Pool_Work_Thread(void *priv, struct worker *w);
int Pool_Task(struct pool *pp, struct pool_task *task, enum pool_how how);

#define WRW_IsReleased(w)	((w)->wrw == NULL)
int WRW_Error(const struct worker *w);
void WRW_Chunked(const struct worker *w);
void WRW_EndChunk(const struct worker *w);
void WRW_Reserve(struct worker *w, int *fd, struct vsl_log *, double t0);
unsigned WRW_Flush(const struct worker *w);
unsigned WRW_FlushRelease(struct worker *w, ssize_t *pacc);
unsigned WRW_Write(const struct worker *w, const void *ptr, int len);
unsigned WRW_WriteH(const struct worker *w, const txt *hh, const char *suf);

/* cache_session.c [SES] */
void SES_Close(struct sess *sp, enum sess_close reason);
void SES_Delete(struct sess *sp, enum sess_close reason, double now);
struct sesspool *SES_NewPool(struct pool *pp, unsigned pool_no);
void SES_DeletePool(struct sesspool *sp);
int SES_ScheduleReq(struct req *);
struct req *SES_GetReq(struct worker *, struct sess *);
void SES_Handle(struct sess *sp, double now);
void SES_ReleaseReq(struct req *);
pool_func_t SES_pool_accept_task;


/* cache_shmlog.c */
extern struct VSC_C_main *VSC_C_main;
void VSM_Init(void);
void *VSM_Alloc(unsigned size, const char *class, const char *type,
    const char *ident);
void VSL_Setup(struct vsl_log *vsl, void *ptr, size_t len);
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
unsigned VRY_Validate(const uint8_t *vary);
void VRY_Prep(struct req *);
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

void WRK_Init(void);
int WRK_TrySumStat(struct worker *w);
void WRK_SumStat(struct worker *w);
void *WRK_thread(void *priv);
typedef void *bgthread_t(struct worker *, void *priv);
void WRK_BgThread(pthread_t *thr, const char *name, bgthread_t *func,
    void *priv);

/* cache_ws.c */

void WS_Init(struct ws *ws, const char *id, void *space, unsigned len);
unsigned WS_Reserve(struct ws *ws, unsigned bytes);
void WS_Release(struct ws *ws, unsigned bytes);
void WS_ReleaseP(struct ws *ws, char *ptr);
void WS_Assert(const struct ws *ws);
void WS_Reset(struct ws *ws, char *p);
char *WS_Alloc(struct ws *ws, unsigned bytes);
void *WS_Copy(struct ws *ws, const void *str, int len);
char *WS_Snapshot(struct ws *ws);
int WS_Overflowed(const struct ws *ws);
void *WS_Printf(struct ws *ws, const char *fmt, ...) __printflike(2, 3);

/* rfc2616.c */
void RFC2616_Ttl(struct busyobj *);
enum body_status RFC2616_Body(struct busyobj *, struct dstat *);
unsigned RFC2616_Req_Gzip(const struct http *);
int RFC2616_Do_Cond(const struct req *sp);
void RFC2616_Weaken_Etag(struct http *hp);


/* stevedore.c */
struct object *STV_NewObject(struct busyobj *,
    const char *hint, unsigned len, uint16_t nhttp);
struct storage *STV_alloc(struct busyobj *, size_t size);
void STV_trim(struct storage *st, size_t size, int move_ok);
void STV_free(struct storage *st);
void STV_open(void);
void STV_close(void);
void STV_Freestore(struct object *o);
int STV_BanInfo(enum baninfo event, const uint8_t *ban, unsigned len);
void STV_BanExport(const uint8_t *bans, unsigned len);
struct storage *STV_alloc_transient(size_t size);

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
