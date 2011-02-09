/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Linpro AS
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
 * $Id$
 */

struct stevedore;
struct sess;
struct iovec;
struct object;
struct objcore;
struct stv_objsecrets;

typedef void storage_init_f(struct stevedore *, int ac, char * const *av);
typedef void storage_open_f(const struct stevedore *);
typedef struct storage *storage_alloc_f(struct stevedore *, size_t size);
typedef void storage_trim_f(struct storage *, size_t size);
typedef void storage_free_f(struct storage *);
typedef struct object *storage_allocobj_f(struct stevedore *, struct sess *sp,
    unsigned ltot, const struct stv_objsecrets *);
typedef void storage_close_f(const struct stevedore *);

/* Prototypes for VCL variable responders */
#define VRTSTVTYPE(ct) typedef ct storage_var_##ct(const struct stevedore *);
#include "vrt_stv_var.h"
#undef VRTSTVTYPE

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

	struct lru		*lru;

#define VRTSTVVAR(nm, vtype, ctype, dval) storage_var_##ctype *var_##nm;
#include "vrt_stv_var.h"
#undef VRTSTVVAR

	/* private fields */
	void			*priv;

	VTAILQ_ENTRY(stevedore)	list;
	char			ident[16];	/* XXX: match vsm_chunk.ident */
};

struct object *STV_MkObject(struct sess *sp, void *ptr, unsigned ltot,
    const struct stv_objsecrets *soc);

struct object *STV_NewObject(struct sess *sp, const char *hint, unsigned len,
    double ttl, unsigned nhttp);
struct storage *STV_alloc(const struct sess *sp, size_t size);
void STV_trim(struct storage *st, size_t size);
void STV_free(struct storage *st);
void STV_open(void);
void STV_close(void);
void STV_Config(const char *spec);
void STV_Config_Transient(void);
void STV_Freestore(struct object *o);

struct lru *LRU_Alloc(void);

int STV_GetFile(const char *fn, int *fdp, const char **fnp, const char *ctx);
uintmax_t STV_FileSize(int fd, const char *size, unsigned *granularity,
    const char *ctx);

/* Synthetic Storage */
void SMS_Init(void);

extern const struct stevedore sma_stevedore;
extern const struct stevedore smf_stevedore;
extern const struct stevedore smp_stevedore;
#ifdef HAVE_LIBUMEM
extern const struct stevedore smu_stevedore;
#endif
