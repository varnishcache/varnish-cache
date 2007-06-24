/*
 * $Id$
 *
 * NB:  This file is machine generated, DO NOT EDIT!
 *
 * Edit vcc_gen_obj.tcl instead
 */

const char * VRT_r_backend_host(struct backend *);
void VRT_l_backend_host(struct backend *, const char *);
const char * VRT_r_backend_port(struct backend *);
void VRT_l_backend_port(struct backend *, const char *);
double VRT_r_backend_dnsttl(struct backend *);
void VRT_l_backend_dnsttl(struct backend *, double);
struct sockaddr * VRT_r_client_ip(struct sess *);
void VRT_l_client_ip(struct sess *, struct sockaddr *);
struct sockaddr * VRT_r_server_ip(struct sess *);
void VRT_l_server_ip(struct sess *, struct sockaddr *);
const char * VRT_r_req_request(struct sess *);
void VRT_l_req_request(struct sess *, const char *);
const char * VRT_r_req_host(struct sess *);
void VRT_l_req_host(struct sess *, const char *);
const char * VRT_r_req_url(struct sess *);
void VRT_l_req_url(struct sess *, const char *);
const char * VRT_r_req_proto(struct sess *);
void VRT_l_req_proto(struct sess *, const char *);
struct backend * VRT_r_req_backend(struct sess *);
void VRT_l_req_backend(struct sess *, struct backend *);
const char * VRT_r_req_hash(struct sess *);
void VRT_l_req_hash(struct sess *, const char *);
unsigned VRT_r_obj_valid(struct sess *);
void VRT_l_obj_valid(struct sess *, unsigned);
unsigned VRT_r_obj_cacheable(struct sess *);
void VRT_l_obj_cacheable(struct sess *, unsigned);
double VRT_r_obj_ttl(struct sess *);
void VRT_l_obj_ttl(struct sess *, double);
const char * VRT_r_req_http_(struct sess *);
void VRT_l_req_http_(struct sess *, const char *);
const char * VRT_r_resp_http_(struct sess *);
void VRT_l_resp_http_(struct sess *, const char *);
