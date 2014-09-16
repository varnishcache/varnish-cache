/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 * This defines the backend interface between the stevedore and the
 * pluggable storage implementations.
 *
 */

struct stevedore;
struct sess;
struct busyobj;
struct objcore;
struct worker;
struct lru;
struct vsl_log;
struct vfp_ctx;

/* Storage -----------------------------------------------------------*/

struct storage {
	unsigned		magic;
#define STORAGE_MAGIC		0x1a4e51c0


	VTAILQ_ENTRY(storage)	list;
	struct stevedore	*stevedore;
	void			*priv;

	unsigned char		*ptr;
	unsigned		len;
	unsigned		space;
};

/* Object ------------------------------------------------------------*/

VTAILQ_HEAD(storagehead, storage);

struct object {
	unsigned		magic;
#define OBJECT_MAGIC		0x32851d42
	struct storage		*objstore;

	char			oa_vxid[4];
	uint8_t			*oa_vary;
	uint8_t			*oa_http;
	uint8_t			oa_flags[1];
	char			oa_gzipbits[24];
	char			oa_lastmodified[8];

	struct storagehead	list;
	ssize_t			len;

	struct storage		*esidata;
};

/* Methods on objcore ------------------------------------------------*/

typedef void updatemeta_f(struct worker *, struct objcore *oc);
typedef void freeobj_f(struct worker *, struct objcore *oc);
typedef struct lru *getlru_f(const struct objcore *oc);

/*
 * Stevedores can either be simple, and provide just this method:
 */

typedef struct object *getobj_f(struct worker *, struct objcore *oc);

/*
 * Or the can be "complex" and provide all of these methods:
 * (Described in comments in cache_obj.c)
 */
typedef void *objiterbegin_f(struct worker *, struct objcore *oc);
typedef enum objiter_status objiter_f(struct objcore *oc, void *oix,
    void **p, ssize_t *l);
typedef void objiterend_f(struct objcore *, void **oix);
typedef int objgetspace_f(struct worker *, struct objcore *,
     ssize_t *sz, uint8_t **ptr);
typedef void objextend_f(struct worker *, struct objcore *, ssize_t l);
typedef void objtrimstore_f(struct worker *, struct objcore *);
typedef void objslim_f(struct worker *, struct objcore *);
typedef void *objgetattr_f(struct worker *, struct objcore *,
    enum obj_attr attr, ssize_t *len);
typedef void *objsetattr_f(struct worker *, struct objcore *,
    enum obj_attr attr, ssize_t len, const void *ptr);
typedef uint64_t objgetlen_f(struct worker *, struct objcore *);

struct storeobj_methods {
	freeobj_f	*freeobj;
	getlru_f	*getlru;
	updatemeta_f	*updatemeta;

	getobj_f	*getobj;

	objiterbegin_f	*objiterbegin;
	objiter_f	*objiter;
	objiterend_f	*objiterend;
	objgetspace_f	*objgetspace;
	objextend_f	*objextend;
	objgetlen_f	*objgetlen;
	objtrimstore_f	*objtrimstore;
	objslim_f	*objslim;
	objgetattr_f	*objgetattr;
	objsetattr_f	*objsetattr;
};

/* Prototypes --------------------------------------------------------*/

typedef void storage_init_f(struct stevedore *, int ac, char * const *av);
typedef void storage_open_f(const struct stevedore *);
typedef struct storage *storage_alloc_f(struct stevedore *, size_t size);
typedef void storage_trim_f(struct storage *, size_t size, int move_ok);
typedef void storage_free_f(struct storage *);
typedef int storage_allocobj_f(struct stevedore *, struct objcore *,
    unsigned ltot);
typedef void storage_close_f(const struct stevedore *);
typedef void storage_signal_close_f(const struct stevedore *);
typedef int storage_baninfo_f(const struct stevedore *, enum baninfo event,
    const uint8_t *ban, unsigned len);
typedef void storage_banexport_f(const struct stevedore *, const uint8_t *bans,
    unsigned len);

/* Prototypes for VCL variable responders */
#define VRTSTVTYPE(ct) typedef ct storage_var_##ct(const struct stevedore *);
#include "tbl/vrt_stv_var.h"
#undef VRTSTVTYPE

extern storage_allocobj_f stv_default_allocobj;

extern const struct storeobj_methods default_oc_methods;

/*--------------------------------------------------------------------*/

struct stevedore {
	unsigned		magic;
#define STEVEDORE_MAGIC		0x4baf43db
	const char		*name;
	unsigned		transient;
	storage_init_f		*init;		/* called by mgt process */
	storage_open_f		*open;		/* called by cache process */
	storage_alloc_f		*alloc;		/* --//-- */
	storage_trim_f		*trim;		/* --//-- */
	storage_free_f		*free;		/* --//-- */
	storage_close_f		*close;		/* --//-- */
	storage_allocobj_f	*allocobj;	/* --//-- */
	storage_signal_close_f	*signal_close;	/* --//-- */
	storage_baninfo_f	*baninfo;	/* --//-- */
	storage_banexport_f	*banexport;	/* --//-- */

	const struct storeobj_methods
				*methods;

	struct lru		*lru;

#define VRTSTVVAR(nm, vtype, ctype, dval) storage_var_##ctype *var_##nm;
#include "tbl/vrt_stv_var.h"
#undef VRTSTVVAR

	/* private fields */
	void			*priv;

	VTAILQ_ENTRY(stevedore)	list;
	char			ident[16];	/* XXX: match VSM_chunk.ident */
};

VTAILQ_HEAD(stevedore_head, stevedore);

extern struct stevedore_head stv_stevedores;
extern struct stevedore *stv_transient;

/*--------------------------------------------------------------------*/
int STV_GetFile(const char *fn, int *fdp, const char **fnp, const char *ctx);
uintmax_t STV_FileSize(int fd, const char *size, unsigned *granularity,
    const char *ctx);
struct object *STV_MkObject(struct stevedore *, struct objcore *, void *ptr);

struct lru *LRU_Alloc(void);
void LRU_Free(struct lru *lru);

/*--------------------------------------------------------------------*/
extern const struct stevedore sma_stevedore;
extern const struct stevedore smf_stevedore;
extern const struct stevedore smp_stevedore;
#ifdef HAVE_LIBUMEM
extern const struct stevedore smu_stevedore;
#endif
