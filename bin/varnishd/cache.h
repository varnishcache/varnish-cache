/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>

#include "vqueue.h"

#include "vsb.h"

#include "libvarnish.h"

#include "vcl_returns.h"
#include "common.h"
#include "heritage.h"
#include "miniobj.h"

#define HTTP_HDR_MAX_VAL 32

enum {
	/* Fields from the first line of HTTP proto */
	HTTP_HDR_REQ,
	HTTP_HDR_URL,
	HTTP_HDR_PROTO,
	HTTP_HDR_STATUS,
	HTTP_HDR_RESPONSE,
	/* HTTP header lines */
	HTTP_HDR_FIRST,
	HTTP_HDR_MAX = HTTP_HDR_MAX_VAL
};

/* Note: intentionally not IOV_MAX unless it has to be */
#if (IOV_MAX < (HTTP_HDR_MAX_VAL * 2))
#  define MAX_IOVS	IOV_MAX
#else
#  define MAX_IOVS	(HTTP_HDR_MAX_VAL * 2)
#endif

struct cli;
struct vsb;
struct sess;
struct director;
struct object;
struct objhead;
struct objexp;
struct workreq;
struct addrinfo;
struct esi_bit;
struct vrt_backend;
struct cli_proto;
struct ban;
struct lock { void *priv; };		// Opaque

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

struct http {
	unsigned		magic;
#define HTTP_MAGIC		0x6428b5c9

	struct ws		*ws;

	unsigned char		conds;		/* If-* headers present */
	enum httpwhence		logtag;
	int			status;
	double			protover;

	txt			hd[HTTP_HDR_MAX];
	unsigned char		hdf[HTTP_HDR_MAX];
#define HDF_FILTER		(1 << 0)	/* Filtered by Connection */
	unsigned		nhd;
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

struct worker {
	unsigned		magic;
#define WORKER_MAGIC		0x6391adcf
	struct objhead		*nobjhead;
	struct object		*nobj;

	double			lastused;

	pthread_cond_t		cond;

	VTAILQ_ENTRY(worker)	list;
	struct workreq		*wrq;

	int			*wfd;
	unsigned		werr;	/* valid after WRK_Flush() */
	struct iovec		iov[MAX_IOVS];
	int			niov;
	ssize_t			liov;

	struct VCL_conf		*vcl;
	struct srcaddr		*srcaddr;
	struct acct		acct;

	unsigned char		*wlb, *wlp, *wle;
	unsigned		wlr;
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

/* Backend Request ---------------------------------------------------*/

struct bereq {
	unsigned		magic;
#define BEREQ_MAGIC		0x3b6d250c
	VTAILQ_ENTRY(bereq)	list;
	struct ws		ws[1];
	struct http		http[1];
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

/* Object structure --------------------------------------------------*/

struct object {
	unsigned		magic;
#define OBJECT_MAGIC		0x32851d42
	unsigned		refcnt;
	unsigned		xid;
	struct objhead		*objhead;
	struct storage		*objstore;
	struct objexp		*objexp;

	struct ws		ws_o[1];
	unsigned char		*vary;

	struct ban		*ban;

	unsigned		pass;

	unsigned		response;

	unsigned		cacheable;

	unsigned		busy;
	unsigned		len;

	double			age;
	double			entered;
	double			ttl;
	double			grace;
	double			prefetch;

	double			last_modified;

	struct http		http[1];
	VTAILQ_ENTRY(object)	list;

	VTAILQ_HEAD(, storage)	store;

	VTAILQ_HEAD(, esi_bit)	esibits;

	double			last_use;

	/* Prefetch */
	struct object		*parent;
	struct object		*child;

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
	int			esis;

	struct worker		*wrk;

	socklen_t		sockaddrlen;
	socklen_t		mysockaddrlen;
	struct sockaddr		*sockaddr;
	struct sockaddr		*mysockaddr;
	struct listen_sock	*mylsock;

	/* formatted ascii client address */
	char			*addr;
	char			*port;
	struct srcaddr		*srcaddr;

	/* HTTP request */
	const char		*doclose;
	struct http		*http;
	struct http		*http0;

	struct ws		ws[1];
	char			*ws_ses;	/* WS above session data */
	char			*ws_req;	/* WS above request data */

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
	unsigned char		sendbody;
	unsigned char		wantbody;
	int			err_code;
	const char		*err_reason;

	VTAILQ_ENTRY(sess)	list;

	struct director		*director;
	struct vbe_conn		*vbe;
	struct bereq		*bereq;
	struct object		*obj;
	struct objhead		*objhead;
	struct VCL_conf		*vcl;

	/* Various internal stuff */
	struct sessmem		*mem;

	struct workreq		workreq;
	struct acct		acct;

	/* pointers to hash string components */
	unsigned		nhashptr;
	unsigned		ihashptr;
	unsigned		lhashptr;
	const char		**hashptr;
};


/* -------------------------------------------------------------------*/

/* Backend connection */
struct vbe_conn {
	unsigned		magic;
#define VBE_CONN_MAGIC		0x0c5e6592
	VTAILQ_ENTRY(vbe_conn)	list;
	struct backend		*backend;
	int			fd;
};

/* Prototypes etc ----------------------------------------------------*/


/* cache_acceptor.c */
void vca_return_session(struct sess *sp);
void vca_close_session(struct sess *sp, const char *why);
void VCA_Prep(struct sess *sp);
void VCA_Init(void);
extern int vca_pipes[2];

/* cache_backend.c */

void VBE_GetFd(struct sess *sp);
void VBE_ClosedFd(struct sess *sp);
void VBE_RecycleFd(struct sess *sp);
struct bereq * VBE_new_bereq(void);
void VBE_free_bereq(struct bereq *bereq);
void VBE_AddHostHeader(const struct sess *sp);
void VBE_Poll(void);

/* cache_backend_cfg.c */
void VBE_Init(void);
struct backend *VBE_AddBackend(struct cli *cli, const struct vrt_backend *vb);

/* cache_backend_poll.c */
void VBP_Init(void);

/* cache_ban.c */
int BAN_Add(struct cli *cli, const char *regexp, int hash);
void BAN_Init(void);
void BAN_NewObj(struct object *o);
void BAN_DestroyObj(struct object *o);
int BAN_CheckObject(struct object *o, const char *url, const char *hash);

/* cache_center.c [CNT] */
void CNT_Session(struct sess *sp);
void CNT_Init(void);

/* cache_cli.c [CLI] */
void CLI_Init(void);
void CLI_Run(void);
enum cli_set_e {MASTER_CLI, PUBLIC_CLI, DEBUG_CLI};
void CLI_AddFuncs(enum cli_set_e which, struct cli_proto *p);
extern pthread_t cli_thread;
#define ASSERT_CLI() do {assert(pthread_self() == cli_thread);} while (0)

/* cache_expiry.c */
void EXP_Insert(struct object *o);
void EXP_Init(void);
void EXP_Rearm(const struct object *o);
void EXP_Touch(const struct object *o, double now);
int EXP_NukeOne(struct sess *sp);

/* cache_fetch.c */
int Fetch(struct sess *sp);
int FetchReqBody(struct sess *sp);
void Fetch_Init(void);

/* cache_http.c */
const char *http_StatusMessage(unsigned);
void HTTP_Init(void);
void http_ClrHeader(struct http *to);
unsigned http_Write(struct worker *w, const struct http *hp, int resp);
void http_CopyResp(struct http *to, const struct http *fm);
void http_SetResp(struct http *to, const char *proto, const char *status,
    const char *response);
void http_FilterFields(struct worker *w, int fd, struct http *to,
    const struct http *fm, unsigned how);
void http_FilterHeader(struct sess *sp, unsigned how);
void http_PutProtocol(struct worker *w, int fd, struct http *to,
    const char *protocol);
void http_PutStatus(struct worker *w, int fd, struct http *to, int status);
void http_PutResponse(struct worker *w, int fd, struct http *to,
    const char *response);
void http_PrintfHeader(struct worker *w, int fd, struct http *to,
    const char *fmt, ...);
void http_SetHeader(struct worker *w, int fd, struct http *to, const char *hdr);
void http_SetH(struct http *to, unsigned n, const char *fm);
void http_ForceGet(struct http *to);
void http_Setup(struct http *ht, struct ws *ws);
int http_GetHdr(const struct http *hp, const char *hdr, char **ptr);
int http_GetHdrField(const struct http *hp, const char *hdr,
    const char *field, char **ptr);
int http_GetStatus(const struct http *hp);
const char *http_GetReq(const struct http *hp);
const char *http_GetProto(const struct http *hp);
int http_HdrIs(const struct http *hp, const char *hdr, const char *val);
int http_DissectRequest(struct sess *sp);
int http_DissectResponse(struct worker *w, const struct http_conn *htc,
    struct http *sp);
const char *http_DoConnection(struct http *hp);
void http_CopyHome(struct worker *w, int fd, struct http *hp);
void http_Unset(struct http *hp, const char *hdr);

/* cache_httpconn.c */
void HTC_Init(struct http_conn *htc, struct ws *ws, int fd);
int HTC_Reinit(struct http_conn *htc);
int HTC_Rx(struct http_conn *htc);
int HTC_Read(struct http_conn *htc, void *d, unsigned len);
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
void Lck__New(struct lock *lck, const char *w);
void Lck__Assert(struct lock *lck, int held);

/* public interface: */
void LCK_Init(void);
void Lck_Delete(struct lock *lck);
void Lck_CondWait(pthread_cond_t *cond, struct lock *lck);

#define Lck_New(a) Lck__New(a, #a);
#define Lck_Lock(a) Lck__Lock(a, __func__, __FILE__, __LINE__)
#define Lck_Unlock(a) Lck__Unlock(a, __func__, __FILE__, __LINE__)
#define Lck_Trylock(a) Lck__Trylock(a, __func__, __FILE__, __LINE__)
#define Lck_AssertHeld(a) Lck__Assert(a, 1)
#define Lck_AssertNotHeld(a) Lck__Assert(a, 0)

/* cache_panic.c */
void PAN_Init(void);

/* cache_pipe.c */
void PipeSession(struct sess *sp);

/* cache_pool.c */
void WRK_Init(void);
int WRK_Queue(struct workreq *wrq);
void WRK_QueueSession(struct sess *sp);
void WRK_Reset(struct worker *w, int *fd);
unsigned WRK_Flush(struct worker *w);
unsigned WRK_Write(struct worker *w, const void *ptr, int len);
unsigned WRK_WriteH(struct worker *w, const txt *hh, const char *suf);
#ifdef SENDFILE_WORKS
void WRK_Sendfile(struct worker *w, int fd, off_t off, unsigned len);
#endif  /* SENDFILE_WORKS */

/* cache_session.c [SES] */
void SES_Init(void);
struct sess *SES_New(const struct sockaddr *addr, unsigned len);
void SES_Delete(struct sess *sp);
void SES_RefSrcAddr(struct sess *sp);
void SES_Charge(struct sess *sp);

/* cache_shmlog.c */
void VSL_Init(void);
#ifdef SHMLOGHEAD_MAGIC
void VSL(enum shmlogtag tag, int id, const char *fmt, ...);
void WSLR(struct worker *w, enum shmlogtag tag, int id, txt t);
void WSL(struct worker *w, enum shmlogtag tag, int id, const char *fmt, ...);
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
void VRY_Create(const struct sess *sp);
int VRY_Match(const struct sess *sp, const unsigned char *vary);

/* cache_vcl.c */
void VCL_Init(void);
void VCL_Refresh(struct VCL_conf **vcc);
void VCL_Rel(struct VCL_conf **vcc);
void VCL_Get(struct VCL_conf **vcc);
void VCL_Poll(void);

#define VCL_RET_MAC(l,u,b,n)
#define VCL_MET_MAC(l,u,b) void VCL_##l##_method(struct sess *);
#include "vcl_returns.h"
#undef VCL_MET_MAC
#undef VCL_RET_MAC

/* cache_vrt_esi.c */

void ESI_Deliver(struct sess *);
void ESI_Destroy(struct object *);

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
int RFC2616_cache_policy(const struct sess *sp, const struct http *hp);

/* storage_synth.c */
struct vsb *SMS_Makesynth(struct object *obj);
void SMS_Finish(struct object *obj);

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
