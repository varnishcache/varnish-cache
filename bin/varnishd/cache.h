/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Redpill Linpro AS
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
 * $Id$
 */

/*
 * This macro can be used in .h files to isolate bits that the manager
 * should not (need to) see, such as pthread mutexes etc.
 */
#define VARNISH_CACHE_CHILD	1

#include <sys/time.h>
#include <sys/uio.h>
#include <sys/socket.h>

#include <pthread.h>
#ifdef HAVE_PTHREAD_NP_H
#include <pthread_np.h>
#endif
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>

#if defined(HAVE_EPOLL_CTL)
#include <sys/epoll.h>
#endif

#include "vqueue.h"

#include "vsb.h"

#include "libvarnish.h"

#include "common.h"
#include "heritage.h"
#include "miniobj.h"

#include "vsc.h"
#include "vsl.h"
#include "vtypes.h"

/*
 * NB: HDR_STATUS is only used in cache_http.c, everybody else uses the
 * http->status integer field.
 */

enum {
	/* Fields from the first line of HTTP proto */
	HTTP_HDR_REQ,
	HTTP_HDR_URL,
	HTTP_HDR_PROTO,
	HTTP_HDR_STATUS,
	HTTP_HDR_RESPONSE,
	/* HTTP header lines */
	HTTP_HDR_FIRST,
};

struct cli;
struct vsb;
struct sess;
struct director;
struct object;
struct objhead;
struct objcore;
struct storage;
struct workreq;
struct vrt_backend;
struct cli_proto;
struct ban;
struct SHA256Context;
struct vsc_lck;
struct waitinglist;

struct lock { void *priv; };		// Opaque

#define DIGEST_LEN		32


/*--------------------------------------------------------------------
 * Pointer aligment magic
 */

#define PALGN		(sizeof(void *) - 1)
#define PAOK(p)		(((uintptr_t)(p) & PALGN) == 0)
#define PRNDDN(p)	((uintptr_t)(p) & ~PALGN)
#define PRNDUP(p)	(((uintptr_t)(p) + PALGN) & ~PALGN)

/*--------------------------------------------------------------------*/

typedef struct {
	char			*b;
	char			*e;
} txt;

/*--------------------------------------------------------------------*/

enum step {
#define STEP(l, u)	STP_##u,
#include "steps.h"
#undef STEP
};

/*--------------------------------------------------------------------
 * Workspace structure for quick memory allocation.
 */

struct ws {
	unsigned		magic;
#define WS_MAGIC		0x35fac554
	const char		*id;		/* identity */
	char			*s;		/* (S)tart of buffer */
	char			*f;		/* (F)ree pointer */
	char			*r;		/* (R)eserved length */
	char			*e;		/* (E)nd of buffer */
	int			overflow;	/* workspace overflowed */
};

/*--------------------------------------------------------------------
 * HTTP Request/Response/Header handling structure.
 */

enum httpwhence {
	HTTP_Rx	 = 1,
	HTTP_Tx  = 2,
	HTTP_Obj = 3
};

/* NB: remember to update http_Copy() if you add fields */
struct http {
	unsigned		magic;
#define HTTP_MAGIC		0x6428b5c9

	struct ws		*ws;

	unsigned char		conds;		/* If-* headers present */
	enum httpwhence		logtag;
	int			status;
	double			protover;

	unsigned		shd;		/* Size of hd space */
	txt			*hd;
	unsigned char		*hdf;
#define HDF_FILTER		(1 << 0)	/* Filtered by Connection */
#define HDF_COPY		(1 << 1)	/* Copy this field */
	unsigned		nhd;		/* Next free hd */
};

/*--------------------------------------------------------------------
 * HTTP Protocol connection structure
 */

struct http_conn {
	unsigned		magic;
#define HTTP_CONN_MAGIC		0x3e19edd1

	int			fd;
	struct ws		*ws;
	txt			rxbuf;
	txt			pipeline;
};

/*--------------------------------------------------------------------*/

struct acct {
	double			first;
#define ACCT(foo)	uint64_t	foo;
#include "acct_fields.h"
#undef ACCT
};

/*--------------------------------------------------------------------*/

#define L0(n)
#define L1(n)			int n;
#define VSC_F(n, t, l, f, e)	L##l(n)
#define VSC_DO_MAIN
struct dstat {
#include "vsc_fields.h"
};
#undef VSC_F
#undef VSC_DO_MAIN
#undef L0
#undef L1

/* Fetch processors --------------------------------------------------*/

typedef void vfp_begin_f(struct sess *, size_t );
typedef int vfp_bytes_f(struct sess *, struct http_conn *, size_t);
typedef int vfp_end_f(struct sess *sp);

struct vfp {
	vfp_begin_f	*begin;
	vfp_bytes_f	*bytes;
	vfp_end_f	*end;
};

extern struct vfp vfp_gunzip;
extern struct vfp vfp_gzip;
extern struct vfp vfp_esi;

/*--------------------------------------------------------------------*/

struct worker {
	unsigned		magic;
#define WORKER_MAGIC		0x6391adcf
	struct objhead		*nobjhead;
	struct objcore		*nobjcore;
	struct waitinglist	*nwaitinglist;
	void			*nhashpriv;
	struct dstat		stats;

	double			lastused;

	pthread_cond_t		cond;

	VTAILQ_ENTRY(worker)	list;
	struct workreq		*wrq;

	int			*wfd;
	unsigned		werr;	/* valid after WRK_Flush() */
	struct iovec		*iov;
	unsigned		siov;
	unsigned		niov;
	ssize_t			liov;

	struct VCL_conf		*vcl;

	uint32_t		*wlb, *wlp, *wle;
	unsigned		wlr;

	struct SHA256Context	*sha256ctx;

	struct http_conn	htc[1];
	struct ws		ws[1];
	struct http		*http[3];
	struct http		*bereq;
	struct http		*beresp1;
	struct http		*beresp;
	struct http		*resp;

	unsigned		cacheable;
	double			age;
	double			entered;
	double			ttl;
	double			grace;

	/* This is only here so VRT can find it */
	char			*storage_hint;

	/* Fetch stuff */
	enum body_status	body_status;
	struct storage		*storage;
	struct vfp		*vfp;
	void			*vfp_private;
	unsigned		do_esi;
	unsigned		do_gzip;
	unsigned		is_gzip;
	unsigned		do_gunzip;
	unsigned		is_gunzip;

	/* ESI stuff */
	struct vep_state	*vep;

	/* Timeouts */
	double			connect_timeout;
	double			first_byte_timeout;
	double			between_bytes_timeout;

	/* Delivery mode */
	unsigned		res_mode;
#define RES_LEN			(1<<1)
#define RES_EOF			(1<<2)
#define RES_CHUNKED		(1<<3)
#define RES_ESI			(1<<4)
#define RES_ESI_CHILD		(1<<5)
#define RES_GUNZIP		(1<<6)

};

/* Work Request for worker thread ------------------------------------*/

/*
 * This is a worker-function.
 * XXX: typesafety is probably not worth fighting for
 */

typedef void workfunc(struct worker *, void *priv);

struct workreq {
	VTAILQ_ENTRY(workreq)	list;
	workfunc		*func;
	void			*priv;
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

	int			fd;
	off_t			where;
};

/* Object core structure ---------------------------------------------
 * Objects have sideways references in the binary heap and the LRU list
 * and we want to avoid paging in a lot of objects just to move them up
 * or down the binheap or to move a unrelated object on the LRU list.
 * To avoid this we use a proxy object, objcore, to hold the relevant
 * housekeeping fields parts of an object.
 */

typedef struct object *getobj_f(struct worker *wrk, struct objcore *oc);
typedef void updatemeta_f(struct objcore *oc);
typedef void freeobj_f(struct objcore *oc);

struct objcore_methods {
	getobj_f	*getobj;
	updatemeta_f	*updatemeta;
	freeobj_f	*freeobj;
};

extern struct objcore_methods default_oc_methods;

struct objcore {
	unsigned		magic;
#define OBJCORE_MAGIC		0x4d301302
	unsigned		refcnt;
	struct objcore_methods	*methods;
	void			*priv;
	unsigned		priv2;
	struct objhead		*objhead;
	double			timer_when;
	unsigned		flags;
#define OC_F_ONLRU		(1<<0)
#define OC_F_BUSY		(1<<1)
#define OC_F_PASS		(1<<2)
#define OC_F_LRUDONTMOVE	(1<<4)
#define OC_F_PRIV		(1<<5)		/* Stevedore private flag */
	unsigned		timer_idx;
	VTAILQ_ENTRY(objcore)	list;
	VLIST_ENTRY(objcore)	lru_list;
	VTAILQ_ENTRY(objcore)	ban_list;
	struct ban		*ban;
};

static inline struct object *
oc_getobj(struct worker *wrk, struct objcore *oc)
{

	return (oc->methods->getobj(wrk, oc));
}

static inline void
oc_updatemeta(struct objcore *oc)
{

	if (oc->methods->updatemeta != NULL)
		oc->methods->updatemeta(oc);
}

static inline void
oc_freeobj(struct objcore *oc)
{

	oc->methods->freeobj(oc);
}

/*--------------------------------------------------------------------*/

struct lru {
	unsigned		magic;
#define LRU_MAGIC		0x3fec7bb0
	VLIST_HEAD(,objcore)	lru_head;
	struct objcore		senteniel;
};

/* Object structure --------------------------------------------------*/

struct object {
	unsigned		magic;
#define OBJECT_MAGIC		0x32851d42
	unsigned		xid;
	struct storage		*objstore;
	struct objcore		*objcore;

	struct ws		ws_o[1];
	unsigned char		*vary;

	double			ban_t;
	unsigned		response;

	unsigned		cacheable;

	unsigned		len;

	double			ttl;
	double			age;
	double			entered;
	double			grace;

	double			last_modified;
	double			last_lru;

	struct http		*http;

	VTAILQ_HEAD(, storage)	store;

	struct storage		*esidata;

	double			last_use;

	int			hits;
};

/* -------------------------------------------------------------------*/

struct sess {
	unsigned		magic;
#define SESS_MAGIC		0x2c2f9c5a
	int			fd;
	int			id;
	unsigned		xid;

	int			restarts;
	int			esi_level;
	int			disable_esi;

	uint8_t			hash_ignore_busy;
	uint8_t			hash_always_miss;

	struct worker		*wrk;

	socklen_t		sockaddrlen;
	socklen_t		mysockaddrlen;
	struct sockaddr_storage	*sockaddr;
	struct sockaddr_storage	*mysockaddr;
	struct listen_sock	*mylsock;

	/* formatted ascii client address */
	char			*addr;
	char			*port;
	char			*client_identity;

	/* HTTP request */
	const char		*doclose;
	struct http		*http;
	struct http		*http0;

	struct ws		ws[1];
	char			*ws_ses;	/* WS above session data */
	char			*ws_req;	/* WS above request data */

	unsigned char		digest[DIGEST_LEN];

	struct http_conn	htc[1];

	/* Timestamps, all on TIM_real() timescale */
	double			t_open;
	double			t_req;
	double			t_resp;
	double			t_end;

	/* Acceptable grace period */
	double			grace;

	enum step		step;
	unsigned		cur_method;
	unsigned		handling;
	unsigned char		pass;
	unsigned char		sendbody;
	unsigned char		wantbody;
	int			err_code;
	const char		*err_reason;

	VTAILQ_ENTRY(sess)	list;

	struct director		*director;
	struct vbc		*vbc;
	struct object		*obj;
	struct objcore		*objcore;
	struct VCL_conf		*vcl;

	/* The busy objhead we sleep on */
	struct objhead		*hash_objhead;

	/* Various internal stuff */
	struct sessmem		*mem;

	struct workreq		workreq;
	struct acct		acct_tmp;
	struct acct		acct_req;
	struct acct		acct_ses;

#if defined(HAVE_EPOLL_CTL)
	struct epoll_event ev;
#endif
};

/* -------------------------------------------------------------------*/

/* Backend connection */
struct vbc {
	unsigned		magic;
#define VBC_MAGIC		0x0c5e6592
	VTAILQ_ENTRY(vbc)	list;
	struct backend		*backend;
	struct vdi_simple	*vdis;
	int			fd;

	struct sockaddr_storage	*addr;
	socklen_t		addrlen;

	uint8_t			recycled;

	/* Timeouts */
	double			first_byte_timeout;
	double			between_bytes_timeout;
};

/* Prototypes etc ----------------------------------------------------*/

/* cache_acceptor.c */
void vca_return_session(struct sess *sp);
void vca_close_session(struct sess *sp, const char *why);
void VCA_Prep(struct sess *sp);
void VCA_Init(void);
void VCA_Shutdown(void);
const char *VCA_waiter_name(void);
extern pthread_t VCA_thread;

/* cache_backend.c */
void VBE_UseHealth(const struct director *vdi);

struct vbc *VDI_GetFd(const struct director *, struct sess *sp);
int VDI_Healthy(const struct director *, const struct sess *sp);
void VDI_CloseFd(struct sess *sp);
void VDI_RecycleFd(struct sess *sp);
void VDI_AddHostHeader(const struct sess *sp);
void VBE_Poll(void);

/* cache_backend_cfg.c */
void VBE_Init(void);
struct backend *VBE_AddBackend(struct cli *cli, const struct vrt_backend *vb);

/* cache_backend_poll.c */
void VBP_Init(void);

/* cache_ban.c */
struct ban *BAN_New(void);
int BAN_AddTest(struct cli *, struct ban *, const char *, const char *,
    const char *);
void BAN_Free(struct ban *b);
void BAN_Insert(struct ban *b);
void BAN_Init(void);
void BAN_NewObj(struct object *o);
void BAN_DestroyObj(struct objcore *oc);
int BAN_CheckObject(struct object *o, const struct sess *sp);
void BAN_Reload(double t0, unsigned flags, const char *ban);
struct ban *BAN_TailRef(void);
void BAN_Compile(void);
struct ban *BAN_RefBan(struct objcore *oc, double t0, const struct ban *tail);
void BAN_Deref(struct ban **ban);

/* cache_center.c [CNT] */
void CNT_Session(struct sess *sp);
void CNT_Init(void);

/* cache_cli.c [CLI] */
void CLI_Init(void);
void CLI_Run(void);
void CLI_AddFuncs(struct cli_proto *p);
extern pthread_t cli_thread;
#define ASSERT_CLI() do {assert(pthread_self() == cli_thread);} while (0)

/* cache_expiry.c */
void EXP_Insert(struct object *o);
void EXP_Inject(struct objcore *oc, struct lru *lru, double when);
void EXP_Init(void);
void EXP_Rearm(const struct object *o);
void EXP_Touch(struct object *o, double tnow);
int EXP_NukeOne(const struct sess *sp, const struct lru *lru);

/* cache_fetch.c */
int FetchHdr(struct sess *sp);
int FetchBody(struct sess *sp);
int FetchReqBody(struct sess *sp);
void Fetch_Init(void);

/* cache_gzip.c */
struct vgz;

struct vgz *VGZ_NewUnzip(const struct sess *sp, struct ws *tmp,
    struct ws *buf_ws, void *buf, ssize_t bufl);
int VGZ_Feed(struct vgz *, const void *, size_t len);
int VGZ_Produce(struct vgz *, const void **, size_t *len);
void VGZ_Destroy(struct vgz **);

/* cache_http.c */
unsigned HTTP_estimate(unsigned nhttp);
void HTTP_Copy(struct http *to, const struct http * const fm);
struct http *HTTP_create(void *p, unsigned nhttp);
const char *http_StatusMessage(unsigned);
unsigned http_EstimateWS(const struct http *fm, unsigned how, unsigned *nhd);
void HTTP_Init(void);
void http_ClrHeader(struct http *to);
unsigned http_Write(struct worker *w, const struct http *hp, int resp);
void http_CopyResp(struct http *to, const struct http *fm);
void http_SetResp(struct http *to, const char *proto, int status,
    const char *response);
void http_FilterFields(struct worker *w, int fd, struct http *to,
    const struct http *fm, unsigned how);
void http_FilterHeader(const struct sess *sp, unsigned how);
void http_PutProtocol(struct worker *w, int fd, const struct http *to,
    const char *protocol);
void http_PutStatus(struct http *to, int status);
void http_PutResponse(struct worker *w, int fd, const struct http *to,
    const char *response);
void http_PrintfHeader(struct worker *w, int fd, struct http *to,
    const char *fmt, ...);
void http_SetHeader(struct worker *w, int fd, struct http *to, const char *hdr);
void http_SetH(const struct http *to, unsigned n, const char *fm);
void http_ForceGet(const struct http *to);
void http_Setup(struct http *ht, struct ws *ws);
int http_GetHdr(const struct http *hp, const char *hdr, char **ptr);
int http_GetHdrData(const struct http *hp, const char *hdr,
    const char *field, char **ptr);
int http_GetHdrField(const struct http *hp, const char *hdr,
    const char *field, char **ptr);
double http_GetHdrQ(const struct http *hp, const char *hdr, const char *field);
int http_GetStatus(const struct http *hp);
const char *http_GetReq(const struct http *hp);
int http_HdrIs(const struct http *hp, const char *hdr, const char *val);
int http_DissectRequest(struct sess *sp);
int http_DissectResponse(struct worker *w, const struct http_conn *htc,
    struct http *sp);
const char *http_DoConnection(const struct http *hp);
void http_CopyHome(struct worker *w, int fd, const struct http *hp);
void http_Unset(struct http *hp, const char *hdr);
void http_CollectHdr(struct http *hp, const char *hdr);

/* cache_httpconn.c */
void HTC_Init(struct http_conn *htc, struct ws *ws, int fd);
int HTC_Reinit(struct http_conn *htc);
int HTC_Rx(struct http_conn *htc);
ssize_t HTC_Read(struct http_conn *htc, void *d, size_t len);
int HTC_Complete(struct http_conn *htc);

#define HTTPH(a, b, c, d, e, f, g) extern char b[];
#include "http_headers.h"
#undef HTTPH

/* cache_main.c */
void THR_SetName(const char *name);
const char* THR_GetName(void);
void THR_SetSession(const struct sess *sp);
const struct sess * THR_GetSession(void);

/* cache_lck.c */

/* Internal functions, call only through macros below */
void Lck__Lock(struct lock *lck, const char *p, const char *f, int l);
void Lck__Unlock(struct lock *lck, const char *p, const char *f, int l);
int Lck__Trylock(struct lock *lck, const char *p, const char *f, int l);
void Lck__New(struct lock *lck, struct vsc_lck *, const char *);
void Lck__Assert(const struct lock *lck, int held);

/* public interface: */
void LCK_Init(void);
void Lck_Delete(struct lock *lck);
void Lck_CondWait(pthread_cond_t *cond, struct lock *lck);

#define Lck_New(a, b) Lck__New(a, b, #b)
#define Lck_Lock(a) Lck__Lock(a, __func__, __FILE__, __LINE__)
#define Lck_Unlock(a) Lck__Unlock(a, __func__, __FILE__, __LINE__)
#define Lck_Trylock(a) Lck__Trylock(a, __func__, __FILE__, __LINE__)
#define Lck_AssertHeld(a) Lck__Assert(a, 1)
#define Lck_AssertNotHeld(a) Lck__Assert(a, 0)

#define LOCK(nam) extern struct vsc_lck *lck_##nam;
#include "locks.h"
#undef LOCK

/* cache_panic.c */
void PAN_Init(void);

/* cache_pipe.c */
void PipeSession(struct sess *sp);

/* cache_pool.c */
void WRK_Init(void);
int WRK_Queue(struct workreq *wrq);
int WRK_QueueSession(struct sess *sp);
void WRK_SumStat(struct worker *w);

void WRW_Reserve(struct worker *w, int *fd);
unsigned WRW_Flush(struct worker *w);
unsigned WRW_FlushRelease(struct worker *w);
unsigned WRW_Write(struct worker *w, const void *ptr, int len);
unsigned WRW_WriteH(struct worker *w, const txt *hh, const char *suf);
#ifdef SENDFILE_WORKS
void WRW_Sendfile(struct worker *w, int fd, off_t off, unsigned len);
#endif  /* SENDFILE_WORKS */

typedef void *bgthread_t(struct sess *, void *priv);
void WRK_BgThread(pthread_t *thr, const char *name, bgthread_t *func,
    void *priv);

/* cache_session.c [SES] */
void SES_Init(void);
struct sess *SES_New(void);
struct sess *SES_Alloc(void);
void SES_Delete(struct sess *sp);
void SES_Charge(struct sess *sp);

/* cache_shmlog.c */
void VSL_Init(void);
#ifdef VSL_ENDMARKER
void VSL(enum vsl_tag tag, int id, const char *fmt, ...);
void WSLR(struct worker *w, enum vsl_tag tag, int id, txt t);
void WSL(struct worker *w, enum vsl_tag tag, int id, const char *fmt, ...);
void WSL_Flush(struct worker *w, int overflow);

#define DSL(flag, tag, id, ...)					\
	do {							\
		if (params->diag_bitmap & (flag))		\
			VSL((tag), (id), __VA_ARGS__);		\
	} while (0)

#define WSP(sess, tag, ...)					\
	WSL((sess)->wrk, tag, (sess)->fd, __VA_ARGS__)

#define WSPR(sess, tag, txt)					\
	WSLR((sess)->wrk, tag, (sess)->fd, txt)

#define INCOMPL() do {							\
	VSL(SLT_Debug, 0, "INCOMPLETE AT: %s(%d)", __func__, __LINE__); \
	fprintf(stderr,							\
	    "INCOMPLETE AT: %s(%d)\n",					\
	    (const char *)__func__, __LINE__);				\
	abort();							\
	} while (0)
#endif

/* cache_response.c */
void RES_BuildHttp(struct sess *sp);
void RES_WriteObj(struct sess *sp);

/* cache_vary.c */
struct vsb *VRY_Create(const struct sess *sp, const struct http *hp);
int VRY_Match(const struct sess *sp, const unsigned char *vary);

/* cache_vcl.c */
void VCL_Init(void);
void VCL_Refresh(struct VCL_conf **vcc);
void VCL_Rel(struct VCL_conf **vcc);
void VCL_Get(struct VCL_conf **vcc);
void VCL_Poll(void);

#define VCL_MET_MAC(l,u,b) void VCL_##l##_method(struct sess *);
#include "vcl_returns.h"
#undef VCL_MET_MAC

/* cache_vrt.c */

char *VRT_String(struct ws *ws, const char *h, const char *p, va_list ap);
char *VRT_StringList(char *d, unsigned dl, const char *p, va_list ap);

void ESI_Deliver(struct sess *);

/* cache_vrt_vmod.c */
void VMOD_Init(void);

/* cache_ws.c */

void WS_Init(struct ws *ws, const char *id, void *space, unsigned len);
unsigned WS_Reserve(struct ws *ws, unsigned bytes);
void WS_Release(struct ws *ws, unsigned bytes);
void WS_ReleaseP(struct ws *ws, char *ptr);
void WS_Assert(const struct ws *ws);
void WS_Reset(struct ws *ws, char *p);
char *WS_Alloc(struct ws *ws, unsigned bytes);
char *WS_Dup(struct ws *ws, const char *);
char *WS_Snapshot(struct ws *ws);
unsigned WS_Free(const struct ws *ws);

/* rfc2616.c */
double RFC2616_Ttl(const struct sess *sp);
enum body_status RFC2616_Body(const struct sess *sp);
unsigned RFC2616_Req_Gzip(const struct sess *sp);

/* storage_synth.c */
struct vsb *SMS_Makesynth(struct object *obj);
void SMS_Finish(struct object *obj);

/* storage_persistent.c */
void SMP_Ready(void);
void SMP_NewBan(double t0, const char *ban);

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

static inline void
AssertObjBusy(const struct object *o)
{
	AN(o->objcore);
	AN (o->objcore->flags & OC_F_BUSY);
}

static inline void
AssertObjPassOrBusy(const struct object *o)
{
	if (o->objcore != NULL)
		AN (o->objcore->flags & OC_F_BUSY);
}
