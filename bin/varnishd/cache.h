/*
 * $Id$
 */

#include <sys/queue.h>

struct event_base;
struct sbuf;

#ifdef EV_TIMEOUT
struct worker {
	struct event_base	*eb;
	struct event		e1, e2;
	struct sbuf		*sb;
	struct object		*nobj;
};
#else
struct worker;
#endif

/* Hashing -----------------------------------------------------------*/

typedef void hash_init_f(void);
typedef struct object *hash_lookup_f(unsigned char *key, struct object *nobj);
typedef void hash_deref_f(struct object *obj);
typedef void hash_purge_f(struct object *obj);

struct hash_slinger {
	const char		*name;
	hash_init_f		*init;
	hash_lookup_f		*lookup;
	hash_deref_f		*deref;
	hash_purge_f		*purge;
};

extern struct hash_slinger hsl_slinger;

extern struct hash_slinger	*hash;

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

/* Storage -----------------------------------------------------------*/

struct sess;

#define VCA_ADDRBUFSIZE		32

struct object {	
	unsigned char		hash[16];
	unsigned 		refcnt;
	unsigned		valid;
	unsigned		cacheable;

	unsigned		busy;
	unsigned		len;
	time_t			ttl;

	char			*header;

	TAILQ_HEAD(, storage)	store;
};

#include "vcl_returns.h"

struct sess {
	int			fd;

	/* formatted ascii client address */
	char			addr[VCA_ADDRBUFSIZE];

	/* HTTP request */
	struct http		*http;

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
void vca_write_obj(struct sess *sp, struct sbuf *hdr);
void vca_flush(struct sess *sp);
void vca_return_session(struct sess *sp);
void vca_close_session(struct sess *sp, const char *why);
void VCA_Init(void);

/* cache_backend.c */
void VBE_Init(void);
int VBE_GetFd(struct backend *bp, void **ptr);
void VBE_ClosedFd(void *ptr);
void VBE_RecycleFd(void *ptr);

/* cache_expiry.c */
void EXP_Init(void);

/* cache_fetch.c */
int FetchSession(struct worker *w, struct sess *sp);

/* cache_http.c */
typedef void http_callback_f(void *, int good);
struct http;
struct http *http_New(void);
void http_Delete(struct http *hp);
int http_GetHdr(struct http *hp, const char *hdr, char **ptr);
int http_GetHdrField(struct http *hp, const char *hdr, const char *field, char **ptr);
int http_GetReq(struct http *hp, char **b);
int http_GetStatus(struct http *hp);
int http_HdrIs(struct http *hp, const char *hdr, const char *val);
int http_GetTail(struct http *hp, unsigned len, char **b, char **e);
int http_GetURL(struct http *hp, char **b);
void http_RecvHead(struct http *hp, int fd, struct event_base *eb, http_callback_f *func, void *arg);
void http_Dissect(struct http *sp, int fd, int rr);
void http_BuildSbuf(int resp, struct sbuf *sb, struct http *hp);

/* cache_main.c */
extern pthread_mutex_t sessmtx;

/* cache_pass.c */
void PassSession(struct worker *w, struct sess *sp);

/* cache_pipe.c */
void PipeSession(struct worker *w, struct sess *sp);

/* cache_pool.c */
void CacheInitPool(void);
void DealWithSession(void *arg, int good);

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
time_t RFC2616_Ttl(struct http *hp, time_t, time_t);
