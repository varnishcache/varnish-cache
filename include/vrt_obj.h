/*
 * $Id: vcc_gen_obj.tcl 545 2006-07-21 20:43:56Z phk $
 *
 * NB:  This file is machine generated, DO NOT EDIT!
 *
 * Edit vcc_gen_obj.tcl instead
 */

const char * VRT_r_backend_host(struct backend *);
void VRT_l_backend_host(struct backend *, const char *);
const char * VRT_r_backend_port(struct backend *);
void VRT_l_backend_port(struct backend *, const char *);
const char * VRT_r_req_request(struct sess *);
void VRT_l_req_request(struct sess *, const char *);
const char * VRT_r_req_url(struct sess *);
void VRT_l_req_url(struct sess *, const char *);
double VRT_r_obj_valid(struct sess *);
void VRT_l_obj_valid(struct sess *, double);
double VRT_r_obj_cacheable(struct sess *);
void VRT_l_obj_cacheable(struct sess *, double);
struct backend * VRT_r_obj_backend(struct sess *);
void VRT_l_obj_backend(struct sess *, struct backend *);
double VRT_r_obj_ttl(struct sess *);
void VRT_l_obj_ttl(struct sess *, double);
const char * VRT_r_req_http_(struct sess *);
void VRT_l_req_http_(struct sess *, const char *);
