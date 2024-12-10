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

#define CACHE_H_INCLUDED	// After vrt.h include.

#include "miniobj.h"
#include "vas.h"
#include "vqueue.h"
#include "vtree.h"

#include "vapi/vsl_int.h"

/*--------------------------------------------------------------------*/

struct vxids {
	uint64_t	vxid;
};

typedef struct vxids vxid_t;

#define NO_VXID ((struct vxids){0})
#define IS_NO_VXID(x) ((x).vxid == 0)
#define VXID_TAG(x) ((uintmax_t)((x).vxid & (VSL_CLIENTMARKER|VSL_BACKENDMARKER)))
#define VXID(u) ((uintmax_t)((u.vxid) & VSL_IDENTMASK))
#define IS_SAME_VXID(x, y) ((x).vxid == (y).vxid)

/*--------------------------------------------------------------------*/

struct body_status {
	const char		*name;
	int			nbr;
	int			avail;
	int			length_known;
};

#define BODYSTATUS(U, l, n, a, k) extern const struct body_status BS_##U[1];
#include "tbl/body_status.h"

typedef const struct body_status *body_status_t;

typedef const char *hdr_t;

/*--------------------------------------------------------------------*/

struct stream_close {
	unsigned		magic;
#define STREAM_CLOSE_MAGIC	0xc879c93d
	int			idx;
	unsigned		is_err;
	const char		*name;
	const char		*desc;
};
    extern const struct stream_close SC_NULL[1];
#define SESS_CLOSE(nm, stat, err, desc) \
    extern const struct stream_close SC_##nm[1];
#include "tbl/sess_close.h"


/*--------------------------------------------------------------------
 * Indices into http->hd[]
 */
enum {
#define SLTH(tag, ind, req, resp, sdesc, ldesc)	ind,
#include "tbl/vsl_tags_http.h"
};

/*--------------------------------------------------------------------*/

struct ban;
struct ban_proto;
struct cli;
struct http_conn;
struct listen_sock;
struct mempool;
struct objcore;
struct objhead;
struct pool;
struct req_step;
struct sess;
struct transport;
struct vcf;
struct VSC_lck;
struct VSC_main;
struct VSC_main_wrk;
struct worker;
struct worker_priv;

#define DIGEST_LEN		32

/*--------------------------------------------------------------------*/

struct lock { void *priv; };	// Opaque

/*--------------------------------------------------------------------
 * Workspace structure for quick memory allocation.
 */

#define WS_ID_SIZE 4

struct ws {
	unsigned		magic;
#define WS_MAGIC		0x35fac554
	char			id[WS_ID_SIZE];	/* identity */
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
	uint32_t		*wlb, *wlp, *wle;
	unsigned		wlr;
	vxid_t			wid;
};

/*--------------------------------------------------------------------*/

VRBT_HEAD(vrt_privs, vrt_priv);

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
 * TASK_QUEUE_{REQ|STR} are new req's (H1/H2), and subject to queue limit.
 *
 * TASK_QUEUE_RUSH is req's returning from waiting list
 *
 * NOTE: When changing the number of classes, update places marked with
 * TASK_QUEUE_RESERVE in params.h
 */
enum task_prio {
	TASK_QUEUE_BO,
	TASK_QUEUE_RUSH,
	TASK_QUEUE_REQ,
	TASK_QUEUE_STR,
	TASK_QUEUE_VCA,
	TASK_QUEUE_BG,
	TASK_QUEUE__END
};

#define TASK_QUEUE_HIGHEST_PRIORITY TASK_QUEUE_BO
#define TASK_QUEUE_RESERVE TASK_QUEUE_BG
#define TASK_QUEUE_LIMITED(prio) \
	(prio == TASK_QUEUE_REQ || prio == TASK_QUEUE_STR)

/*--------------------------------------------------------------------*/

struct worker {
	unsigned		magic;
#define WORKER_MAGIC		0x6391adcf
	int			strangelove;
	struct worker_priv	*wpriv;
	struct pool		*pool;
	struct VSC_main_wrk	*stats;
	struct vsl_log		*vsl;		// borrowed from req/bo

	struct pool_task	task[1];

	vtim_real		lastused;

	pthread_cond_t		cond;

	struct ws		aws[1];

	unsigned		cur_method;
	unsigned		seen_methods;

	struct wrk_vpi		*vpi;
};

/* Stored object -----------------------------------------------------
 * This is just to encapsulate the fields owned by the stevedore
 */

struct storeobj {
	const struct stevedore	*stevedore;
	void			*priv;
	uint64_t		priv2;
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
	uint64_t		fetched_so_far;
	uint64_t		delivered_so_far;
	uint64_t		transit_buffer;
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

#define OC_F_TRANSIENT (OC_F_PRIVATE | OC_F_HFM | OC_F_HFP)

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
	vtim_real		timer_when;
	VCL_INT			hits;


	vtim_real		t_origin;
	float			ttl;
	float			grace;
	float			keep;

	uint8_t			flags;

	uint8_t			exp_flags;

	uint16_t		oa_present;

	unsigned		timer_idx;	// XXX 4Gobj limit
	unsigned		waitinglist_gen;
	vtim_real		last_lru;
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
	unsigned		retries;
	struct req		*req;
	struct sess		*sp;
	struct worker		*wrk;

	/* beresp.body */
	struct vfp_ctx		*vfc;
	const char		*vfp_filter_list;
	/* bereq.body */
	const char		*vdp_filter_list;

	struct ws		ws[1];
	uintptr_t		ws_bo;
	struct http		*bereq0;
	struct http		*bereq;
	struct http		*beresp;
	struct objcore		*bereq_body;
	struct objcore		*stale_oc;
	struct objcore		*fetch_objcore;

	const char		*no_retry;

	struct http_conn	*htc;

	struct pool_task	fetch_task[1];

#define BERESP_FLAG(l, r, w, f, d) unsigned	l:1;
#define BEREQ_FLAG(l, r, w, d) BERESP_FLAG(l, r, w, 0, d)
#include "tbl/bereq_flags.h"
#include "tbl/beresp_flags.h"

	/* Timeouts */
	vtim_dur		connect_timeout;
	vtim_dur		first_byte_timeout;
	vtim_dur		between_bytes_timeout;
	vtim_dur		task_deadline;

	/* Timers */
	vtim_real		t_first;	/* First timestamp logged */
	vtim_real		t_resp;		/* response received */
	vtim_real		t_prev;		/* Previous timestamp logged */

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

	const char		*client_identity;
};

#define BUSYOBJ_TMO(bo, pfx, tmo)					\
	(isnan((bo)->tmo) ? cache_param->pfx##tmo : (bo)->tmo)


/*--------------------------------------------------------------------*/

struct reqtop {
	unsigned		magic;
#define REQTOP_MAGIC		0x57fbda52
	struct req		*topreq;
	struct vcl		*vcl0;
	struct vrt_privs	privs[1];
};

struct req {
	unsigned		magic;
#define REQ_MAGIC		0xfb4abf6d

	body_status_t		req_body_status;
	stream_close_t		doclose;
	unsigned		restarts;
	unsigned		esi_level;
	unsigned		waitinglist_gen;

	/* Delivery mode */
	unsigned		res_mode;
#define RES_ESI			(1<<4)
#define RES_PIPE		(1<<7)

	const struct req_step	*req_step;
	struct reqtop		*top;	/* esi_level == 0 request */

#define REQ_FLAG(l, r, w, d) unsigned	l:1;
#include "tbl/req_flags.h"

	uint16_t		err_code;
	const char		*err_reason;

	struct sess		*sp;
	struct worker		*wrk;
	struct pool_task	task[1];

	const struct transport	*transport;
	void			*transport_priv;

	VTAILQ_ENTRY(req)	w_list;

	struct objcore		*body_oc;

	/* Built Vary string == workspace reservation */
	uint8_t			*vary_b;
	uint8_t			*vary_e;

	uint8_t			digest[DIGEST_LEN];

	vtim_dur		d_ttl;
	vtim_dur		d_grace;

	const struct stevedore	*storage;

	const struct director	*director_hint;
	struct vcl		*vcl;

	uintptr_t		ws_req;		/* WS above request data */

	/* Timestamps */
	vtim_real		t_first;	/* First timestamp logged */
	vtim_real		t_prev;		/* Previous timestamp logged */
	vtim_real		t_req;		/* Headers complete */
	vtim_real		t_resp;		/* Entry to last deliver/synth */

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
	struct boc		*boc;		/* valid during cnt_transmit */

	/* resp.body */
	struct vdp_ctx		*vdc;
	const char		*vdp_filter_list;
	/* req.body */
	const char		*vfp_filter_list;

	/* Transaction VSL buffer */
	struct vsl_log		vsl[1];

	/* Temporary accounting */
	struct acct_req		acct;

	struct vrt_privs	privs[1];

	struct vcf		*vcf;
};

#define IS_TOPREQ(req) ((req)->top->topreq == (req))

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
	SA_LAST
};

struct sess {
	unsigned		magic;
#define SESS_MAGIC		0x2c2f9c5a

	uint16_t		sattr[SA_LAST];
	struct listen_sock	*listen_sock;
	int			refcnt;
	int			fd;
	vxid_t			vxid;

	struct lock		mtx;

	struct pool		*pool;

	struct ws		ws[1];

	vtim_real		t_open;		/* fd accepted */
	vtim_real		t_idle;		/* fd accepted or resp sent */
	vtim_dur		timeout_idle;
	vtim_dur		timeout_linger;
	vtim_dur		send_timeout;
	vtim_dur		idle_send_timeout;
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
#define IS_CLI() (pthread_equal(pthread_self(), cli_thread))
#define ASSERT_CLI() do {assert(IS_CLI());} while (0)

/* cache_http.c */
unsigned HTTP_estimate(unsigned nhttp);
void HTTP_Clone(struct http *to, const struct http * const fm);
void HTTP_Dup(struct http *to, const struct http * const fm);
struct http *HTTP_create(void *p, uint16_t nhttp, unsigned);
const char *http_Status2Reason(unsigned, const char **);
int http_IsHdr(const txt *hh, hdr_t hdr);
unsigned http_EstimateWS(const struct http *fm, unsigned how);
void http_PutResponse(struct http *to, const char *proto, uint16_t status,
    const char *response);
void http_FilterReq(struct http *to, const struct http *fm, unsigned how);
void HTTP_Encode(const struct http *fm, uint8_t *, unsigned len, unsigned how);
int HTTP_Decode(struct http *to, const uint8_t *fm);
void http_ForceHeader(struct http *to, hdr_t, const char *val);
void http_AppendHeader(struct http *to, hdr_t, const char *val);
void http_PrintfHeader(struct http *to, const char *fmt, ...)
    v_printflike_(2, 3);
void http_TimeHeader(struct http *to, const char *fmt, vtim_real now);
const char * http_ViaHeader(void);
void http_Proto(struct http *to);
void http_SetHeader(struct http *to, const char *header);
void http_SetH(struct http *to, unsigned n, const char *header);
void http_ForceField(struct http *to, unsigned n, const char *t);
void HTTP_Setup(struct http *, struct ws *, struct vsl_log *, enum VSL_tag_e);
void http_Teardown(struct http *ht);
int http_GetHdr(const struct http *hp, hdr_t, const char **ptr);
int http_GetHdrToken(const struct http *hp, hdr_t,
    const char *token, const char **pb, const char **pe);
int http_GetHdrField(const struct http *hp, hdr_t,
    const char *field, const char **ptr);
double http_GetHdrQ(const struct http *hp, hdr_t, const char *field);
ssize_t http_GetContentLength(const struct http *hp);
ssize_t http_GetContentRange(const struct http *hp, ssize_t *lo, ssize_t *hi);
const char * http_GetRange(const struct http *hp, ssize_t *lo, ssize_t *hi,
    ssize_t len);
uint16_t http_GetStatus(const struct http *hp);
int http_IsStatus(const struct http *hp, int);
void http_SetStatus(struct http *to, uint16_t status, const char *reason);
const char *http_GetMethod(const struct http *hp);
int http_HdrIs(const struct http *hp, hdr_t, const char *val);
void http_CopyHome(const struct http *hp);
void http_Unset(struct http *hp, hdr_t);
unsigned http_CountHdr(const struct http *hp, hdr_t);
void http_CollectHdr(struct http *hp, hdr_t);
void http_CollectHdrSep(struct http *hp, hdr_t, const char *sep);
void http_VSL_log(const struct http *hp);
void HTTP_Merge(struct worker *, struct objcore *, struct http *to);
uint16_t HTTP_GetStatusPack(struct worker *, struct objcore *oc);
int HTTP_IterHdrPack(struct worker *, struct objcore *, const char **);
#define HTTP_FOREACH_PACK(wrk, oc, ptr) \
	 for ((ptr) = NULL; HTTP_IterHdrPack(wrk, oc, &(ptr));)
const char *HTTP_GetHdrPack(struct worker *, struct objcore *, hdr_t);
stream_close_t http_DoConnection(struct http *hp, stream_close_t sc_close);
int http_IsFiltered(const struct http *hp, unsigned u, unsigned how);

#define HTTPH_R_PASS		(1 << 0)	/* Request (c->b) in pass mode */
#define HTTPH_R_FETCH		(1 << 1)	/* Request (c->b) for fetch */
#define HTTPH_A_INS		(1 << 2)	/* Response (b->o) for insert */
#define HTTPH_A_PASS		(1 << 3)	/* Response (b->o) for pass */
#define HTTPH_C_SPECIFIC	(1 << 4)	/* Connection-specific */

#define HTTPH(a, b, c) extern char b[];
#include "tbl/http_headers.h"

extern const char H__Status[];
extern const char H__Proto[];
extern const char H__Reason[];

// rfc7233,l,1207,1208
#define http_tok_eq(s1, s2)		(!vct_casecmp(s1, s2))
#define http_tok_at(s1, s2, l)		(!vct_caselencmp(s1, s2, l))
#define http_ctok_at(s, cs)		(!vct_caselencmp(s, cs, sizeof(cs) - 1))

// rfc7230,l,1037,1038
#define http_scheme_at(str, tok)	http_ctok_at(str, #tok "://")

// rfc7230,l,1144,1144
// rfc7231,l,1156,1158
#define http_method_eq(str, tok)	(!strcmp(str, #tok))

// rfc7230,l,1222,1222
// rfc7230,l,2848,2848
// rfc7231,l,3883,3885
// rfc7234,l,1339,1340
// rfc7234,l,1418,1419
#define http_hdr_eq(s1, s2)		http_tok_eq(s1, s2)
#define http_hdr_at(s1, s2, l)		http_tok_at(s1, s2, l)

// rfc7230,l,1952,1952
// rfc7231,l,604,604
#define http_coding_eq(str, tok)	http_tok_eq(str, #tok)

// rfc7231,l,1864,1864
#define http_expect_eq(str, tok)	http_tok_eq(str, #tok)

// rfc7233,l,1207,1208
#define http_range_at(str, tok, l)	http_tok_at(str, #tok, l)

/* cache_lck.c */

/* Internal functions, call only through macros below */
void Lck__Lock(struct lock *lck, const char *p,  int l);
void Lck__Unlock(struct lock *lck, const char *p,  int l);
int Lck__Trylock(struct lock *lck, const char *p,  int l);
void Lck__New(struct lock *lck, struct VSC_lck *, const char *);
int Lck__Held(const struct lock *lck);
int Lck__Owned(const struct lock *lck);
extern pthread_mutexattr_t mtxattr_errorcheck;

/* public interface: */
void Lck_Delete(struct lock *lck);
int Lck_CondWaitUntil(pthread_cond_t *, struct lock *, vtim_real when);
int Lck_CondWait(pthread_cond_t *, struct lock *);
int Lck_CondWaitTimeout(pthread_cond_t *, struct lock *, vtim_dur timeout);

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

typedef int objiterate_f(void *priv, unsigned flush,
    const void *ptr, ssize_t len);
#define OBJ_ITER_FLUSH	0x01
#define OBJ_ITER_END	0x02

int ObjIterate(struct worker *, struct objcore *,
    void *priv, objiterate_f *func, int final);

vxid_t ObjGetXID(struct worker *, struct objcore *);
uint64_t ObjGetLen(struct worker *, struct objcore *);
int ObjGetDouble(struct worker *, struct objcore *, enum obj_attr, double *);
int ObjGetU64(struct worker *, struct objcore *, enum obj_attr, uint64_t *);
int ObjCheckFlag(struct worker *, struct objcore *, enum obj_flags of);

/* cache_req_body.c */
ssize_t VRB_Iterate(struct worker *, struct vsl_log *, struct req *,
    objiterate_f *func, void *priv);

/* cache_session.c [SES] */

#define SESS_ATTR(UP, low, typ, len)					\
	int SES_Get_##low(const struct sess *sp, typ **dst);
#include "tbl/sess_attr.h"
const char *SES_Get_String_Attr(const struct sess *sp, enum sess_attr a);

/* cache_shmlog.c */
void VSLv(enum VSL_tag_e tag, vxid_t vxid, const char *fmt, va_list va);
void VSL(enum VSL_tag_e tag, vxid_t vxid, const char *fmt, ...)
    v_printflike_(3, 4);
void VSLs(enum VSL_tag_e tag, vxid_t vxid, const struct strands *s);
void VSLbv(struct vsl_log *, enum VSL_tag_e tag, const char *fmt, va_list va);
void VSLb(struct vsl_log *, enum VSL_tag_e tag, const char *fmt, ...)
    v_printflike_(3, 4);
void VSLbt(struct vsl_log *, enum VSL_tag_e tag, txt t);
void VSLbs(struct vsl_log *, enum VSL_tag_e tag, const struct strands *s);
void VSLb_ts(struct vsl_log *, const char *event, vtim_real first,
    vtim_real *pprev, vtim_real now);
void VSLb_bin(struct vsl_log *, enum VSL_tag_e, ssize_t, const void*);
int VSL_tag_is_masked(enum VSL_tag_e tag);

static inline void
VSLb_ts_req(struct req *req, const char *event, vtim_real now)
{

	if (isnan(req->t_first) || req->t_first == 0.)
		req->t_first = req->t_prev = now;
	VSLb_ts(req->vsl, event, req->t_first, &req->t_prev, now);
}

static inline void
VSLb_ts_busyobj(struct busyobj *bo, const char *event, vtim_real now)
{

	if (isnan(bo->t_first) || bo->t_first == 0.)
		bo->t_first = bo->t_prev = now;
	VSLb_ts(bo->vsl, event, bo->t_first, &bo->t_prev, now);
}

/* cache_vcl.c */
const char *VCL_Name(const struct vcl *);

/* cache_wrk.c */

typedef void *bgthread_t(struct worker *, void *priv);
void WRK_BgThread(pthread_t *thr, const char *name, bgthread_t *func,
    void *priv);

/* cache_ws.c */
void WS_Init(struct ws *ws, const char *id, void *space, unsigned len);

unsigned WS_ReserveSize(struct ws *, unsigned);
unsigned WS_ReserveAll(struct ws *);
void WS_Release(struct ws *ws, unsigned bytes);
void WS_ReleaseP(struct ws *ws, const char *ptr);
void WS_Assert(const struct ws *ws);
void WS_Reset(struct ws *ws, uintptr_t);
void *WS_Alloc(struct ws *ws, unsigned bytes);
void *WS_Copy(struct ws *ws, const void *str, int len);
uintptr_t WS_Snapshot(struct ws *ws);
int WS_Allocated(const struct ws *ws, const void *ptr, ssize_t len);
unsigned WS_Dump(const struct ws *ws, char, size_t off, void *buf, size_t len);

static inline void *
WS_Reservation(const struct ws *ws)
{

	WS_Assert(ws);
	AN(ws->r);
	AN(ws->f);
	return (ws->f);
}

static inline unsigned
WS_ReservationSize(const struct ws *ws)
{

	AN(ws->r);
	return (ws->r - ws->f);
}

static inline unsigned
WS_ReserveLumps(struct ws *ws, size_t sz)
{

	AN(sz);
	return (WS_ReserveAll(ws) / sz);
}

/* cache_ws_common.c */
void WS_MarkOverflow(struct ws *ws);
int WS_Overflowed(const struct ws *ws);

const char *WS_Printf(struct ws *ws, const char *fmt, ...) v_printflike_(2, 3);

void WS_VSB_new(struct vsb *, struct ws *);
char *WS_VSB_finish(struct vsb *, struct ws *, size_t *);

/* WS utility */
#define WS_TASK_ALLOC_OBJ(ctx, ptr, magic) do {			\
	ptr = WS_Alloc((ctx)->ws, sizeof *(ptr));		\
	if ((ptr) == NULL)					\
		VRT_fail(ctx, "Out of workspace for " #magic);	\
	else							\
		INIT_OBJ(ptr, magic);				\
} while(0)

/* cache_rfc2616.c */
void RFC2616_Ttl(struct busyobj *, vtim_real now, vtim_real *t_origin,
    float *ttl, float *grace, float *keep);
unsigned RFC2616_Req_Gzip(const struct http *);
int RFC2616_Do_Cond(const struct req *sp);
void RFC2616_Weaken_Etag(struct http *hp);
void RFC2616_Vary_AE(struct http *hp);
const char * RFC2616_Strong_LM(const struct http *hp, struct worker *wrk,
    struct objcore *oc);

/*
 * We want to cache the most recent timestamp in wrk->lastused to avoid
 * extra timestamps in cache_pool.c.  Hide this detail with a macro
 */
#define W_TIM_real(w) ((w)->lastused = VTIM_real())
