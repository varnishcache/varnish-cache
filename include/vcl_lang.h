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

struct vcl_ref {
	unsigned	line;
	unsigned	pos;
	unsigned	count;
	const char	*token;
};

struct vcl_acl {
	unsigned	ip;
	unsigned	mask;
};

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

	char			done;

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

#define VCL_FARGS	struct sess *sess
#define VCL_PASS_ARGS	sess

void VCL_count(unsigned);
void VCL_no_cache(VCL_FARGS);
void VCL_no_new_cache(VCL_FARGS);
int ip_match(unsigned, struct vcl_acl *);
int string_match(const char *, const char *);
int VCL_rewrite(const char *, const char *);
void VCL_error(VCL_FARGS, unsigned, const char *);
int VCL_switch_config(const char *);

char *VCL_GetHdr(VCL_FARGS, const char *);

typedef void vcl_init_f(void);
typedef void vcl_func_f(VCL_FARGS);

struct VCL_conf {
	unsigned	magic;
#define VCL_CONF_MAGIC	0x7406c509	/* from /dev/random */
	vcl_init_f	*init_func;
	vcl_func_f	*recv_func;
	vcl_func_f	*hit_func;
	vcl_func_f	*miss_func;
	vcl_func_f	*fetch_func;
	struct backend	*default_backend;
	struct vcl_ref	*ref;
	unsigned	nref;
	unsigned	busy;
};

#define VCL_done(sess, hand)			\
	do {					\
		sess->handling = hand;		\
		sess->done = 1;			\
		return;				\
	} while (0)
