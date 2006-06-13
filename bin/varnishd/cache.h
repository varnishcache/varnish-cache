/*
 * $Id$
 */

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
	void			*priv;
	struct stevedore	*stevedore;
};

#include "_stevedore.h"

/*
 * XXX: in the longer term, we want to support multiple stevedores,
 * XXX: selected by some kind of heuristics based on size, lifetime
 * XXX: etc etc.  For now we support only one.
 */
extern struct stevedore *stevedore;

/* Prototypes etc ----------------------------------------------------*/


/* cache_acceptor.c */
void vca_write(struct sess *sp, void *ptr, size_t len);
void vca_flush(struct sess *sp);
void *vca_main(void *arg);
void vca_retire_session(struct sess *sp);
void vca_recycle_session(struct sess *sp);

/* cache_backend.c */
void VBE_Init(void);
int VBE_GetFd(struct backend *bp, void **ptr);
void VBE_Pass(struct sess *sp);
void VBE_ClosedFd(void *ptr);
void VBE_RecycleFd(void *ptr);

/* cache_fetch.c */
int FetchSession(struct worker *w, struct sess *sp);

/* cache_http.c */
typedef void http_callback_f(void *, int good);
struct http;
struct http *http_New(void);
void http_Delete(struct http *hp);
int http_GetHdr(struct http *hp, const char *hdr, char **ptr);
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
#ifdef CLI_PRIV_H
cli_func_t	cli_func_config_list;
cli_func_t	cli_func_config_load;
cli_func_t	cli_func_config_unload;
cli_func_t	cli_func_config_use;
#endif
