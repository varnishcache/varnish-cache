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
	unsigned char	not;
	unsigned char	paren;
	unsigned char	mask;
	const char	*name;
	void		*priv;
};

/* ACL related */
int VRT_acl_match(struct sess *, struct vrt_acl *);
void VRT_acl_init(struct vrt_acl *);
void VRT_acl_fini(struct vrt_acl *);

void VRT_count(struct sess *, unsigned);
int VRT_rewrite(const char *, const char *);
void VRT_error(struct sess *, unsigned, const char *);
int VRT_switch_config(const char *);

char *VRT_GetHdr(struct sess *, const char *);
void VRT_handling(struct sess *sp, unsigned hand);

/* Backend related */
void VRT_set_backend_name(struct backend *, const char *);
void VRT_alloc_backends(struct VCL_conf *cp);
void VRT_free_backends(struct VCL_conf *cp);
void VRT_fini_backend(struct backend *be);


#define VRT_done(sp, hand)			\
	do {					\
		VRT_handling(sp, hand);		\
		return (1);			\
	} while (0)
