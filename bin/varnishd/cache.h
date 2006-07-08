/*
 * $Id$
 */

#include <pthread.h>
#include <sys/time.h>
#include <queue.h>
#include <event.h>

#include "vcl_returns.h"

#define VCA_ADDRBUFSIZE		64	/* Sizeof ascii network address */

struct event_base;
struct cli;
struct sbuf;
struct sess;
struct object;
struct objhead;

/*--------------------------------------------------------------------
 * HTTP Request/Response/Header handling structure.
 * RSN: struct worker and struct session will have one of these embedded.
 */

typedef void http_callback_f(void *, int bad);

struct http {
	struct event		ev;
	http_callback_f		*callback;
	void			*arg;

	char			*s;		/* start of buffer */
	char			*e;		/* end of buffer */
	char			*v;		/* valid bytes */
	char			*t;		/* start of trailing data */


	char			*req;
	char			*url;
	char			*proto;
	char			*status;
	char			*response;
	
	char			**hdr;
	unsigned		nhdr;
};

/*--------------------------------------------------------------------*/

struct worker {
	struct event_base	*eb;
	struct event		e1, e2;
	struct sbuf		*sb;
	struct objhead		*nobjhead;
	struct object		*nobj;
};

#include "hash_slinger.h"

/* Storage -----------------------------------------------------------*/

struct storage {
	TAILQ_ENTRY(storage)	list;
	unsigned char		*ptr;
	unsigned		len;
	unsigned		space;
	void			*priv;
	struct stevedore	*stevedore;
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
	unsigned 		refcnt;
	unsigned		xid;
	struct objhead		*objhead;
	pthread_cond_t		cv;

	unsigned		heap_idx;
	unsigned		ban_seq;

	unsigned		response;

	unsigned		valid;
	unsigned		cacheable;

	unsigned		busy;
	unsigned		len;

	time_t			age;
	time_t			entered;
	time_t			ttl;

	char			*header;
	TAILQ_ENTRY(object)	list;

	TAILQ_ENTRY(object)	deathrow;

	TAILQ_HEAD(, storage)	store;
};

struct objhead {
	void			*hashpriv;

	pthread_mutex_t		mtx;
	TAILQ_HEAD(,object)	objects;
};

struct sess {
	int			fd;
	unsigned		xid;

	/* formatted ascii client address */
	char			addr[VCA_ADDRBUFSIZE];

	/* HTTP request */
	struct http		*http;

	time_t			t_req;
	time_t			t_resp;

	unsigned 		handling;

	TAILQ_ENTRY(sess)	list;

	struct backend		*backend;
	struct object		*obj;
	struct VCL_conf		*vcl;

	/* Various internal stuff */
	struct event		*rd_e;
	struct sessmem		*mem;
	time_t			t0;
};

struct backend {
	const char	*vcl_name;
	const char	*hostname;
	const char	*portname;
	unsigned	ip;
#if 0
	struct addrinfo	*addr;
	double		responsetime;
	double		timeout;
	double		bandwidth;
	int		down;
#endif

	/* internal stuff */
	struct vbe	*vbe;
};

/* Prototypes etc ----------------------------------------------------*/


/* cache_acceptor.c */
void vca_write(struct sess *sp, void *ptr, size_t len);
void vca_write_obj(struct worker *w, struct sess *sp);
void vca_flush(struct sess *sp);
void vca_return_session(struct sess *sp);
void vca_close_session(struct sess *sp, const char *why);
void VCA_Init(void);

/* cache_backend.c */
void VBE_Init(void);
int VBE_GetFd(struct backend *bp, void **ptr, unsigned xid);
void VBE_ClosedFd(void *ptr);
void VBE_RecycleFd(void *ptr);

/* cache_ban.c */
void BAN_Init(void);
void cli_func_url_purge(struct cli *cli, char **av, void *priv);
void BAN_NewObj(struct object *o);
int BAN_CheckObject(struct object *o, const char *url);

/* cache_expiry.c */
void EXP_Insert(struct object *o);
void EXP_Init(void);
void EXP_TTLchange(struct object *o);

/* cache_fetch.c */
int FetchSession(struct worker *w, struct sess *sp);

/* cache_hash.c */
struct object *HSH_Lookup(struct worker *w, struct http *h);
void HSH_Unbusy(struct object *o);
void HSH_Deref(struct object *o);
void HSH_Init(void);

/* cache_http.c */
struct http *http_New(void);
void http_Delete(struct http *hp);
int http_GetHdr(struct http *hp, const char *hdr, char **ptr);
int http_GetHdrField(struct http *hp, const char *hdr, const char *field, char **ptr);
int http_GetReq(struct http *hp, char **b);
int http_GetProto(struct http *hp, char **b);
int http_GetStatus(struct http *hp);
int http_HdrIs(struct http *hp, const char *hdr, const char *val);
int http_GetTail(struct http *hp, unsigned len, char **b, char **e);
int http_GetURL(struct http *hp, char **b);
void http_RecvHead(struct http *hp, int fd, struct event_base *eb, http_callback_f *func, void *arg);
int http_Dissect(struct http *sp, int fd, int rr);
enum http_build {
	Build_Pipe,
	Build_Pass,
	Build_Fetch,
	Build_Reply
};
void http_BuildSbuf(int fd, enum http_build mode, struct sbuf *sb, struct http *hp);

/* cache_main.c */
extern pthread_mutex_t sessmtx;

/* cache_pass.c */
void PassSession(struct worker *w, struct sess *sp);

/* cache_pipe.c */
void PipeSession(struct worker *w, struct sess *sp);

/* cache_pool.c */
void CacheInitPool(void);
void DealWithSession(void *arg);

/* cache_shmlog.c */
void VSL_Init(void);
#ifdef SHMLOGHEAD_MAGIC
void VSLR(enum shmlogtag tag, unsigned id, const char *b, const char *e);
void VSL(enum shmlogtag tag, unsigned id, const char *fmt, ...);
#define HERE() VSL(SLT_Debug, 0, "HERE: %s(%d)", __func__, __LINE__)
#define INCOMPL() do {							\
	VSL(SLT_Debug, 0, "INCOMPLETE AT: %s(%d)", __func__, __LINE__); \
	assert(__LINE__ == 0);						\
	} while (0)
#endif
extern struct varnish_stats *VSL_stats;

/* cache_response.c */
void RES_Error(struct worker *w, struct sess *sp, int error, const char *msg);

/* cache_vcl.c */
void RelVCL(struct VCL_conf *vc);
struct VCL_conf *GetVCL(void);
int CVCL_Load(const char *fn, const char *name);

#define VCL_RET_MAC(l,u,b)
#define VCL_MET_MAC(l,u,b) void VCL_##l##_method(struct sess *);
#include "vcl_returns.h"
#undef VCL_MET_MAC
#undef VCL_RET_MAC

#ifdef CLI_PRIV_H
cli_func_t	cli_func_config_list;
cli_func_t	cli_func_config_load;
cli_func_t	cli_func_config_unload;
cli_func_t	cli_func_config_use;
#endif

/* rfc2616.c */
int RFC2616_cache_policy(struct sess *sp, struct http *hp);
