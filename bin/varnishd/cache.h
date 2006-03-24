/*
 * $Id$
 */

#define VCA_RXBUFSIZE		1024
#define VCA_ADDRBUFSIZE		32

struct sess {
	int		fd;

	/* formatted ascii client address */
	char		addr[VCA_ADDRBUFSIZE];

	/* Receive buffer for HTTP header */
	char		rcv[VCA_RXBUFSIZE + 1];
	unsigned	rcv_len;

	/* HTTP request info, points into rcv */
	const char	*req_b;
	const char	*req_e;
	const char	*url_b;
	const char	*url_e;
	const char	*proto_b;
	const char	*proto_e;
	const char	*hdr_b;
	const char	*hdr_e;

	enum {
		HND_Unclass,
		HND_Handle,
		HND_Pass
	}		handling;

	/* Various internal stuff */
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


