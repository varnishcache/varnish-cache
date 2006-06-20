/*
 * Stuff necessary to compile a VCL programs C code
 *
 * XXX: When this file is changed, lib/libvcl/vcl_gen_fixed_token.tcl
 * XXX: *MUST* be rerun.
 */

/* XXX: This include is bad.  The VCL compiler shouldn't know about it. */
#include <sys/queue.h>

struct sess;
typedef void sesscb_f(struct sess *sp);

#define VCA_ADDRBUFSIZE		32

struct object {	
	unsigned char		hash[16];
	unsigned 		refcnt;
	unsigned		valid;
	unsigned		cacheable;

	unsigned		busy;
	unsigned		len;

	char			*header;

	TAILQ_HEAD(, storage)	store;
};
enum handling {
	HND_Error	= (1 << 0),
	HND_Pipe	= (1 << 1),
	HND_Pass	= (1 << 2),
	HND_Lookup	= (1 << 3),
	HND_Fetch	= (1 << 4),
	HND_Insert	= (1 << 5),
	HND_Deliver	= (1 << 6),
};

struct sess {
	int			fd;

	/* formatted ascii client address */
	char			addr[VCA_ADDRBUFSIZE];

	/* HTTP request */
	struct http		*http;

	enum handling		handling;

	TAILQ_ENTRY(sess)	list;

	sesscb_f		*sesscb;

	struct backend		*backend;
	struct object		*obj;
	struct VCL_conf		*vcl;

	/* Various internal stuff */
	struct event		*rd_e;
	struct sessmem		*mem;
};

struct backend {
	const char	*hostname;
	const char	*portname;
	struct addrinfo	*addr;
	unsigned	ip;
	double		responsetime;
	double		timeout;
	double		bandwidth;
	int		down;

	/* internal stuff */
	struct vbe	*vbe;
};

#if 0
int ip_match(unsigned, struct vcl_acl *);
int string_match(const char *, const char *);
#endif

typedef void vcl_init_f(void);
typedef int vcl_func_f(struct sess *sp);

struct VCL_conf {
	unsigned	magic;
#define VCL_CONF_MAGIC	0x7406c509	/* from /dev/random */
	vcl_init_f	*init_func;
	vcl_func_f	*recv_func;
	vcl_func_f	*hit_func;
	vcl_func_f	*miss_func;
	vcl_func_f	*fetch_func;
	struct backend	*default_backend;
	struct vrt_ref	*ref;
	unsigned	nref;
	unsigned	busy;
};
