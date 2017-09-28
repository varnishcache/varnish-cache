/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 */

struct worker;
struct objhead;

typedef void hash_init_f(int ac, char * const *av);
typedef void hash_start_f(void);
typedef void hash_prep_f(struct worker *);
typedef struct objhead *hash_lookup_f(struct worker *, const void *digest,
    struct objhead **);
typedef int hash_deref_f(struct objhead *);

struct hash_slinger {
	unsigned		magic;
#define SLINGER_MAGIC		0x1b720cba
	const char		*name;
	hash_init_f		*init;
	hash_start_f		*start;
	hash_prep_f		*prep;
	hash_lookup_f		*lookup;
	hash_deref_f		*deref;
};

enum lookup_e {
	HSH_MISS,
	HSH_BUSY,
	HSH_HIT,
	HSH_EXP,
	HSH_EXPBUSY
};

/* mgt_hash.c */
void HSH_config(const char *);

/* cache_hash.c */
void HSH_Init(const struct hash_slinger *);
void HSH_Cleanup(struct worker *);

extern const struct hash_slinger hsl_slinger;
extern const struct hash_slinger hcl_slinger;
extern const struct hash_slinger hcb_slinger;
