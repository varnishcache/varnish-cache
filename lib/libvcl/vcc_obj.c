/*
 * $Id$
 *
 * NB:  This file is machine generated, DO NOT EDIT!
 *
 * Edit vcc_gen_obj.tcl instead
 */

#include "config.h"
#include <stdio.h>
#include "vcc_compile.h"

struct var vcc_vars[] = {
	{ "client.ip", IP, 9,
	    "VRT_r_client_ip(sp)",	    NULL,
	    V_RO,	    0,
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH
	     | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH | VCL_MET_DELIVER
	     | VCL_MET_ERROR
	},
	{ "server.ip", IP, 9,
	    "VRT_r_server_ip(sp)",	    NULL,
	    V_RO,	    0,
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH
	     | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH | VCL_MET_DELIVER
	     | VCL_MET_ERROR
	},
	{ "server.port", INT, 11,
	    "VRT_r_server_port(sp)",	    NULL,
	    V_RO,	    0,
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH
	     | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH | VCL_MET_DELIVER
	     | VCL_MET_ERROR
	},
	{ "req.request", STRING, 11,
	    "VRT_r_req_request(sp)",	    "VRT_l_req_request(sp, ",
	    V_RW,	    0,
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH
	     | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH | VCL_MET_ERROR
	},
	{ "req.url", STRING, 7,
	    "VRT_r_req_url(sp)",	    "VRT_l_req_url(sp, ",
	    V_RW,	    0,
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH
	     | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH | VCL_MET_ERROR
	},
	{ "req.proto", STRING, 9,
	    "VRT_r_req_proto(sp)",	    "VRT_l_req_proto(sp, ",
	    V_RW,	    0,
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH
	     | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH | VCL_MET_ERROR
	},
	{ "req.http.", HEADER, 9,
	    "VRT_r_req_http_(sp)",	    "VRT_l_req_http_(sp, ",
	    V_RW,	    "HDR_REQ",
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH
	     | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH | VCL_MET_ERROR
	},
	{ "req.hash", HASH, 8,
	    NULL,	    "VRT_l_req_hash(sp, ",
	    V_WO,	    0,
	    VCL_MET_HASH | VCL_MET_ERROR
	},
	{ "req.backend", BACKEND, 11,
	    "VRT_r_req_backend(sp)",	    "VRT_l_req_backend(sp, ",
	    V_RW,	    0,
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH
	     | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH | VCL_MET_ERROR
	},
	{ "req.restarts", INT, 12,
	    "VRT_r_req_restarts(sp)",	    NULL,
	    V_RO,	    0,
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH
	     | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH | VCL_MET_DELIVER
	     | VCL_MET_ERROR
	},
	{ "req.grace", TIME, 9,
	    "VRT_r_req_grace(sp)",	    "VRT_l_req_grace(sp, ",
	    V_RW,	    0,
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH
	     | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH | VCL_MET_DELIVER
	     | VCL_MET_ERROR
	},
	{ "req.xid", STRING, 7,
	    "VRT_r_req_xid(sp)",	    NULL,
	    V_RO,	    0,
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH
	     | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH | VCL_MET_DELIVER
	     | VCL_MET_ERROR
	},
	{ "bereq.request", STRING, 13,
	    "VRT_r_bereq_request(sp)",	    "VRT_l_bereq_request(sp, ",
	    V_RW,	    0,
	    VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_MISS | VCL_MET_FETCH
	},
	{ "bereq.url", STRING, 9,
	    "VRT_r_bereq_url(sp)",	    "VRT_l_bereq_url(sp, ",
	    V_RW,	    0,
	    VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_MISS | VCL_MET_FETCH
	},
	{ "bereq.proto", STRING, 11,
	    "VRT_r_bereq_proto(sp)",	    "VRT_l_bereq_proto(sp, ",
	    V_RW,	    0,
	    VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_MISS | VCL_MET_FETCH
	},
	{ "bereq.http.", HEADER, 11,
	    "VRT_r_bereq_http_(sp)",	    "VRT_l_bereq_http_(sp, ",
	    V_RW,	    "HDR_BEREQ",
	    VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_MISS | VCL_MET_FETCH
	},
	{ "bereq.connect_timeout", TIME, 21,
	    "VRT_r_bereq_connect_timeout(sp)",	    "VRT_l_bereq_connect_timeout(sp, ",
	    V_RW,	    0,
	    VCL_MET_PASS | VCL_MET_MISS
	},
	{ "bereq.first_byte_timeout", TIME, 24,
	    "VRT_r_bereq_first_byte_timeout(sp)",	    "VRT_l_bereq_first_byte_timeout(sp, ",
	    V_RW,	    0,
	    VCL_MET_PASS | VCL_MET_MISS
	},
	{ "bereq.between_bytes_timeout", TIME, 27,
	    "VRT_r_bereq_between_bytes_timeout(sp)",	    "VRT_l_bereq_between_bytes_timeout(sp, ",
	    V_RW,	    0,
	    VCL_MET_PASS | VCL_MET_MISS
	},
	{ "beresp.request", STRING, 14,
	    "VRT_r_beresp_request(sp)",	    "VRT_l_beresp_request(sp, ",
	    V_RW,	    0,
	    VCL_MET_FETCH
	},
	{ "beresp.url", STRING, 10,
	    "VRT_r_beresp_url(sp)",	    "VRT_l_beresp_url(sp, ",
	    V_RW,	    0,
	    VCL_MET_FETCH
	},
	{ "beresp.proto", STRING, 12,
	    "VRT_r_beresp_proto(sp)",	    "VRT_l_beresp_proto(sp, ",
	    V_RW,	    0,
	    VCL_MET_FETCH
	},
	{ "beresp.http.", HEADER, 12,
	    "VRT_r_beresp_http_(sp)",	    "VRT_l_beresp_http_(sp, ",
	    V_RW,	    "HDR_BEREQ",
	    VCL_MET_FETCH
	},
	{ "obj.proto", STRING, 9,
	    "VRT_r_obj_proto(sp)",	    "VRT_l_obj_proto(sp, ",
	    V_RW,	    0,
	    VCL_MET_HIT | VCL_MET_FETCH | VCL_MET_ERROR
	},
	{ "obj.status", INT, 10,
	    "VRT_r_obj_status(sp)",	    "VRT_l_obj_status(sp, ",
	    V_RW,	    0,
	    VCL_MET_FETCH | VCL_MET_ERROR
	},
	{ "obj.response", STRING, 12,
	    "VRT_r_obj_response(sp)",	    "VRT_l_obj_response(sp, ",
	    V_RW,	    0,
	    VCL_MET_FETCH | VCL_MET_ERROR
	},
	{ "obj.hits", INT, 8,
	    "VRT_r_obj_hits(sp)",	    NULL,
	    V_RO,	    0,
	    VCL_MET_HIT | VCL_MET_FETCH | VCL_MET_DELIVER
	},
	{ "obj.http.", HEADER, 9,
	    "VRT_r_obj_http_(sp)",	    "VRT_l_obj_http_(sp, ",
	    V_RW,	    "HDR_OBJ",
	    VCL_MET_HIT | VCL_MET_FETCH | VCL_MET_ERROR
	},
	{ "obj.cacheable", BOOL, 13,
	    "VRT_r_obj_cacheable(sp)",	    "VRT_l_obj_cacheable(sp, ",
	    V_RW,	    0,
	    VCL_MET_HIT | VCL_MET_FETCH | VCL_MET_DISCARD | VCL_MET_TIMEOUT
	     | VCL_MET_ERROR
	},
	{ "obj.ttl", TIME, 7,
	    "VRT_r_obj_ttl(sp)",	    "VRT_l_obj_ttl(sp, ",
	    V_RW,	    0,
	    VCL_MET_HIT | VCL_MET_FETCH | VCL_MET_DISCARD | VCL_MET_TIMEOUT
	     | VCL_MET_ERROR
	},
	{ "obj.grace", TIME, 9,
	    "VRT_r_obj_grace(sp)",	    "VRT_l_obj_grace(sp, ",
	    V_RW,	    0,
	    VCL_MET_HIT | VCL_MET_FETCH | VCL_MET_DISCARD | VCL_MET_TIMEOUT
	     | VCL_MET_ERROR
	},
	{ "obj.lastuse", TIME, 11,
	    "VRT_r_obj_lastuse(sp)",	    NULL,
	    V_RO,	    0,
	    VCL_MET_HIT | VCL_MET_FETCH | VCL_MET_DELIVER | VCL_MET_DISCARD
	     | VCL_MET_TIMEOUT | VCL_MET_ERROR
	},
	{ "obj.hash", STRING, 8,
	    "VRT_r_obj_hash(sp)",	    NULL,
	    V_RO,	    0,
	    VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH | VCL_MET_DELIVER
	     | VCL_MET_ERROR
	},
	{ "resp.proto", STRING, 10,
	    "VRT_r_resp_proto(sp)",	    "VRT_l_resp_proto(sp, ",
	    V_RW,	    0,
	    VCL_MET_DELIVER
	},
	{ "resp.status", INT, 11,
	    "VRT_r_resp_status(sp)",	    "VRT_l_resp_status(sp, ",
	    V_RW,	    0,
	    VCL_MET_DELIVER
	},
	{ "resp.response", STRING, 13,
	    "VRT_r_resp_response(sp)",	    "VRT_l_resp_response(sp, ",
	    V_RW,	    0,
	    VCL_MET_DELIVER
	},
	{ "resp.http.", HEADER, 10,
	    "VRT_r_resp_http_(sp)",	    "VRT_l_resp_http_(sp, ",
	    V_RW,	    "HDR_RESP",
	    VCL_MET_DELIVER
	},
	{ "now", TIME, 3,
	    "VRT_r_now(sp)",	    NULL,
	    V_RO,	    0,
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH
	     | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH | VCL_MET_DELIVER
	     | VCL_MET_DISCARD | VCL_MET_TIMEOUT
	},
	{ "req.backend.healthy", BOOL, 19,
	    "VRT_r_req_backend_healthy(sp)",	    NULL,
	    V_RO,	    0,
	    VCL_MET_RECV | VCL_MET_PIPE | VCL_MET_PASS | VCL_MET_HASH
	     | VCL_MET_MISS | VCL_MET_HIT | VCL_MET_FETCH | VCL_MET_DELIVER
	     | VCL_MET_DISCARD | VCL_MET_TIMEOUT
	},
	{ NULL }
};
