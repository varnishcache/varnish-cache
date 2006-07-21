/*
 * $Id$
 *
 * NB:  This file is machine generated, DO NOT EDIT!
 *
 * Edit vcc_gen_obj.tcl instead
 */

#include <stdio.h>
#include "vcc_compile.h"

struct var vcc_be_vars[] = {
	{ "backend.host", HOSTNAME, 12,
	    "VRT_r_backend_host(backend)",
	    "VRT_l_backend_host(backend, ",
	},
	{ "backend.port", PORTNAME, 12,
	    "VRT_r_backend_port(backend)",
	    "VRT_l_backend_port(backend, ",
	},
	{ NULL }
};

struct var vcc_vars[] = {
	{ "req.request", STRING, 11,
	    "VRT_r_req_request(sp)",
	    "VRT_l_req_request(sp, ",
	},
	{ "req.url", STRING, 7,
	    "VRT_r_req_url(sp)",
	    "VRT_l_req_url(sp, ",
	},
	{ "obj.valid", BOOL, 9,
	    "VRT_r_obj_valid(sp)",
	    "VRT_l_obj_valid(sp, ",
	},
	{ "obj.cacheable", BOOL, 13,
	    "VRT_r_obj_cacheable(sp)",
	    "VRT_l_obj_cacheable(sp, ",
	},
	{ "obj.backend", BACKEND, 11,
	    "VRT_r_obj_backend(sp)",
	    "VRT_l_obj_backend(sp, ",
	},
	{ "obj.ttl", TIME, 7,
	    "VRT_r_obj_ttl(sp)",
	    "VRT_l_obj_ttl(sp, ",
	},
	{ "req.http.", HEADER, 9,
	    "VRT_r_req_http_(sp)",
	    "VRT_l_req_http_(sp, ",
	},
	{ NULL }
};

const char *vrt_obj_h = 
	"/*\n"
	" * $Id$\n"
	" *\n"
	" * NB:  This file is machine generated, DO NOT EDIT!\n"
	" *\n"
	" * Edit vcc_gen_obj.tcl instead\n"
	" */\n"
	"\n"
	"const char * VRT_r_backend_host(struct backend *);\n"
	"void VRT_l_backend_host(struct backend *, const char *);\n"
	"const char * VRT_r_backend_port(struct backend *);\n"
	"void VRT_l_backend_port(struct backend *, const char *);\n"
	"const char * VRT_r_req_request(struct sess *);\n"
	"void VRT_l_req_request(struct sess *, const char *);\n"
	"const char * VRT_r_req_url(struct sess *);\n"
	"void VRT_l_req_url(struct sess *, const char *);\n"
	"double VRT_r_obj_valid(struct sess *);\n"
	"void VRT_l_obj_valid(struct sess *, double);\n"
	"double VRT_r_obj_cacheable(struct sess *);\n"
	"void VRT_l_obj_cacheable(struct sess *, double);\n"
	"struct backend * VRT_r_obj_backend(struct sess *);\n"
	"void VRT_l_obj_backend(struct sess *, struct backend *);\n"
	"double VRT_r_obj_ttl(struct sess *);\n"
	"void VRT_l_obj_ttl(struct sess *, double);\n"
	"const char * VRT_r_req_http_(struct sess *);\n"
	"void VRT_l_req_http_(struct sess *, const char *);\n"
;
