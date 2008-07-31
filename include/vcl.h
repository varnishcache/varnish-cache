/*
 * $Id$
 *
 * NB:  This file is machine generated, DO NOT EDIT!
 *
 * Edit vcc_gen_fixed_token.tcl instead
 */

struct sess;
struct cli;

typedef void vcl_init_f(struct cli *);
typedef void vcl_fini_f(struct cli *);
typedef int vcl_func_f(struct sess *sp);

struct VCL_conf {
	unsigned        magic;
#define VCL_CONF_MAGIC  0x7406c509      /* from /dev/random */

        struct director  **director;
        unsigned        ndirector;
        struct vrt_ref  *ref;
        unsigned        nref;
        unsigned        busy;
        unsigned        discard;
        
	unsigned	nsrc;
	const char	**srcname;
	const char	**srcbody;

	unsigned	nhashcount;

        vcl_init_f      *init_func;
        vcl_fini_f      *fini_func;

	vcl_func_f	*recv_func;
	vcl_func_f	*pipe_func;
	vcl_func_f	*pass_func;
	vcl_func_f	*hash_func;
	vcl_func_f	*miss_func;
	vcl_func_f	*hit_func;
	vcl_func_f	*fetch_func;
	vcl_func_f	*deliver_func;
	vcl_func_f	*prefetch_func;
	vcl_func_f	*timeout_func;
	vcl_func_f	*discard_func;
	vcl_func_f	*error_func;
};
