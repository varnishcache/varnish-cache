/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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

typedef void storage_init_f(struct stevedore *, int ac, char * const *av);
typedef void storage_open_f(const struct stevedore *);
typedef struct storage *storage_alloc_f(struct stevedore *, size_t size);
typedef void storage_trim_f(struct storage *, size_t size);
typedef void storage_free_f(struct storage *);
typedef void storage_object_f(const struct sess *sp);
typedef void storage_close_f(const struct stevedore *);

struct stevedore {
	unsigned		magic;
#define STEVEDORE_MAGIC		0x4baf43db
	const char		*name;
	storage_init_f		*init;	/* called by mgt process */
	storage_open_f		*open;	/* called by cache process */
	storage_alloc_f		*alloc;
	storage_trim_f		*trim;
	storage_free_f		*free;
	storage_object_f	*object;
	storage_close_f		*close;

	struct objcore_head	lru;
	struct objcore		*lru_tail;

	/* private fields */
	void			*priv;

	VTAILQ_ENTRY(stevedore)	list;
};

struct storage *STV_alloc(struct sess *sp, size_t size);
void STV_trim(struct storage *st, size_t size);
void STV_free(struct storage *st);
void STV_add(const struct stevedore *stv, int ac, char * const *av);
void STV_open(void);
void STV_close(void);
struct objcore *STV_lru(struct storage *st);


int STV_GetFile(const char *fn, int *fdp, const char **fnp, const char *ctx);
uintmax_t STV_FileSize(int fd, const char *size, unsigned *granularity, const char *ctx);

/* Synthetic Storage */
void SMS_Init(void);

extern struct stevedore sma_stevedore;
extern struct stevedore smf_stevedore;
extern struct stevedore smp_stevedore;
#ifdef HAVE_LIBUMEM
extern struct stevedore smu_stevedore;
#endif

extern const struct choice STV_choice[];
