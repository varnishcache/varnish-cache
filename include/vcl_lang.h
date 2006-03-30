/*
 * Stuff necessary to compile a VCL programs C code
 *
 * XXX: When this file is changed, lib/libvcl/vcl_gen_fixed_token.tcl
 * XXX: *MUST* be rerun.
 */

#include <sys/queue.h>
#include <pthread.h>

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
	const char		*req_e;
	const char		*url_b;
	const char		*url_e;
	const char		*proto_b;
	const char		*proto_e;
	const char		*hdr_b;
	const char		*hdr_e;

	enum {
		HND_Unclass,
		HND_Handle,
		HND_Pass
	}			handling;

	char			done;

	TAILQ_ENTRY(sess)	list;

	struct VCL_conf		*vcl;

	/* Various internal stuff */
	struct event		*rd_e;
	struct sessmem		*mem;
};

struct be_conn {
	TAILQ_ENTRY(be_conn)	list;
	int			fd;
};

struct backend {
	unsigned	ip;
	double		responsetime;
	double		timeout;
	double		bandwidth;
	int		down;

	/* Internals */
	TAILQ_HEAD(,be_conn)	bec_head;
	unsigned		nbec;
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
