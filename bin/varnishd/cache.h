/*
 * $Id$
 */

/* cache_acceptor.c */
void *vca_main(void *arg);

/* cache_backend.c */
void VBE_Init(void);

/* cache_httpd.c */
void HttpdAnalyze(struct sess *sp);

/* cache_main.c */
pthread_mutex_t	sessmtx;

/* cache_pool.c */
void CacheInitPool(void);
void DealWithSession(struct sess *sp);

/* cache_shmlog.c */
void VSL_Init(void);
#ifdef SHMLOGHEAD_MAGIC
void VSLR(enum shmlogtag tag, unsigned id, const char *b, const char *e);
void VSL(enum shmlogtag tag, unsigned id, const char *fmt, ...);
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
