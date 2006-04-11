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
};
#else
struct worker;
#endif

/* Hashing -----------------------------------------------------------*/
struct object {	/* XXX: this goes elsewhere in due time */
	unsigned char		hash[16];
	unsigned 		refcnt;
};

typedef void hash_init_f(void);
typedef struct object *hash_lookup_f(unsigned char *key, struct object *nobj);
typedef void hash_purge_f(struct object *obj);

struct hash_slinger {
	const char		*name;
	hash_init_f		*init;
	hash_lookup_f		*lookup;
	hash_purge_f		*purge;
};

extern struct hash_slinger hsl_slinger;

/* Prototypes etc ----------------------------------------------------*/


/* cache_acceptor.c */
void *vca_main(void *arg);
void vca_retire_session(struct sess *sp);
void vca_recycle_session(struct sess *sp);

/* cache_backend.c */
void VBE_Init(void);
int VBE_GetFd(struct backend *bp, void **ptr);
void VBE_Pass(struct sess *sp);
void VBE_ClosedFd(void *ptr);
void VBE_RecycleFd(void *ptr);


/* cache_httpd.c */
void HttpdAnalyze(struct sess *sp, int rr);
void HttpdGetHead(struct sess *sp, struct event_base *eb, sesscb_f *func);
void HttpdBuildSbuf(int resp, int filter, struct sbuf *sb, struct sess *sp);

/* cache_main.c */
pthread_mutex_t	sessmtx;

/* cache_pass.c */
void PassSession(struct worker *w, struct sess *sp);

/* cache_pipe.c */
void PipeSession(struct worker *w, struct sess *sp);

/* cache_pool.c */
void CacheInitPool(void);
void DealWithSession(struct sess *sp);

/* cache_shmlog.c */
void VSL_Init(void);
#ifdef SHMLOGHEAD_MAGIC
void VSLR(enum shmlogtag tag, unsigned id, const char *b, const char *e);
void VSL(enum shmlogtag tag, unsigned id, const char *fmt, ...);
#define HERE() VSL(SLT_Debug, 0, "HERE: %s(%d)", __func__, __LINE__)
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
