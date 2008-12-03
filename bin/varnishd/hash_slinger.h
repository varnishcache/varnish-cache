/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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

struct sess;
struct object;

typedef void hash_init_f(int ac, char * const *av);
typedef void hash_start_f(void);
typedef struct objhead *
    hash_lookup_f(const struct sess *sp, struct objhead *nobj);
typedef int hash_deref_f(struct objhead *obj);

struct hash_slinger {
	unsigned		magic;
#define SLINGER_MAGIC		0x1b720cba
	const char		*name;
	hash_init_f		*init;
	hash_start_f		*start;
	hash_lookup_f		*lookup;
	hash_deref_f		*deref;
};

/* cache_hash.c */
void HSH_Prealloc(struct sess *sp);
void HSH_Freestore(struct object *o);
int HSH_Compare(const struct sess *sp, const struct objhead *o);
void HSH_Copy(const struct sess *sp, struct objhead *o);
struct object *HSH_Lookup(struct sess *sp);
void HSH_Unbusy(const struct sess *sp);
void HSH_Ref(struct object *o);
void HSH_Deref(struct object *o);
double HSH_Grace(double g);
void HSH_Init(void);
void HSH_AddString(struct sess *sp, const char *str);
void HSH_Prepare(struct sess *sp, unsigned hashcount);


#ifdef VARNISH_CACHE_CHILD
struct objhead {
	unsigned		magic;
#define OBJHEAD_MAGIC		0x1b96615d

	struct lock		mtx;
	unsigned		refcnt;
	VTAILQ_HEAD(,object)	objects;
	char			*hash;
	unsigned		hashlen;
	unsigned char		digest[32];
	unsigned char		digest_len;
	VTAILQ_HEAD(, sess)	waitinglist;

	/*----------------------------------------------------
	 * The fields below are for the sole private use of
	 * the hash implementation(s).
	 */
	union {
		void		*filler[3];
		struct {
			VTAILQ_ENTRY(objhead)	u_n_hoh_list;
			void			*u_n_hoh_head;
			unsigned		u_n_hoh_digest;
		} n;
	} u;
#define hoh_list u.n.u_n_hoh_list
#define hoh_head u.n.u_n_hoh_head
#define hoh_digest u.n.u_n_hoh_digest
};
#endif /* VARNISH_CACHE_CHILD */
