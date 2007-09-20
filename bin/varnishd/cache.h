/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2007 Linpro AS
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

#include <sys/time.h>
#include <sys/uio.h>
#include <sys/socket.h>

#include <pthread.h>
#include <stdint.h>

#include "queue.h"
#include "vsb.h"

#include "libvarnish.h"

#include "vcl_returns.h"
#include "common.h"
#include "miniobj.h"

enum {
	HTTP_HDR_REQ,
	HTTP_HDR_URL,
	HTTP_HDR_PROTO,
	HTTP_HDR_STATUS,
	HTTP_HDR_RESPONSE,
	/* add more here */
	HTTP_HDR_FIRST,
	HTTP_HDR_MAX = 32
};

/* Note: intentionally not IOV_MAX */
#define MAX_IOVS	(HTTP_HDR_MAX * 2)

/* Amount of per-worker logspace */
#define WLOGSPACE	8192

struct cli;
struct vsb;
struct sess;
struct object;
struct objhead;
struct workreq;
struct addrinfo;

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
	char			*s;		/* (S)tart of buffer */
	char			*e;		/* (E)nd of buffer */
	char			*f;		/* (F)ree pointer */
	char			*r;		/* (R)eserved length */
};

void WS_Init(struct ws *ws, void *space, unsigned len);
unsigned WS_Reserve(struct ws *ws, unsigned bytes);
void WS_Release(struct ws *ws, unsigned bytes);
void WS_ReleaseP(struct ws *ws, char *ptr);
void WS_Assert(struct ws *ws);
void WS_Reset(struct ws *ws);
char *WS_Alloc(struct ws *ws, unsigned bytes);

/*--------------------------------------------------------------------
 * HTTP Request/Response/Header handling structure.
 */

struct http_hdr {
	char			*b;
	char			*e;
};

enum httpwhence {
	HTTP_Rx,
	HTTP_Tx,
	HTTP_Obj
};

struct http {
	unsigned		magic;
#define HTTP_MAGIC		0x6428b5c9

	struct ws		ws[1];
	char			*rx_s, *rx_e;	/* Received Request */
	char			*pl_s, *pl_e;	/* Pipelined bytes */

	unsigned char		conds;		/* If-* headers present */
	enum httpwhence 	logtag;

	struct http_hdr		hd[HTTP_HDR_MAX];
	unsigned char		hdf[HTTP_HDR_MAX];
#define HDF_FILTER		(1 << 0)	/* Filtered by Connection */
	unsigned		nhd;
};

/*--------------------------------------------------------------------*/

struct acct {
	double			first;
	uint64_t		sess;
	uint64_t		req;
	uint64_t		pipe;
	uint64_t		pass;
	uint64_t		fetch;
	uint64_t		hdrbytes;
	uint64_t		bodybytes;
};

/*--------------------------------------------------------------------*/

struct worker {
	unsigned		magic;
#define WORKER_MAGIC		0x6391adcf
	struct objhead		*nobjhead;
	struct object		*nobj;

	double			used;

	int			pipe[2];

	TAILQ_ENTRY(worker)	list;
	struct workreq		*wrq;

	int			*wfd;
	unsigned		werr;	/* valid after WRK_Flush() */
	struct iovec		iov[MAX_IOVS];
	unsigned		niov;
	size_t			liov;

	struct VCL_conf		*vcl;
	struct srcaddr		*srcaddr;
	struct acct		acct;

	unsigned char		*wlp, *wle;
	unsigned		wlr;
	unsigned char		wlog[WLOGSPACE];
};

struct workreq {
	TAILQ_ENTRY(workreq)	list;
	struct sess		*sess;
};

#include "hash_slinger.h"

/* Backend Request ---------------------------------------------------*/

struct bereq {
	unsigned		magic;
#define BEREQ_MAGIC		0x3b6d250c
	TAILQ_ENTRY(bereq)	list;
	void			*space;
	unsigned		len;
	struct http		http[1];
};

/* Storage -----------------------------------------------------------*/

struct storage {
	unsigned		magic;
#define STORAGE_MAGIC		0x1a4e51c0
	TAILQ_ENTRY(storage)	list;
	struct stevedore	*stevedore;
	void			*priv;

	unsigned char		*ptr;
	unsigned		len;
	unsigned		space;

	int			fd;
	off_t			where;
};

/* -------------------------------------------------------------------*/

struct object {
	unsigned		magic;
#define OBJECT_MAGIC		0x32851d42
	unsigned 		refcnt;
	unsigned		xid;
	struct objhead		*objhead;

	unsigned char		*vary;

	unsigned		heap_idx;
	unsigned		ban_seq;

	unsigned		pass;

	unsigned		response;

	unsigned		valid;
	unsigned		cacheable;

	unsigned		busy;
	unsigned		len;

	double			age;
	double			entered;
	double			ttl;

	double			last_modified;

	struct http		http;
	TAILQ_ENTRY(object)	list;

	TAILQ_ENTRY(object)	deathrow;

	TAILQ_HEAD(, storage)	store;

	TAILQ_HEAD(, sess)	waitinglist;

	double			lru_stamp;
	TAILQ_ENTRY(object)	lru;
};

struct objhead {
	unsigned		magic;
#define OBJHEAD_MAGIC		0x1b96615d
	void			*hashpriv;

	pthread_mutex_t		mtx;
	TAILQ_HEAD(,object)	objects;
	char			*hash;
	unsigned		hashlen;
};

/* -------------------------------------------------------------------*/

struct sess {
	unsigned		magic;
#define SESS_MAGIC		0x2c2f9c5a
	int			fd;
	int			id;
	unsigned		xid;

	struct worker		*wrk;

	socklen_t		sockaddrlen;
	socklen_t		mysockaddrlen;
	struct sockaddr		*sockaddr;
	struct sockaddr		*mysockaddr;

	/* formatted ascii client address */
	char			addr[TCP_ADDRBUFSIZE];
	char			port[TCP_PORTBUFSIZE];
	struct srcaddr		*srcaddr;

	/* HTTP request */
	const char		*doclose;
	struct http		*http;

	/* Timestamps, all on TIM_real() timescale */
	double			t_open;
	double			t_req;
	double			t_resp;
	double			t_end;

	enum step		step;
	unsigned 		handling;
	unsigned char		wantbody;
	int			err_code;
	const char		*err_reason;

	TAILQ_ENTRY(sess)	list;

	struct backend		*backend;
	struct bereq		*bereq;
	struct object		*obj;
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
	TAILQ_ENTRY(vbe_conn)	list;
	struct backend		*backend;
	int			fd;
	void			*priv;
};


/* Backend method */
typedef struct vbe_conn *vbe_getfd_f(struct sess *sp);
typedef void vbe_close_f(struct worker *w, struct vbe_conn *vc);
typedef void vbe_recycle_f(struct worker *w, struct vbe_conn *vc);
typedef void vbe_init_f(void);
typedef const char *vbe_gethostname_f(struct backend *);
typedef void vbe_cleanup_f(struct backend *);
typedef void vbe_updatehealth_f(struct sess *sp, struct vbe_conn *vc, int);

struct backend_method {
	const char		*name;
	vbe_getfd_f		*getfd;
	vbe_close_f		*close;
	vbe_recycle_f		*recycle;
	vbe_cleanup_f		*cleanup;
	vbe_gethostname_f	*gethostname;
	vbe_updatehealth_f	*updatehealth;
	vbe_init_f		*init;
};

/* Backend indstance */
struct backend {
	unsigned		magic;
#define BACKEND_MAGIC		0x64c4c7c6
	char			*vcl_name;

	TAILQ_ENTRY(backend)	list;
	int			refcount;
	pthread_mutex_t		mtx;

	struct backend_method	*method;
	void			*priv;

	int			health;
	double			last_check;
	int			minute_limit;
};

/*
 * NB: This list is not locked, it is only ever manipulated from the
 * cachers CLI thread.
 */
TAILQ_HEAD(backendlist, backend);

/* Prototypes etc ----------------------------------------------------*/


/* cache_acceptor.c */
void vca_return_session(struct sess *sp);
void vca_close_session(struct sess *sp, const char *why);
void VCA_Prep(struct sess *sp);
void VCA_Init(void);
extern int vca_pipes[2];

/* cache_backend.c */

void VBE_Init(void);
struct vbe_conn *VBE_GetFd(struct sess *sp);
void VBE_ClosedFd(struct worker *w, struct vbe_conn *vc);
void VBE_RecycleFd(struct worker *w, struct vbe_conn *vc);
struct bereq * VBE_new_bereq(void);
void VBE_free_bereq(struct bereq *bereq);
extern struct backendlist backendlist;
void VBE_DropRef(struct backend *);
void VBE_DropRefLocked(struct backend *);
struct backend *VBE_NewBackend(struct backend_method *method);
struct vbe_conn *VBE_NewConn(void);
void VBE_ReleaseConn(struct vbe_conn *);
void VBE_UpdateHealth(struct sess *sp, struct vbe_conn *, int);

/* convenience functions for backend methods */
int VBE_TryConnect(struct sess *sp, struct addrinfo *ai);
int VBE_CheckFd(int fd);

/* cache_backend_simple.c */
extern struct backend_method	backend_method_simple;
extern struct backend_method	backend_method_random;
extern struct backend_method	backend_method_round_robin;

/* cache_ban.c */
void AddBan(const char *, int hash);
void BAN_Init(void);
void cli_func_url_purge(struct cli *cli, char **av, void *priv);
void cli_func_hash_purge(struct cli *cli, char **av, void *priv);
void BAN_NewObj(struct object *o);
int BAN_CheckObject(struct object *o, const char *url, const char *hash);

/* cache_center.c [CNT] */
void CNT_Session(struct sess *sp);
void CNT_Init(void);

/* cache_cli.c [CLI] */
void CLI_Init(void);

/* cache_expiry.c */
void EXP_Insert(struct object *o);
void EXP_Init(void);
void EXP_TTLchange(struct object *o);
void EXP_Terminate(struct object *o);

/* cache_fetch.c */
int Fetch(struct sess *sp);

/* cache_hash.c */
void HSH_Prealloc(struct sess *sp);
int HSH_Compare(struct sess *sp, struct objhead *o);
void HSH_Copy(struct sess *sp, struct objhead *o);
struct object *HSH_Lookup(struct sess *sp);
void HSH_Unbusy(struct object *o);
void HSH_Ref(struct object *o);
void HSH_Deref(struct object *o);
void HSH_Init(void);

/* cache_http.c */
const char *http_StatusMessage(int);
void HTTP_Init(void);
void http_ClrHeader(struct http *to);
unsigned http_Write(struct worker *w, struct http *hp, int resp);
void http_CopyResp(struct http *to, struct http *fm);
void http_SetResp(struct http *to, const char *proto, const char *status, const char *response);
void http_FilterFields(struct worker *w, int fd, struct http *to, struct http *fm, unsigned how);
void http_FilterHeader(struct sess *sp, unsigned how);
void http_PutProtocol(struct worker *w, int fd, struct http *to, const char *protocol);
void http_PutStatus(struct worker *w, int fd, struct http *to, int status);
void http_PutResponse(struct worker *w, int fd, struct http *to, const char *response);
void http_PrintfHeader(struct worker *w, int fd, struct http *to, const char *fmt, ...);
void http_SetHeader(struct worker *w, int fd, struct http *to, const char *hdr);
void http_SetH(struct http *to, unsigned n, const char *fm);
void http_Setup(struct http *ht, void *space, unsigned len);
int http_GetHdr(struct http *hp, const char *hdr, char **ptr);
int http_GetHdrField(struct http *hp, const char *hdr, const char *field, char **ptr);
int http_GetStatus(struct http *hp);
const char *http_GetProto(struct http *hp);
int http_HdrIs(struct http *hp, const char *hdr, const char *val);
int http_GetTail(struct http *hp, unsigned len, char **b, char **e);
int http_Read(struct http *hp, int fd, void *b, unsigned len);
void http_RecvPrep(struct http *hp);
int http_RecvPrepAgain(struct http *hp);
int http_RecvSome(int fd, struct http *hp);
int http_RecvHead(struct http *hp, int fd);
int http_DissectRequest(struct worker *w, struct http *sp, int fd);
int http_DissectResponse(struct worker *w, struct http *sp, int fd);
void http_DoConnection(struct sess *sp);
void http_CopyHome(struct worker *w, int fd, struct http *hp);
void http_Unset(struct http *hp, const char *hdr);


#define HTTPH(a, b, c, d, e, f, g) extern char b[];
#include "http_headers.h"
#undef HTTPH

/* cache_pipe.c */
void PipeSession(struct sess *sp);

/* cache_pool.c */
void WRK_Init(void);
void WRK_QueueSession(struct sess *sp);
void WRK_Reset(struct worker *w, int *fd);
int WRK_Flush(struct worker *w);
unsigned WRK_Write(struct worker *w, const void *ptr, int len);
unsigned WRK_WriteH(struct worker *w, struct http_hdr *hh, const char *suf);
#ifdef HAVE_SENDFILE
void WRK_Sendfile(struct worker *w, int fd, off_t off, unsigned len);
#endif  /* HAVE_SENDFILE */

/* cache_session.c [SES] */
void SES_Init(void);
struct sess *SES_New(struct sockaddr *addr, unsigned len);
void SES_Delete(struct sess *sp);
void SES_RefSrcAddr(struct sess *sp);
void SES_Charge(struct sess *sp);

/* cache_shmlog.c */

void VSL_Init(void);
#ifdef SHMLOGHEAD_MAGIC
void VSLR(enum shmlogtag tag, unsigned id, const char *b, const char *e);
void VSL(enum shmlogtag tag, unsigned id, const char *fmt, ...);
void WSLR(struct worker *w, enum shmlogtag tag, unsigned id, const char *b, const char *e);
void WSL(struct worker *w, enum shmlogtag tag, unsigned id, const char *fmt, ...);
void WSL_Flush(struct worker *w);
#define INCOMPL() do {							\
	VSL(SLT_Debug, 0, "INCOMPLETE AT: %s(%d)", __func__, __LINE__); \
	fprintf(stderr,"INCOMPLETE AT: %s(%d)\n", (const char *)__func__, __LINE__);	\
	abort();							\
	} while (0)
#endif

/* cache_response.c */
void RES_BuildHttp(struct sess *sp);
void RES_Error(struct sess *sp, int code, const char *reason);
void RES_WriteObj(struct sess *sp);

/* cache_synthetic.c */
void SYN_ErrorPage(struct sess *sp, int status, const char *reason, int ttl);

/* cache_vary.c */
void VRY_Create(struct sess *sp);
int VRY_Match(struct sess *sp, unsigned char *vary);

/* cache_vcl.c */
void VCL_Init(void);
void VCL_Refresh(struct VCL_conf **vcc);
void VCL_Rel(struct VCL_conf **vcc);
void VCL_Get(struct VCL_conf **vcc);

/* cache_lru.c */
// void LRU_Init(void);
void LRU_Enter(struct object *o, double stamp);
void LRU_Remove(struct object *o);
int LRU_DiscardOne(void);
int LRU_DiscardSpace(int64_t quota);
int LRU_DiscardTime(double cutoff);

#define VCL_RET_MAC(l,u,b,n)
#define VCL_MET_MAC(l,u,b) void VCL_##l##_method(struct sess *);
#include "vcl_returns.h"
#undef VCL_MET_MAC
#undef VCL_RET_MAC

#ifdef CLI_PRIV_H
cli_func_t	cli_func_config_list;
cli_func_t	cli_func_config_load;
cli_func_t	cli_func_config_discard;
cli_func_t	cli_func_config_use;
cli_func_t	cli_func_dump_pool;
#endif

/* rfc2616.c */
int RFC2616_cache_policy(struct sess *sp, struct http *hp);

#if 1
#define MTX			pthread_mutex_t
#define MTX_INIT(foo)		AZ(pthread_mutex_init(foo, NULL))
#define MTX_DESTROY(foo)	AZ(pthread_mutex_destroy(foo))
#define LOCK(foo)		AZ(pthread_mutex_lock(foo))
#define UNLOCK(foo)		AZ(pthread_mutex_unlock(foo))
#else
#define MTX			pthread_mutex_t
#define MTX_INIT(foo)		AZ(pthread_mutex_init(foo, NULL))
#define MTX_DESTROY(foo)	AZ(pthread_mutex_destroy(foo))
#define LOCK(foo) 					\
do { 							\
	if (pthread_mutex_trylock(foo)) {		\
		VSL(SLT_Debug, 0,			\
		    "MTX_CONTEST(%s,%s,%d," #foo ")",	\
		    __func__, __FILE__, __LINE__);	\
		AZ(pthread_mutex_lock(foo)); 		\
	} else if (1) {					\
		VSL(SLT_Debug, 0,			\
		    "MTX_LOCK(%s,%s,%d," #foo ")",	\
		    __func__, __FILE__, __LINE__); 	\
	}						\
} while (0);
#define UNLOCK(foo)					\
do {							\
	AZ(pthread_mutex_unlock(foo));			\
	if (1)						\
		VSL(SLT_Debug, 0,			\
		    "MTX_UNLOCK(%s,%s,%d," #foo ")",	\
		    __func__, __FILE__, __LINE__);	\
} while (0);
#endif
