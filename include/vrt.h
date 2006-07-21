/*
 * $Id$ 
 *
 * Runtime support for compiled VCL programs.
 *
 * XXX: When this file is changed, lib/libvcl/vcl_gen_fixed_token.tcl
 * XXX: *MUST* be rerun.
 */

struct sess;
struct backend;
struct VCL_conf;

struct vrt_ref {
	unsigned	line;
	unsigned	pos;
	unsigned	count;
	const char	*token;
};

struct vrt_acl {
	unsigned	ip;
	unsigned	mask;
};

void VRT_count(struct sess *, unsigned);
#if 0
int ip_match(unsigned, struct vcl_acl *);
int string_match(const char *, const char *);
#endif
int VRT_rewrite(const char *, const char *);
void VRT_error(struct sess *, unsigned, const char *);
int VRT_switch_config(const char *);

char *VRT_GetHdr(struct sess *, const char *);
void VRT_handling(struct sess *sp, unsigned hand);

void VRT_set_backend_name(struct backend *, const char *);

void VRT_alloc_backends(struct VCL_conf *cp);

#define VRT_done(sp, hand)			\
	do {					\
		VRT_handling(sp, hand);		\
		return (1);			\
	} while (0)
