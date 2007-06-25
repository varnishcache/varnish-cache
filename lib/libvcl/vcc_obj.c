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
	    0, 
	    
	},
	{ "backend.port", PORTNAME, 12,
	    "VRT_r_backend_port(backend)",
	    "VRT_l_backend_port(backend, ",
	    0, 
	    
	},
	{ "backend.dnsttl", TIME, 14,
	    "VRT_r_backend_dnsttl(backend)",
	    "VRT_l_backend_dnsttl(backend, ",
	    0, 
	    
	},
	{ NULL }
};

struct var vcc_vars[] = {
	{ "client.ip", IP, 9,
	    "VRT_r_client_ip(sp)",
	    "VRT_l_client_ip(sp, ",
	    1, 
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH
	},
	{ "server.ip", IP, 9,
	    "VRT_r_server_ip(sp)",
	    "VRT_l_server_ip(sp, ",
	    1, 
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH
	},
	{ "req.request", STRING, 11,
	    "VRT_r_req_request(sp)",
	    "VRT_l_req_request(sp, ",
	    1, 
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH
	},
	{ "req.host", STRING, 8,
	    "VRT_r_req_host(sp)",
	    "VRT_l_req_host(sp, ",
	    1, 
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH
	},
	{ "req.url", STRING, 7,
	    "VRT_r_req_url(sp)",
	    "VRT_l_req_url(sp, ",
	    1, 
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH
	},
	{ "req.proto", STRING, 9,
	    "VRT_r_req_proto(sp)",
	    "VRT_l_req_proto(sp, ",
	    1, 
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH
	},
	{ "req.backend", BACKEND, 11,
	    "VRT_r_req_backend(sp)",
	    "VRT_l_req_backend(sp, ",
	    0, 
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH
	},
	{ "req.http.", HEADER, 9,
	    "VRT_r_req_http_(sp)",
	    "VRT_l_req_http_(sp, ",
	    1, 
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH
	},
	{ "req.hash", HASH, 8,
	    "VRT_r_req_hash(sp)",
	    "VRT_l_req_hash(sp, ",
	    0, 
	    VCL_MET_HASH
	},
	{ "obj.valid", BOOL, 9,
	    "VRT_r_obj_valid(sp)",
	    "VRT_l_obj_valid(sp, ",
	    0, 
	    VCL_MET_HIT | VCL_MET_FETCH
	},
	{ "obj.cacheable", BOOL, 13,
	    "VRT_r_obj_cacheable(sp)",
	    "VRT_l_obj_cacheable(sp, ",
	    0, 
	    VCL_MET_HIT | VCL_MET_FETCH
	},
	{ "obj.ttl", TIME, 7,
	    "VRT_r_obj_ttl(sp)",
	    "VRT_l_obj_ttl(sp, ",
	    0, 
	    VCL_MET_HIT | VCL_MET_FETCH
	},
	{ "resp.http.", HEADER, 10,
	    "VRT_r_resp_http_(sp)",
	    "VRT_l_resp_http_(sp, ",
	    1, 
	    VCL_MET_FETCH
	},
	{ NULL }
};
