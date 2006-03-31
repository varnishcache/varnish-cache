/*
 * Stuff necessary to compile a VCL programs C code
 *
 * XXX: When this file is changed, lib/libvcl/vcl_gen_fixed_token.tcl
 * XXX: *MUST* be rerun.
 */

/* XXX: This include is bad.  The VCL compiler shouldn't know about it. */
#include <sys/queue.h>

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

struct sess {
	int			fd;

	/* formatted ascii client address */
	char			addr[VCA_ADDRBUFSIZE];

	/* Receive buffer for HTTP header */
	char			rcv[VCA_RXBUFSIZE + 1];
	unsigned		rcv_len;

	/* HTTP request info, points into rcv */
	const char		*req_b;
	const char		*url_b;
	const char		*proto_b;
#define HTTPH(a, b) const char *b;
#include <http_headers.h>
#undef HTTPH

	enum {
		HND_Unclass,
		HND_Handle,
		HND_Pass
	}			handling;

	char			done;

	TAILQ_ENTRY(sess)	list;

	struct backend		*backend;
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
void VCL_no_cache();
void VCL_no_new_cache();
int ip_match(unsigned, struct vcl_acl *);
int string_match(const char *, const char *);
int VCL_rewrite(const char *, const char *);
int VCL_error(unsigned, const char *);
void VCL_pass(VCL_FARGS);
int VCL_fetch(void);
int VCL_switch_config(const char *);

typedef void vcl_init_f(void);
typedef void vcl_func_f(VCL_FARGS);

struct VCL_conf {
	unsigned	magic;
#define VCL_CONF_MAGIC	0x7406c509	/* from /dev/random */
	vcl_init_f	*init_func;
	vcl_func_f	*main_func;
	struct backend	*default_backend;
	struct vcl_ref	*ref;
	unsigned	nref;
	unsigned	busy;
};
