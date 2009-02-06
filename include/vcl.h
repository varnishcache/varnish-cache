/*
 * $Id$
 *
 * NB:  This file is machine generated, DO NOT EDIT!
 *
 * Edit and run vcc_gen_fixed_token.tcl instead
 */

struct sess;
struct cli;

typedef void vcl_init_f(struct cli *);
typedef void vcl_fini_f(struct cli *);
typedef int vcl_func_f(struct sess *sp);

/* VCL Methods */
#define VCL_MET_RECV		(1 << 0)
#define VCL_MET_PIPE		(1 << 1)
#define VCL_MET_PASS		(1 << 2)
#define VCL_MET_HASH		(1 << 3)
#define VCL_MET_MISS		(1 << 4)
#define VCL_MET_HIT		(1 << 5)
#define VCL_MET_FETCH		(1 << 6)
#define VCL_MET_DELIVER		(1 << 7)
#define VCL_MET_PREFETCH	(1 << 8)
#define VCL_MET_TIMEOUT		(1 << 9)
#define VCL_MET_DISCARD		(1 << 10)
#define VCL_MET_ERROR		(1 << 11)

#define VCL_MET_MAX		12

/* VCL Returns */
#define VCL_RET_ERROR		(1 << 0)
#define VCL_RET_LOOKUP		(1 << 1)
#define VCL_RET_HASH		(1 << 2)
#define VCL_RET_PIPE		(1 << 3)
#define VCL_RET_PASS		(1 << 4)
#define VCL_RET_FETCH		(1 << 5)
#define VCL_RET_DELIVER		(1 << 6)
#define VCL_RET_DISCARD		(1 << 7)
#define VCL_RET_KEEP		(1 << 8)
#define VCL_RET_RESTART		(1 << 9)

#define VCL_RET_MAX		10

struct VCL_conf {
	unsigned	magic;
#define VCL_CONF_MAGIC	0x7406c509	/* from /dev/random */

	struct director	**director;
	unsigned	ndirector;
	struct vrt_ref	*ref;
	unsigned	nref;
	unsigned	busy;
	unsigned	discard;

	unsigned	nsrc;
	const char	**srcname;
	const char	**srcbody;

	unsigned	nhashcount;

	vcl_init_f	*init_func;
	vcl_fini_f	*fini_func;

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
