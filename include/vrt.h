/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 * Runtime support for compiled VCL programs.
 *
 * XXX: When this file is changed, lib/libvcc/generate.py *MUST* be rerun.
 */

struct req;
struct busyobj;
struct worker;
struct vsl_log;
struct http;
struct ws;
struct vsb;
struct cli;
struct director;
struct VCL_conf;
struct sockaddr_storage;
struct suckaddr;

/***********************************************************************
 * This is the central definition of the mapping from VCL types to
 * C-types.  The python scripts read these from here.
 */

typedef struct director *		VCL_BACKEND;
typedef unsigned			VCL_BOOL;
typedef double				VCL_BYTES;
typedef double				VCL_DURATION;
typedef const char *			VCL_ENUM;
typedef const struct gethdr_s *		VCL_HEADER;
typedef long				VCL_INT;
typedef const struct suckaddr *		VCL_IP;
typedef double				VCL_REAL;
typedef const char *			VCL_STRING;
typedef double				VCL_TIME;
typedef void				VCL_VOID;
typedef const struct vmod_priv *	VCL_BLOB;

/***********************************************************************
 * This is the composite argument we pass to compiled VCL and VRT
 * functions.
 */

struct vrt_ctx {
	unsigned			magic;
#define VRT_CTX_MAGIC			0x6bb8f0db

	unsigned			method;
	unsigned			*handling;

	struct vsl_log			*vsl;
	struct VCL_conf			*vcl;
	struct ws			*ws;

	struct req			*req;
	struct http			*http_req;
	struct http			*http_obj;
	struct http			*http_resp;

	struct busyobj			*bo;
	struct http			*http_bereq;
	struct http			*http_beresp;

};

/***********************************************************************/

enum gethdr_e { HDR_REQ, HDR_RESP, HDR_OBJ, HDR_BEREQ, HDR_BERESP };

struct gethdr_s {
	enum gethdr_e	where;
	const char	*what;
};

/*
 * A backend probe specification
 */

extern const void * const vrt_magic_string_end;
extern const void * const vrt_magic_string_unset;

struct vrt_backend_probe {
	const char	*url;
	const char	*request;
	double		timeout;
	double		interval;
	unsigned	exp_status;
	unsigned	window;
	unsigned	threshold;
	unsigned	initial;
};

/*
 * A backend is a host+port somewhere on the network
 */
struct vrt_backend {
	const char			*vcl_name;
	const char			*ipv4_addr;
	const char			*ipv6_addr;
	const char			*port;

	const struct suckaddr		*ipv4_suckaddr;
	const struct suckaddr		*ipv6_suckaddr;

	const char			*hosthdr;

	double				connect_timeout;
	double				first_byte_timeout;
	double				between_bytes_timeout;
	unsigned			max_connections;
	const struct vrt_backend_probe	*probe;
};

/*
 * A director with an unpredictable reply
 */

struct vrt_dir_random_entry {
	int					host;
	double					weight;
};

struct vrt_dir_random {
	const char				*name;
	unsigned				retries;
	unsigned				nmember;
	const struct vrt_dir_random_entry	*members;
};

/*
 * A director with dns-based selection
 */

struct vrt_dir_dns_entry {
	int					host;
};

struct vrt_dir_dns {
	const char				*name;
	const char				*suffix;
	const double				ttl;
	unsigned				nmember;
	const struct vrt_dir_dns_entry		*members;
};

/*
 * other stuff.
 * XXX: document when bored
 */

struct vrt_ref {
	unsigned	source;
	unsigned	offset;
	unsigned	line;
	unsigned	pos;
	unsigned	count;
	const char	*token;
};

/* ACL related */
#define VRT_ACL_MAXADDR		16	/* max(IPv4, IPv6) */

void VRT_acl_log(const struct vrt_ctx *, const char *msg);

/* req related */

int VRT_CacheReqBody(const struct vrt_ctx *, long long maxsize);

/* Regexp related */
void VRT_re_init(void **, const char *);
void VRT_re_fini(void *);
int VRT_re_match(const struct vrt_ctx *, const char *, void *re);
const char *VRT_regsub(const struct vrt_ctx *, int all, const char *,
    void *, const char *);

void VRT_ban_string(const struct vrt_ctx *, const char *);
void VRT_purge(const struct vrt_ctx *, double ttl, double grace);

void VRT_count(const struct vrt_ctx *, unsigned);
int VRT_rewrite(const char *, const char *);
void VRT_error(const struct vrt_ctx *, unsigned, const char *);
int VRT_switch_config(const char *);

char *VRT_GetHdr(const struct vrt_ctx *, const struct gethdr_s *);
void VRT_SetHdr(const struct vrt_ctx *, const struct gethdr_s *,
    const char *, ...);
void VRT_handling(const struct vrt_ctx *, unsigned hand);

void VRT_hashdata(const struct vrt_ctx *, const char *str, ...);

/* Simple stuff */
int VRT_strcmp(const char *s1, const char *s2);
void VRT_memmove(void *dst, const void *src, unsigned len);

void VRT_Rollback(const struct vrt_ctx *);

/* Synthetic pages */
void VRT_synth_page(const struct vrt_ctx *, const char *, ...);

/* Backend related */
void VRT_init_dir(struct cli *, struct director **, int idx, const void *priv);
void VRT_fini_dir(struct cli *, struct director *);

/* Suckaddr related */
int VRT_VSA_GetPtr(const struct suckaddr *sua, const unsigned char ** dst);

/* VMOD/Modules related */
int VRT_Vmod_Init(void **hdl, void *ptr, int len, const char *nm,
    const char *path, struct cli *cli);
void VRT_Vmod_Fini(void **hdl);

struct vmod_priv;
typedef void vmod_priv_free_f(void *);
struct vmod_priv {
	void			*priv;
	int			len;
	vmod_priv_free_f	*free;
};

typedef int vmod_init_f(struct vmod_priv *,  const struct VCL_conf *);

void VRT_priv_fini(const struct vmod_priv *p);

/* Stevedore related functions */
int VRT_Stv(const char *nm);

/* Convert things to string */

char *VRT_IP_string(const struct vrt_ctx *, VCL_IP);
char *VRT_INT_string(const struct vrt_ctx *, VCL_INT);
char *VRT_REAL_string(const struct vrt_ctx *, VCL_REAL);
char *VRT_TIME_string(const struct vrt_ctx *, VCL_TIME);
const char *VRT_BOOL_string(VCL_BOOL);
const char *VRT_BACKEND_string(VCL_BACKEND);
const char *VRT_CollectString(const struct vrt_ctx *, const char *p, ...);
