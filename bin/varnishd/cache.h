/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006 Linpro AS
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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

#define MAX_IOVS	(HTTP_HDR_MAX * 2)

/* Amount of per-worker logspace */
#define WLOGSPACE	8192

struct cli;
struct vsb;
struct sess;
struct object;
struct objhead;
struct workreq;

/*--------------------------------------------------------------------*/

enum step {
#define STEP(l, u)	STP_##u,
#include "steps.h"
#undef STEP
};

/*--------------------------------------------------------------------
 * HTTP Request/Response/Header handling structure.
 * RSN: struct worker and struct session will have one of these embedded.
 */

struct http_hdr {
	char			*b;
	char			*e;
};

struct http {
	unsigned		magic;
#define HTTP_MAGIC		0x6428b5c9

	char			*s;		/* (S)tart of buffer */
	char			*t;		/* start of (T)railing data */
	char			*v;		/* end of (V)alid bytes */
	char			*f;		/* first (F)ree byte */
	char			*e;		/* (E)nd of buffer */

	unsigned char		conds;		/* If-* headers present */
	enum httpwhence {
		HTTP_Rx,
		HTTP_Tx,
		HTTP_Obj
	}			logtag;

	struct http_hdr		hd[HTTP_HDR_MAX];
	unsigned char		hdf[HTTP_HDR_MAX];
#define HDF_FILTER		(1 << 0)	/* Filtered by Connection */
	unsigned		nhd;
};

/*--------------------------------------------------------------------*/

struct acct {
	time_t			first;
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

	time_t			idle;

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
	unsigned char		wlog[WLOGSPACE];
	unsigned char		*wlp, *wle;
	unsigned		wlr;
};

struct workreq {
	TAILQ_ENTRY(workreq)	list;
	struct sess		*sess;
};

#include "hash_slinger.h"

/* Backend Connection ------------------------------------------------*/

struct vbe_conn {
	unsigned		magic;
#define VBE_CONN_MAGIC		0x0c5e6592
	TAILQ_ENTRY(vbe_conn)	list;
	struct backend		*backend;
	int			fd;
	struct http		*http;
	struct http		*http2;
	struct http		http_mem[2];
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

#include "stevedore.h"

/*
 * XXX: in the longer term, we want to support multiple stevedores,
 * XXX: selected by some kind of heuristics based on size, lifetime
 * XXX: etc etc.  For now we support only one.
 */
extern struct stevedore *stevedore;

/* -------------------------------------------------------------------*/

struct object {
	unsigned		magic;
#define OBJECT_MAGIC		0x32851d42
	unsigned 		refcnt;
	unsigned		xid;
	struct objhead		*objhead;

	unsigned		heap_idx;
	unsigned		ban_seq;

	unsigned		pass;

	unsigned		response;

	unsigned		valid;
	unsigned		cacheable;

	unsigned		busy;
	unsigned		len;

	time_t			age;
	time_t			entered;
	time_t			ttl;

	time_t			last_modified;

	struct http		http;
	TAILQ_ENTRY(object)	list;

	TAILQ_ENTRY(object)	deathrow;

	TAILQ_HEAD(, storage)	store;

	TAILQ_HEAD(, sess)	waitinglist;
};

struct objhead {
	unsigned		magic;
#define OBJHEAD_MAGIC		0x1b96615d
	void			*hashpriv;

	pthread_mutex_t		mtx;
	TAILQ_HEAD(,object)	objects;
};

/* -------------------------------------------------------------------*/

struct sess {
	unsigned		magic;
#define SESS_MAGIC		0x2c2f9c5a
	int			fd;
	int			id;
	unsigned		xid;

	struct worker		*wrk;

	unsigned		sockaddrlen;
	struct sockaddr		*sockaddr;

	/* formatted ascii client address */
	char			addr[TCP_ADDRBUFSIZE];
	char			port[TCP_PORTBUFSIZE];
	struct srcaddr		*srcaddr;

	/* HTTP request */
	const char		*doclose;
	struct http		*http;

	struct timespec		t_open;
	struct timespec		t_req;
	struct timespec		t_resp;
	struct timespec		t_end;

	enum step		step;
	unsigned 		handling;
	unsigned char		wantbody;
	int			err_code;
	const char		*err_reason;

	TAILQ_ENTRY(sess)	list;

	struct backend		*backend;
	struct object		*obj;
	struct VCL_conf		*vcl;

	/* Various internal stuff */
	struct sessmem		*mem;

	struct workreq		workreq;
	struct acct		acct;
};

struct backend {
	unsigned		magic;
#define BACKEND_MAGIC		0x64c4c7c6
	const char		*vcl_name;
	const char		*hostname;
	const char		*portname;

	struct addrinfo		*addr;
	struct addrinfo		*last_addr;

	TAILQ_HEAD(,vbe_conn)	connlist;

	double			dnsttl;
	double			dnstime;
#if 0
	double			responsetime;
	double			timeout;
	double			bandwidth;
	int			down;
#endif

};

/* Prototypes etc ----------------------------------------------------*/


/* cache_acceptor.c */
void vca_return_session(struct sess *sp);
void vca_close_session(struct sess *sp, const char *why);
void VCA_Prep(struct sess *sp);
void VCA_Init(void);

/* cache_backend.c */
void VBE_Init(void);
struct vbe_conn *VBE_GetFd(struct sess *sp);
void VBE_ClosedFd(struct worker *w, struct vbe_conn *vc, int already);
void VBE_RecycleFd(struct worker *w, struct vbe_conn *vc);

/* cache_ban.c */
void BAN_Init(void);
void cli_func_url_purge(struct cli *cli, char **av, void *priv);
void BAN_NewObj(struct object *o);
int BAN_CheckObject(struct object *o, const char *url);

/* cache_center.c [CNT] */
void CNT_Session(struct sess *sp);
void CNT_Init(void);

/* cache_cli.c [CLI] */
void CLI_Init(void);

/* cache_expiry.c */
void EXP_Insert(struct object *o);
void EXP_Init(void);
void EXP_TTLchange(struct object *o);

/* cache_fetch.c */
int Fetch(struct sess *sp);

/* cache_hash.c */
void HSH_Prealloc(struct sess *sp);
void HSH_Freestore(struct object *o);
struct object *HSH_Lookup(struct sess *sp);
void HSH_Unbusy(struct object *o);
void HSH_Ref(struct object *o);
void HSH_Deref(struct object *o);
void HSH_Init(void);

/* cache_http.c */
void HTTP_Init(void);
void http_ClrHeader(struct http *to);
void http_CopyHttp(struct http *to, struct http *fm);
unsigned http_Write(struct worker *w, struct http *hp, int resp);
void http_GetReq(struct worker *w, int fd, struct http *to, struct http *fm);
void http_CopyReq(struct worker *w, int fd, struct http *to, struct http *fm);
void http_CopyResp(struct worker *w, int fd, struct http *to, struct http *fm);
void http_SetResp(struct worker *w, int fd, struct http *to, const char *proto, const char *status, const char *response);
void http_FilterHeader(struct worker *w, int fd, struct http *to, struct http *fm, unsigned how);
void http_PrintfHeader(struct worker *w, int fd, struct http *to, const char *fmt, ...);
void http_SetHeader(struct worker *w, int fd, struct http *to, const char *hdr);
void http_Setup(struct http *ht, void *space, unsigned len);
int http_GetHdr(struct http *hp, const char *hdr, char **ptr);
int http_GetHdrField(struct http *hp, const char *hdr, const char *field, char **ptr);
int http_GetStatus(struct http *hp);
int http_IsBodyless(struct http *hp);
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
void RES_Error(struct sess *sp, int code, const char *reason);
void RES_WriteObj(struct sess *sp);

/* cache_vcl.c */
void VCL_Init(void);
void VCL_Refresh(struct VCL_conf **vcc);
void VCL_Rel(struct VCL_conf **vcc);
void VCL_Get(struct VCL_conf **vcc);

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
