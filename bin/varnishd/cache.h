/*
 * $Id$
 */

#define VCA_RXBUFSIZE		1024
#define VCA_ADDRBUFSIZE		32
struct sess {
	int		fd;
	char		rcv[VCA_RXBUFSIZE + 1];
	char		addr[VCA_ADDRBUFSIZE];
	unsigned	rcv_len;
	struct event	*rd_e;
	struct sessmem	*mem;
};

/* cache_acceptor.c */
void *vca_main(void *arg);

/* cache_httpd.c */
void HttpdAnalyze(struct sess *sp);

/* cache_shmlog.c */
void VSL_Init(void);
#ifdef SHMLOGHEAD_MAGIC
void VSLR(enum shmlogtag tag, unsigned id, const char *b, const char *e);
void VSL(enum shmlogtag tag, unsigned id, const char *fmt, ...);
#endif


