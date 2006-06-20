/*
 * $Id$
 *
 * Interface to a compiled VCL program.
 *
 * XXX: When this file is changed, lib/libvcl/vcl_gen_fixed_token.tcl
 * XXX: *MUST* be rerun.
 */

struct sess;

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
	struct backend	**backend;
	unsigned	nbackend;
	struct vrt_ref	*ref;
	unsigned	nref;
	unsigned	busy;
};
