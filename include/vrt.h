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
void VRT_no_cache(struct sess *);
void VRT_no_new_cache(struct sess *);
#if 0
int ip_match(unsigned, struct vcl_acl *);
int string_match(const char *, const char *);
#endif
int VRT_rewrite(const char *, const char *);
void VRT_error(struct sess *, unsigned, const char *);
int VRT_switch_config(const char *);

char *VRT_GetHdr(struct sess *, const char *);
char *VRT_GetReq(struct sess *);
void VRT_handling(struct sess *sp, unsigned hand);
int VRT_obj_valid(struct sess *);
int VRT_obj_cacheable(struct sess *);

void VRT_set_backend_hostname(struct backend *, const char *);
void VRT_set_backend_portname(struct backend *, const char *);

void VRT_alloc_backends(struct VCL_conf *cp);

#define VRT_done(sp, hand)			\
	do {					\
		VRT_handling(sp, hand);		\
		return (1);			\
	} while (0)
