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

#define VCA_RXBUFSIZE		1024
#define VCA_ADDRBUFSIZE		32
#define VCA_UNKNOWNHDR		10

struct httphdr {
	const char		*req;
	const char		*url;
	const char		*proto;
	const char		*status;
	const char		*response;
#define HTTPH(a, b, c, d, e, f, g) const char *b;
#include <http_headers.h>
#undef HTTPH
	const char		*uhdr[VCA_UNKNOWNHDR];
	unsigned		nuhdr;
};

struct object {	
	unsigned char		hash[16];
	unsigned 		refcnt;
	unsigned		valid;
	unsigned		cacheable;

	unsigned		busy;
	unsigned		len;

	TAILQ_HEAD(, storage)	store;
};

struct sess {
	int			fd;

	/* formatted ascii client address */
	char			addr[VCA_ADDRBUFSIZE];

	/* Receive buffer for HTTP header */
	char			rcv[VCA_RXBUFSIZE + 1];
	unsigned		rcv_len;
	unsigned		rcv_ptr;

	/* HTTP request info, points into rcv */
	struct httphdr		http;

	enum {
		HND_Unclass,
		HND_Deliver,
		HND_Pass,
		HND_Pipe,
		HND_Lookup,
		HND_Fetch
	}			handling;

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
void VCL_pass(VCL_FARGS);
void VCL_fetch(VCL_FARGS);
void VCL_insert(VCL_FARGS);
int VCL_switch_config(const char *);

typedef void vcl_init_f(void);
typedef void vcl_func_f(VCL_FARGS);

struct VCL_conf {
	unsigned	magic;
#define VCL_CONF_MAGIC	0x7406c509	/* from /dev/random */
	vcl_init_f	*init_func;
	vcl_func_f	*recv_func;
	vcl_func_f	*lookup_func;
	vcl_func_f	*fetch_func;
	struct backend	*default_backend;
	struct vcl_ref	*ref;
	unsigned	nref;
	unsigned	busy;
};
