/*
 * $Id$
 */

#define VCA_RXBUFSIZE		1024
struct sess {
	int		fd;
	char		rcv[VCA_RXBUFSIZE + 1];
	unsigned	rcv_len;
	struct event	rd_e;
};

/* cache_acceptor.c */
void *vca_main(void *arg);

/* cache_shmlog.c */
void VSL_Init(void);
#ifdef SHMLOGHEAD_MAGIC
void VSL(enum shmlogtag tag, unsigned id, const char *fmt, ...);
#endif


