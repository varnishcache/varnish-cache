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

struct hash_slinger;

struct objhead {
	unsigned		magic;
#define OBJHEAD_MAGIC		0x1b96615d

	int			refcnt;
	struct lock		mtx;
	VTAILQ_HEAD(,objcore)	objcs;
	uint8_t			digest[DIGEST_LEN];
	VTAILQ_HEAD(, req)	waitinglist;

	/*----------------------------------------------------
	 * The fields below are for the sole private use of
	 * the hash implementation(s).
	 */
	union {
		struct {
			VTAILQ_ENTRY(objhead)	u_n_hoh_list;
			void			*u_n_hoh_head;
		} n;
	} _u;
#define hoh_list _u.n.u_n_hoh_list
#define hoh_head _u.n.u_n_hoh_head
};

void HSH_Fail(struct objcore *);
void HSH_Kill(struct objcore *);
void HSH_Insert(struct worker *, const void *hash, struct objcore *,
    struct ban *);
void HSH_Unbusy(struct worker *, struct objcore *);
int HSH_Snipe(const struct worker *, struct objcore *);
struct boc *HSH_RefBoc(const struct objcore *);
void HSH_DerefBoc(struct worker *wrk, struct objcore *);
void HSH_DeleteObjHead(const struct worker *, struct objhead *);
int HSH_DerefObjHead(struct worker *, struct objhead **);

int HSH_DerefObjCore(struct worker *, struct objcore **, int rushmax);
#define HSH_RUSH_POLICY -1
#define HSH_RUSH_ALL	INT_MAX

enum lookup_e HSH_Lookup(struct req *, struct objcore **, struct objcore **,
    int always_insert);
void HSH_Ref(struct objcore *o);
void HSH_AddString(struct req *, void *ctx, const char *str);
unsigned HSH_Purge(struct worker *, struct objhead *, double ttl_now,
    double ttl, double grace, double keep);
struct objcore *HSH_Private(const struct worker *wrk);
void HSH_Abandon(struct objcore *oc);
