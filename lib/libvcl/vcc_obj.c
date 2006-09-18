/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006 Linpro AS
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Initial implementation by Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
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
	{ "client.ip", IP, 9,
	    "VRT_r_client_ip(sp)",
	    "VRT_l_client_ip(sp, ",
	},
	{ "req.request", STRING, 11,
	    "VRT_r_req_request(sp)",
	    "VRT_l_req_request(sp, ",
	},
	{ "req.host", STRING, 8,
	    "VRT_r_req_host(sp)",
	    "VRT_l_req_host(sp, ",
	},
	{ "req.url", STRING, 7,
	    "VRT_r_req_url(sp)",
	    "VRT_l_req_url(sp, ",
	},
	{ "req.proto", STRING, 9,
	    "VRT_r_req_proto(sp)",
	    "VRT_l_req_proto(sp, ",
	},
	{ "req.backend", BACKEND, 11,
	    "VRT_r_req_backend(sp)",
	    "VRT_l_req_backend(sp, ",
	},
	{ "obj.valid", BOOL, 9,
	    "VRT_r_obj_valid(sp)",
	    "VRT_l_obj_valid(sp, ",
	},
	{ "obj.cacheable", BOOL, 13,
	    "VRT_r_obj_cacheable(sp)",
	    "VRT_l_obj_cacheable(sp, ",
	},
	{ "obj.ttl", TIME, 7,
	    "VRT_r_obj_ttl(sp)",
	    "VRT_l_obj_ttl(sp, ",
	},
	{ "req.http.", HEADER, 9,
	    "VRT_r_req_http_(sp)",
	    "VRT_l_req_http_(sp, ",
	},
	{ "resp.http.", HEADER, 10,
	    "VRT_r_resp_http_(sp)",
	    "VRT_l_resp_http_(sp, ",
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
	"const unsigned char * VRT_r_client_ip(struct sess *);\n"
	"void VRT_l_client_ip(struct sess *, const unsigned char *);\n"
	"const char * VRT_r_req_request(struct sess *);\n"
	"void VRT_l_req_request(struct sess *, const char *);\n"
	"const char * VRT_r_req_host(struct sess *);\n"
	"void VRT_l_req_host(struct sess *, const char *);\n"
	"const char * VRT_r_req_url(struct sess *);\n"
	"void VRT_l_req_url(struct sess *, const char *);\n"
	"const char * VRT_r_req_proto(struct sess *);\n"
	"void VRT_l_req_proto(struct sess *, const char *);\n"
	"struct backend * VRT_r_req_backend(struct sess *);\n"
	"void VRT_l_req_backend(struct sess *, struct backend *);\n"
	"double VRT_r_obj_valid(struct sess *);\n"
	"void VRT_l_obj_valid(struct sess *, double);\n"
	"double VRT_r_obj_cacheable(struct sess *);\n"
	"void VRT_l_obj_cacheable(struct sess *, double);\n"
	"double VRT_r_obj_ttl(struct sess *);\n"
	"void VRT_l_obj_ttl(struct sess *, double);\n"
	"const char * VRT_r_req_http_(struct sess *);\n"
	"void VRT_l_req_http_(struct sess *, const char *);\n"
	"const char * VRT_r_resp_http_(struct sess *);\n"
	"void VRT_l_resp_http_(struct sess *, const char *);\n"
;
