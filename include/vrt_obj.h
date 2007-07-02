/*
 * $Id$
 *
 * NB:  This file is machine generated, DO NOT EDIT!
 *
 * Edit vcc_gen_obj.tcl instead
 */

void VRT_l_backend_host(struct backend *, const char *);
void VRT_l_backend_port(struct backend *, const char *);
void VRT_l_backend_dnsttl(struct backend *, double);
struct sockaddr * VRT_r_client_ip(struct sess *);
struct sockaddr * VRT_r_server_ip(struct sess *);
const char * VRT_r_req_request(struct sess *);
const char * VRT_r_req_url(struct sess *);
const char * VRT_r_req_proto(struct sess *);
void VRT_l_req_hash(struct sess *, const char *);
struct backend * VRT_r_req_backend(struct sess *);
void VRT_l_req_backend(struct sess *, struct backend *);
const char * VRT_r_bereq_request(struct sess *);
void VRT_l_bereq_request(struct sess *, const char *);
const char * VRT_r_bereq_url(struct sess *);
void VRT_l_bereq_url(struct sess *, const char *);
const char * VRT_r_bereq_proto(struct sess *);
void VRT_l_bereq_proto(struct sess *, const char *);
const char * VRT_r_obj_proto(struct sess *);
void VRT_l_obj_proto(struct sess *, const char *);
int VRT_r_obj_status(struct sess *);
void VRT_l_obj_status(struct sess *, int);
const char * VRT_r_obj_response(struct sess *);
void VRT_l_obj_response(struct sess *, const char *);
unsigned VRT_r_obj_valid(struct sess *);
void VRT_l_obj_valid(struct sess *, unsigned);
unsigned VRT_r_obj_cacheable(struct sess *);
void VRT_l_obj_cacheable(struct sess *, unsigned);
double VRT_r_obj_ttl(struct sess *);
void VRT_l_obj_ttl(struct sess *, double);
const char * VRT_r_resp_proto(struct sess *);
void VRT_l_resp_proto(struct sess *, const char *);
int VRT_r_resp_status(struct sess *);
void VRT_l_resp_status(struct sess *, int);
const char * VRT_r_resp_response(struct sess *);
void VRT_l_resp_response(struct sess *, const char *);
