/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
 * Runtime support for compiled VCL programs and VMODs.
 *
 * NB: When this file is changed, lib/libvcc/generate.py *MUST* be rerun.
 */

#ifdef CACHE_H_INCLUDED
#  error "vrt.h included after cache.h - they are inclusive"
#endif

#ifdef VRT_H_INCLUDED
#  error "vrt.h included multiple times"
#endif
#define VRT_H_INCLUDED

#ifndef VDEF_H_INCLUDED
#  error "include vdef.h before vrt.h"
#endif

/***********************************************************************
 * Major and minor VRT API versions.
 *
 * Whenever something is added, increment MINOR version
 * Whenever something is deleted or changed in a way which is not
 * binary/load-time compatible, increment MAJOR version
 *
 * NEXT (2020-09-15)
 *	VRT_VDI_Resolve() added
 * 11.0 (2020-03-16)
 *	Changed type of vsa_suckaddr_len from int to size_t
 *	New prefix_{ptr|len} fields in vrt_backend
 *	VRT_HashStrands32() added
 *	VRT_l_resp_body() changed
 *	VRT_l_beresp_body() changed
 *	VRT_Format_Proxy() added	// transitional interface
 *	VRT_AllocStrandsWS() added
 * 10.0 (2019-09-15)
 *	VRT_UpperLowerStrands added.
 *	VRT_synth_page now takes STRANDS argument
 *	VRT_hashdata() now takes STRANDS argument
 *	VCL_BOOL VRT_Strands2Bool(VCL_STRANDS) added.
 *	VRT_BundleStrands() moved to vcc_interface.h
 *	VRT_VCL_{Busy|Unbusy} changed to VRT_VCL_{Prevent|Allow}_Cold
 *	VRT_re[fl]_vcl changed to VRT_VCL_{Prevent|Allow}_Discard
 *	VRT_Vmod_{Init|Unload} moved to vcc_interface.h
 *	VRT_count moved to vcc_interface.h
 *	VRT_VCL_Prevent_Cold() and VRT_VCL_Allow_Cold() added.
 *	VRT_vcl_get moved to vcc_interface.h
 *	VRT_vcl_rel moved to vcc_interface.h
 *	VRT_vcl_select moved to vcc_interface.h
 *	VRT_VSA_GetPtr() changed
 *	VRT_ipcmp() changed
 *	VRT_Stv_*() functions renamed to VRT_stevedore_*()
 *	[cache.h] WS_ReserveAll() added
 *	[cache.h] WS_Reserve(ws, 0) deprecated
 * 9.0 (2019-03-15)
 *	Make 'len' in vmod_priv 'long'
 *	HTTP_Copy() removed
 *	HTTP_Dup() added
 *	HTTP_Clone() added
 *	VCL_BLOB changed to newly introduced struct vrt_blob *
 *	VRT_blob() changed
 *	req->req_bodybytes removed
 *	    use: AZ(ObjGetU64(req->wrk, req->body_oc, OA_LEN, &u));
 *	struct vdi_methods .list callback signature changed
 *	VRT_LookupDirector() added
 *	VRT_SetChanged() added
 *	VRT_SetHealth() removed
 *	// in cache_filter.h:
 *	VRT_AddVDP() added
 *	VRT_RemoveVDP() added
 * 8.0 (2018-09-15)
 *	VRT_Strands() added
 *	VRT_StrandsWS() added
 *	VRT_CollectStrands() added
 *	VRT_STRANDS_string() removed from vrt.h (never implemented)
 *	VRT_Vmod_Init signature changed
 *	VRT_Vmod_Fini changed to VRT_Vmod_Unload
 *	// directors
 *	VRT_backend_healthy() removed
 *	VRT_Healthy() changed prototype
 *	struct vdi_methods and callback prototypes added
 *	struct director added;
 *	VRT_AddDirector() added
 *	VRT_SetHealth() added
 *	VRT_DisableDirector() added
 *	VRT_DelDirector() added
 *	// in cache_filter.h:
 *	VRT_AddVFP() added
 *	VRT_RemoveVFP() added
 * 7.0 (2018-03-15)
 *	lots of stuff moved from cache.h to cache_varnishd.h
 *	   (ie: from "$Abi vrt" to "$Abi strict")
 *	VCL_INT and VCL_BYTES are always 64 bits.
 *	path field added to struct vrt_backend
 *	VRT_Healthy() added
 *	VRT_VSC_Alloc() added
 *	VRT_VSC_Destroy() added
 *	VRT_VSC_Hide() added
 *	VRT_VSC_Reveal() added
 *	VRT_VSC_Overhead() added
 *	struct director.event added
 *	struct director.destroy added
 *	VRT_r_beresp_storage_hint() VCL <= 4.0  #2509
 *	VRT_l_beresp_storage_hint() VCL <= 4.0  #2509
 *	VRT_blob() added
 *	VCL_STRANDS added
 * 6.1 (2017-09-15 aka 5.2)
 *	http_CollectHdrSep added
 *	VRT_purge modified (may fail a transaction, signature changed)
 *	VRT_r_req_hash() added
 *	VRT_r_bereq_hash() added
 * 6.0 (2017-03-15):
 *	VRT_hit_for_pass added
 *	VRT_ipcmp added
 *	VRT_Vmod_Init signature changed
 *	VRT_vcl_lookup removed
 *	VRT_fail added
 *	[cache.h] WS_Reset and WS_Snapshot signatures changed
 *	[cache.h] WS_Front added
 *	[cache.h] WS_ReserveLumps added
 *	[cache.h] WS_Inside added
 *	[cache.h] WS_Assert_Allocated added
 * 5.0:
 *	Varnish 5.0 release "better safe than sorry" bump
 * 4.0:
 *	VCL_BYTES changed to long long
 *	VRT_CacheReqBody changed signature
 * 3.2:
 *	vrt_backend grew .proxy_header field
 *	vrt_ctx grew .sp field.
 *	vrt_acl type added
 */

#define VRT_MAJOR_VERSION	11U

#define VRT_MINOR_VERSION	0U

/***********************************************************************/

#include <stddef.h>		// NULL, size_t
#include <stdint.h>		// [u]int%d_t

struct VCL_conf;
struct busyobj;
struct director;
struct http;
struct req;
struct stevedore;
struct suckaddr;
struct vcl;
struct vmod;
struct vmod_priv;
struct vrt_acl;
struct vsb;
struct vsc_seg;
struct vsmw_cluster;
struct vsl_log;
struct ws;
struct VSC_main;

/*
 * VCL_STRANDS:
 *
 * An argc+argv type of data structure where n indicates the number of strings
 * in the p array. Individual components of a strands may be null.
 *
 * A STRANDS allows you to work on a strings concatenation with the option to
 * collect it into a single STRING, or if possible work directly on individual
 * parts.
 *
 * The memory management is very strict: a VMOD function receiving a STRANDS
 * argument should keep no reference after the function returns. Retention of
 * a STRANDS further in the ongoing task is undefined behavior and may result
 * in a panic or data corruption.
 */

struct strands {
	int		n;
	const char	**p;
};

struct strands * VRT_AllocStrandsWS(struct ws *, int);


/*
 * VCL_BLOB:
 *
 * Opaque, immutable data (pointer + length), minimum lifetime is the VCL task.
 *
 * Type (optional) is owned by the creator of the blob. blob consumers may use
 * it for checks, but should not assert on it.
 *
 * The data behind the blob pointer is assumed to be immutable for the blob's
 * lifetime.
 *
 * Memory management is either implicit or up to the vmod:
 *
 * - memory for shortlived blobs should come from the respective workspace
 *
 * - management of memory for longer lived blobs is up to the vmod
 *   (in which case the blob will probably be embedded in an object or
 *    referenced by other state with vcl lifetime)
 */

struct vrt_blob {
	unsigned	type;
	size_t		len;
	const void	*blob;
};

/***********************************************************************
 * This is the central definition of the mapping from VCL types to
 * C-types.  The python scripts read these from here.
 * (alphabetic order)
 */

typedef const struct vrt_acl *			VCL_ACL;
typedef const struct director *			VCL_BACKEND;
typedef const struct vrt_blob *			VCL_BLOB;
typedef const char *				VCL_BODY;
typedef unsigned				VCL_BOOL;
typedef int64_t					VCL_BYTES;
typedef vtim_dur				VCL_DURATION;
typedef const char *				VCL_ENUM;
typedef const struct gethdr_s *			VCL_HEADER;
typedef struct http *				VCL_HTTP;
typedef void					VCL_INSTANCE;
typedef int64_t					VCL_INT;
typedef const struct suckaddr *			VCL_IP;
typedef const struct vrt_backend_probe *	VCL_PROBE;
typedef double					VCL_REAL;
typedef const struct stevedore *		VCL_STEVEDORE;
typedef const struct strands *			VCL_STRANDS;
typedef const char *				VCL_STRING;
typedef vtim_real				VCL_TIME;
typedef struct vcl *				VCL_VCL;
typedef void					VCL_VOID;

struct vrt_type {
	unsigned			magic;
#define VRT_TYPE_MAGIC			0xa943bc32
	const char			*lname;
	const char			*uname;
	const char			*ctype;
	size_t				szof;
};

enum lbody_e {
	LBODY_SET,
	LBODY_ADD,
};

/***********************************************************************
 * This is the composite argument we pass to compiled VCL and VRT
 * functions.
 */

struct vrt_ctx {
	unsigned			magic;
#define VRT_CTX_MAGIC			0x6bb8f0db

	unsigned			syntax;
	unsigned			method;
	unsigned			*handling;
	unsigned			vclver;

	/*
	 * msg is for error messages and exists only for
	 * VCL_EVENT_LOAD
	 * VCL_EVENT_WARM
	 */
	struct vsb			*msg;
	struct vsl_log			*vsl;
	VCL_VCL				vcl;
	struct ws			*ws;

	struct sess			*sp;

	struct req			*req;
	VCL_HTTP			http_req;
	VCL_HTTP			http_req_top;
	VCL_HTTP			http_resp;

	struct busyobj			*bo;
	VCL_HTTP			http_bereq;
	VCL_HTTP			http_beresp;

	vtim_real			now;

	/*
	 * method specific argument:
	 *    hash:		struct VSHA256Context
	 *    synth+error:	struct vsb *
	 */
	void				*specific;
};

#define VRT_CTX		const struct vrt_ctx *ctx

/***********************************************************************
 * This is the interface structure to a compiled VMOD
 */

struct vmod_data {
	/* The version/id fields must be first, they protect the rest */
	unsigned			vrt_major;
	unsigned			vrt_minor;
	const char			*file_id;

	const char			*name;
	const char			*func_name;
	const void			*func;
	int				func_len;
	const char			*proto;
	const char			*json;
	const char			*abi;
};

/***********************************************************************
 * Enum for events sent to compiled VCL and from there to Vmods
 */

enum vcl_event_e {
	VCL_EVENT_LOAD,
	VCL_EVENT_WARM,
	VCL_EVENT_COLD,
	VCL_EVENT_DISCARD,
};

/***********************************************************************/

extern const void * const vrt_magic_string_end;
extern const void * const vrt_magic_string_unset;

/***********************************************************************
 * We want the VCC to spit this structs out as const, but when VMODs
 * come up with them we want to clone them into malloc'ed space which
 * we can free again.
 * We collect all the knowledge here by macroizing the fields and make
 * a macro for handling them all.
 * See also:  cache_backend.h & cache_backend_cfg.c
 * One of those things...
 */

#define VRT_BACKEND_FIELDS(rigid)				\
	rigid char			*vcl_name;		\
	rigid char			*ipv4_addr;		\
	rigid char			*ipv6_addr;		\
	rigid char			*port;			\
	rigid char			*path;			\
	rigid char			*hosthdr;		\
	vtim_dur			connect_timeout;	\
	vtim_dur			first_byte_timeout;	\
	vtim_dur			between_bytes_timeout;	\
	unsigned			max_connections;	\
	unsigned			proxy_header;		\
	void				*prefix_ptr;		\
	unsigned			prefix_len;

#define VRT_BACKEND_HANDLE()			\
	do {					\
		DA(vcl_name);			\
		DA(ipv4_addr);			\
		DA(ipv6_addr);			\
		DA(port);			\
		DA(path);			\
		DA(hosthdr);			\
		DN(connect_timeout);		\
		DN(first_byte_timeout);		\
		DN(between_bytes_timeout);	\
		DN(max_connections);		\
		DN(proxy_header);		\
	} while(0)

struct vrt_backend {
	unsigned			magic;
#define VRT_BACKEND_MAGIC		0x4799ce6c
	VRT_BACKEND_FIELDS(const)
	VCL_IP				ipv4_suckaddr;
	VCL_IP				ipv6_suckaddr;
	VCL_PROBE			probe;
};

#define VRT_BACKEND_PROBE_FIELDS(rigid)				\
	vtim_dur			timeout;		\
	vtim_dur			interval;		\
	unsigned			exp_status;		\
	unsigned			window;			\
	unsigned			threshold;		\
	unsigned			initial;

#define VRT_BACKEND_PROBE_HANDLE()		\
	do {					\
		DN(timeout);			\
		DN(interval);			\
		DN(exp_status);			\
		DN(window);			\
		DN(threshold);			\
		DN(initial);			\
	} while (0)

struct vrt_backend_probe {
	unsigned			magic;
#define VRT_BACKEND_PROBE_MAGIC		0x84998490
	const char			*url;
	const char			*request;
	VRT_BACKEND_PROBE_FIELDS(const)
};

VCL_BACKEND VRT_VDI_Resolve(VRT_CTX, VCL_BACKEND);

/***********************************************************************
 * Implementation details of ACLs
 */

void VRT_acl_log(VRT_CTX, const char *);
int VRT_acl_match(VRT_CTX, VCL_ACL, VCL_IP);

/***********************************************************************
 * Compile time regexp
 */

void VRT_re_init(void **, const char *);
void VRT_re_fini(void *);
int VRT_re_match(VRT_CTX, const char *, void *);

/***********************************************************************
 * Getting hold of the various struct http
 */

enum gethdr_e {
	HDR_REQ,
	HDR_REQ_TOP,
	HDR_RESP,
	HDR_OBJ,
	HDR_BEREQ,
	HDR_BERESP
};

struct gethdr_s {
	enum gethdr_e	where;
	const char	*what;
};

VCL_HTTP VRT_selecthttp(VRT_CTX, enum gethdr_e);
VCL_STRING VRT_GetHdr(VRT_CTX, VCL_HEADER);

/***********************************************************************
 * req related
 */

VCL_BYTES VRT_CacheReqBody(VRT_CTX, VCL_BYTES maxsize);

/* Regexp related */

const char *VRT_regsub(VRT_CTX, int all, const char *, void *, const char *);
VCL_VOID VRT_ban_string(VRT_CTX, VCL_STRING);
VCL_INT VRT_purge(VRT_CTX, VCL_DURATION, VCL_DURATION, VCL_DURATION);
VCL_VOID VRT_synth(VRT_CTX, VCL_INT, VCL_STRING);
VCL_VOID VRT_hit_for_pass(VRT_CTX, VCL_DURATION);

VCL_VOID VRT_SetHdr(VRT_CTX, VCL_HEADER, const char *, ...);
VCL_VOID VRT_handling(VRT_CTX, unsigned hand);
VCL_VOID VRT_fail(VRT_CTX, const char *fmt, ...) v_printflike_(2,3);
VCL_VOID VRT_hashdata(VRT_CTX, VCL_STRANDS);

/* Simple stuff */
int VRT_strcmp(const char *s1, const char *s2);
void VRT_memmove(void *dst, const void *src, unsigned len);
VCL_BOOL VRT_ipcmp(VRT_CTX, VCL_IP, VCL_IP);
VCL_BLOB VRT_blob(VRT_CTX, const char *, const void *, size_t, unsigned);

VCL_VOID VRT_Rollback(VRT_CTX, VCL_HTTP);

/* Synthetic pages */
VCL_VOID VRT_synth_page(VRT_CTX, VCL_STRANDS);

/* Backend related */
VCL_BACKEND VRT_new_backend(VRT_CTX, const struct vrt_backend *);
VCL_BACKEND VRT_new_backend_clustered(VRT_CTX,
    struct vsmw_cluster *, const struct vrt_backend *);
size_t VRT_backend_vsm_need(VRT_CTX);
void VRT_delete_backend(VRT_CTX, VCL_BACKEND *);

/* VSM related */
struct vsmw_cluster *VRT_VSM_Cluster_New(VRT_CTX, size_t);
void VRT_VSM_Cluster_Destroy(VRT_CTX, struct vsmw_cluster **);

/* VDI - Director API */
typedef VCL_BOOL vdi_healthy_f(VRT_CTX, VCL_BACKEND, VCL_TIME *);
typedef VCL_BACKEND vdi_resolve_f(VRT_CTX, VCL_BACKEND);
typedef int vdi_gethdrs_f(VRT_CTX, VCL_BACKEND);
typedef VCL_IP vdi_getip_f(VRT_CTX, VCL_BACKEND);
typedef void vdi_finish_f(VRT_CTX, VCL_BACKEND);
typedef enum sess_close vdi_http1pipe_f(VRT_CTX, VCL_BACKEND);
typedef void vdi_event_f(VCL_BACKEND, enum vcl_event_e);
typedef void vdi_destroy_f(VCL_BACKEND);
typedef void vdi_panic_f(VCL_BACKEND, struct vsb *);
typedef void vdi_list_f(VRT_CTX, VCL_BACKEND, struct vsb *, int, int);

struct vdi_methods {
	unsigned			magic;
#define VDI_METHODS_MAGIC		0x4ec0c4bb
	const char			*type;
	vdi_http1pipe_f			*http1pipe;
	vdi_healthy_f			*healthy;
	vdi_resolve_f			*resolve;
	vdi_gethdrs_f			*gethdrs;
	vdi_getip_f			*getip;
	vdi_finish_f			*finish;
	vdi_event_f			*event;
	vdi_destroy_f			*destroy;
	vdi_panic_f			*panic;
	vdi_list_f			*list;
};

struct vcldir;

struct director {
	unsigned			magic;
#define DIRECTOR_MAGIC			0x3336351d
	void				*priv;
	char				*vcl_name;
	struct vcldir			*vdir;
};

VCL_BOOL VRT_Healthy(VRT_CTX, VCL_BACKEND, VCL_TIME *);
VCL_VOID VRT_SetChanged(VCL_BACKEND, VCL_TIME);
VCL_BACKEND VRT_AddDirector(VRT_CTX, const struct vdi_methods *,
    void *, const char *, ...) v_printflike_(4, 5);
void VRT_DisableDirector(VCL_BACKEND);
VCL_BACKEND VRT_LookupDirector(VRT_CTX, VCL_STRING);
void VRT_DelDirector(VCL_BACKEND *);

/* Suckaddr related */
int VRT_VSA_GetPtr(VRT_CTX, VCL_IP sua, const unsigned char ** dst);
/* transitional interface */
void VRT_Format_Proxy(struct vsb *, VCL_INT, VCL_IP, VCL_IP, VCL_STRING);

typedef int vmod_event_f(VRT_CTX, struct vmod_priv *, enum vcl_event_e);

typedef void vmod_priv_free_f(void *);
struct vmod_priv {
	void			*priv;
	long			len;
	vmod_priv_free_f	*free;
};

void VRT_priv_fini(const struct vmod_priv *p);
struct vmod_priv *VRT_priv_task(VRT_CTX, const void *vmod_id);
struct vmod_priv *VRT_priv_top(VRT_CTX, const void *vmod_id);

/* Stevedore related functions */
int VRT_Stv(const char *nm);
VCL_STEVEDORE VRT_stevedore(const char *nm);

/* Convert things to string */

int VRT_CompareStrands(VCL_STRANDS a, VCL_STRANDS b);
VCL_BOOL VRT_Strands2Bool(VCL_STRANDS);
uint32_t VRT_HashStrands32(VCL_STRANDS);
char *VRT_Strands(char *, size_t, VCL_STRANDS);
VCL_STRING VRT_StrandsWS(struct ws *, const char *, VCL_STRANDS);
VCL_STRING VRT_CollectStrands(VRT_CTX, VCL_STRANDS);
VCL_STRING VRT_UpperLowerStrands(VRT_CTX, VCL_STRANDS s, int up);

VCL_STRING VRT_BACKEND_string(VCL_BACKEND);
VCL_STRING VRT_BOOL_string(VCL_BOOL);
VCL_STRING VRT_CollectString(VRT_CTX, const char *p, ...);
VCL_STRING VRT_INT_string(VRT_CTX, VCL_INT);
VCL_STRING VRT_IP_string(VRT_CTX, VCL_IP);
VCL_STRING VRT_REAL_string(VRT_CTX, VCL_REAL);
VCL_STRING VRT_STEVEDORE_string(VCL_STEVEDORE);
VCL_STRING VRT_TIME_string(VRT_CTX, VCL_TIME);

#ifdef va_start	// XXX: hackish
void *VRT_VSC_Alloc(struct vsmw_cluster *, struct vsc_seg **,
    const char *, size_t, const unsigned char *, size_t, const char *, va_list);
#endif
void VRT_VSC_Destroy(const char *, struct vsc_seg *);
void VRT_VSC_Hide(const struct vsc_seg *);
void VRT_VSC_Reveal(const struct vsc_seg *);
size_t VRT_VSC_Overhead(size_t);

/*
 * API to restrict the VCL in various ways
 */

struct vclref;
struct vclref * VRT_VCL_Prevent_Cold(VRT_CTX, const char *);
void VRT_VCL_Allow_Cold(struct vclref **);

struct vclref * VRT_VCL_Prevent_Discard(VRT_CTX, const char *);
void VRT_VCL_Allow_Discard(struct vclref **);
