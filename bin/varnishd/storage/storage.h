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

struct stv_objsecrets;
struct stevedore;
struct sess;
struct busyobj;
struct objcore;
struct worker;
struct lru;

typedef void storage_init_f(struct stevedore *, int ac, char * const *av);
typedef void storage_open_f(const struct stevedore *);
typedef struct storage *storage_alloc_f(struct stevedore *, size_t size);
typedef void storage_trim_f(struct storage *, size_t size, int move_ok);
typedef void storage_free_f(struct storage *);
typedef struct object *storage_allocobj_f(struct stevedore *, struct busyobj *,
    unsigned ltot, const struct stv_objsecrets *);
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
struct object *STV_MkObject(struct stevedore *stv, struct busyobj *bo,
    void *ptr, unsigned ltot, const struct stv_objsecrets *soc);

struct lru *LRU_Alloc(void);
void LRU_Free(struct lru *lru);

/*--------------------------------------------------------------------*/
extern const struct stevedore sma_stevedore;
extern const struct stevedore smf_stevedore;
extern const struct stevedore smp_stevedore;
#ifdef HAVE_LIBUMEM
extern const struct stevedore smu_stevedore;
#endif
