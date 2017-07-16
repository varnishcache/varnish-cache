#define MAX_HDR		50

struct http {
	unsigned		magic;
#define HTTP_MAGIC		0x2f02169c
	int			fd;
	int			*sfd;
	int			timeout;
	struct vtclog		*vl;

	struct vsb		*vsb;

	int			nrxbuf;
	char			*rxbuf;
	char			*rem_ip;
	char			*rem_port;
	char			*rem_path;
	int			prxbuf;
	char			*body;
	unsigned		bodyl;
	char			bodylen[20];
	char			chunklen[20];

	char			*req[MAX_HDR];
	char			*resp[MAX_HDR];

	int			gziplevel;
	int			gzipresidual;

	int			fatal;

	/* H/2 */
	unsigned		h2;
	int			wf;

	pthread_t		tp;
	VTAILQ_HEAD(, stream)   streams;
	pthread_mutex_t		mtx;
	pthread_cond_t          cond;
	struct hpk_ctx		*encctx;
	struct hpk_ctx		*decctx;
	uint64_t		iws;
	int64_t			ws;
};


