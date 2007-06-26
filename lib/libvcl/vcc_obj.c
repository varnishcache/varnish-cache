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
	    NULL,
	    "VRT_l_backend_host(backend, ",
	    V_WO,
	    
	},
	{ "backend.port", PORTNAME, 12,
	    NULL,
	    "VRT_l_backend_port(backend, ",
	    V_WO,
	    
	},
	{ "backend.dnsttl", TIME, 14,
	    NULL,
	    "VRT_l_backend_dnsttl(backend, ",
	    V_WO,
	    
	},
	{ NULL }
};

struct var vcc_vars[] = {
	{ "client.ip", IP, 9,
	    "VRT_r_client_ip(sp)",
	    NULL,
	    V_RO,
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH
	},
	{ "server.ip", IP, 9,
	    "VRT_r_server_ip(sp)",
	    NULL,
	    V_RO,
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH
	},
	{ "req.request", STRING, 11,
	    "VRT_r_req_request(sp)",
	    NULL,
	    V_RO,
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH
	},
	{ "req.url", STRING, 7,
	    "VRT_r_req_url(sp)",
	    NULL,
	    V_RO,
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH
	},
	{ "req.proto", STRING, 9,
	    "VRT_r_req_proto(sp)",
	    NULL,
	    V_RO,
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH
	},
	{ "req.backend", BACKEND, 11,
	    "VRT_r_req_backend(sp)",
	    "VRT_l_req_backend(sp, ",
	    V_RW,
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH
	},
	{ "req.http.", HEADER, 9,
	    "VRT_r_req_http_(sp)",
	    NULL,
	    V_RO,
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH
	},
	{ "req.hash", HASH, 8,
	    NULL,
	    "VRT_l_req_hash(sp, ",
	    V_WO,
	    VCL_MET_HASH
	},
	{ "obj.valid", BOOL, 9,
	    "VRT_r_obj_valid(sp)",
	    "VRT_l_obj_valid(sp, ",
	    V_RW,
	    VCL_MET_HIT | VCL_MET_FETCH | VCL_MET_DISCARD | VCL_MET_TIMEOUT
	},
	{ "obj.cacheable", BOOL, 13,
	    "VRT_r_obj_cacheable(sp)",
	    "VRT_l_obj_cacheable(sp, ",
	    V_RW,
	    VCL_MET_HIT | VCL_MET_FETCH | VCL_MET_DISCARD | VCL_MET_TIMEOUT
	},
	{ "obj.ttl", TIME, 7,
	    "VRT_r_obj_ttl(sp)",
	    "VRT_l_obj_ttl(sp, ",
	    V_RW,
	    VCL_MET_HIT | VCL_MET_FETCH | VCL_MET_DISCARD | VCL_MET_TIMEOUT
	},
	{ "resp.proto", STRING, 10,
	    "VRT_r_resp_proto(sp)",
	    NULL,
	    V_RO,
	    VCL_MET_FETCH
	},
	{ "resp.status", INT, 11,
	    "VRT_r_resp_status(sp)",
	    NULL,
	    V_RO,
	    VCL_MET_FETCH
	},
	{ "resp.response", STRING, 13,
	    "VRT_r_resp_response(sp)",
	    NULL,
	    V_RO,
	    VCL_MET_FETCH
	},
	{ "resp.http.", HEADER, 10,
	    "VRT_r_resp_http_(sp)",
	    NULL,
	    V_RO,
	    VCL_MET_FETCH
	},
	{ NULL }
};
