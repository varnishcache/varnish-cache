/*-
 * Copyright (c) 2008-2011 Varnish Software AS
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
 * A Crit Bit tree based hash
 */

// #define PHK

#include "config.h"

#include <stdlib.h>

#include "cache/cache.h"

#include "hash/hash_slinger.h"
#include "vcli_priv.h"
#include "vmb.h"
#include "vtim.h"

static struct lock hcb_mtx;

/*---------------------------------------------------------------------
 * Table for finding out how many bits two bytes have in common,
 * counting from the MSB towards the LSB.
 * ie:
 *	hcb_bittbl[0x01 ^ 0x22] == 2
 *	hcb_bittbl[0x10 ^ 0x0b] == 3
 *
 */

static unsigned char hcb_bittbl[256];

static unsigned char
hcb_bits(unsigned char x, unsigned char y)
{
	return (hcb_bittbl[x ^ y]);
}

static void
hcb_build_bittbl(void)
{
	unsigned char x;
	unsigned y;

	y = 0;
	for (x = 0; x < 8; x++)
		for (; y < (1U << x); y++)
			hcb_bittbl[y] = 8 - x;

	/* Quick asserts for sanity check */
	assert(hcb_bits(0x34, 0x34) == 8);
	assert(hcb_bits(0xaa, 0x55) == 0);
	assert(hcb_bits(0x01, 0x22) == 2);
	assert(hcb_bits(0x10, 0x0b) == 3);
}

/*---------------------------------------------------------------------
 * For space reasons we overload the two pointers with two different
 * kinds of of pointers.  We cast them to uintptr_t's and abuse the
 * low two bits to tell them apart, assuming that Varnish will never
 * run on machines with less than 32bit alignment.
 *
 * Asserts will explode if these assumptions are not met.
 */

struct hcb_y {
	unsigned		magic;
#define HCB_Y_MAGIC		0x125c4bd2
	unsigned short		critbit;
	unsigned char		ptr;
	unsigned char		bitmask;
	volatile uintptr_t	leaf[2];
	VSTAILQ_ENTRY(hcb_y)	list;
};

#define HCB_BIT_NODE		(1<<0)
#define HCB_BIT_Y		(1<<1)

struct hcb_root {
	volatile uintptr_t	origo;
};

static struct hcb_root	hcb_root;

static VSTAILQ_HEAD(, hcb_y)	cool_y = VSTAILQ_HEAD_INITIALIZER(cool_y);
static VSTAILQ_HEAD(, hcb_y)	dead_y = VSTAILQ_HEAD_INITIALIZER(dead_y);
static VTAILQ_HEAD(, objhead)	cool_h = VTAILQ_HEAD_INITIALIZER(cool_h);
static VTAILQ_HEAD(, objhead)	dead_h = VTAILQ_HEAD_INITIALIZER(dead_h);

/*---------------------------------------------------------------------
 * Pointer accessor functions
 */
static int
hcb_is_node(uintptr_t u)
{

	return (u & HCB_BIT_NODE);
}

static int
hcb_is_y(uintptr_t u)
{

	return (u & HCB_BIT_Y);
}

static uintptr_t
hcb_r_node(struct objhead *n)
{

	assert(!((uintptr_t)n & (HCB_BIT_NODE|HCB_BIT_Y)));
	return (HCB_BIT_NODE | (uintptr_t)n);
}

static struct objhead *
hcb_l_node(uintptr_t u)
{

	assert(u & HCB_BIT_NODE);
	assert(!(u & HCB_BIT_Y));
	return ((struct objhead *)(u & ~HCB_BIT_NODE));
}

static uintptr_t
hcb_r_y(struct hcb_y *y)
{

	CHECK_OBJ_NOTNULL(y, HCB_Y_MAGIC);
	assert(!((uintptr_t)y & (HCB_BIT_NODE|HCB_BIT_Y)));
	return (HCB_BIT_Y | (uintptr_t)y);
}

static struct hcb_y *
hcb_l_y(uintptr_t u)
{

	assert(!(u & HCB_BIT_NODE));
	assert(u & HCB_BIT_Y);
	return ((struct hcb_y *)(u & ~HCB_BIT_Y));
}

/*---------------------------------------------------------------------
 * Find the "critical" bit that separates these two digests
 */

static unsigned
hcb_crit_bit(const uint8_t *digest, const struct objhead *oh2, struct hcb_y *y)
{
	unsigned char u, r;

	CHECK_OBJ_NOTNULL(y, HCB_Y_MAGIC);
	for (u = 0; u < DIGEST_LEN && digest[u] == oh2->digest[u]; u++)
		;
	assert(u < DIGEST_LEN);
	r = hcb_bits(digest[u], oh2->digest[u]);
	y->ptr = u;
	y->bitmask = 0x80 >> r;
	y->critbit = u * 8 + r;
	return (y->critbit);
}

/*---------------------------------------------------------------------
 * Unless we have the lock, we need to be very careful about pointer
 * references into the tree, we cannot trust things to be the same
 * in two consequtive memory accesses.
 */

static struct objhead *
hcb_insert(struct worker *wrk, struct hcb_root *root, const uint8_t *digest,
    struct objhead **noh)
{
	volatile uintptr_t *p;
	uintptr_t pp;
	struct hcb_y *y, *y2;
	struct objhead *oh2;
	unsigned s, s2;

	p = &root->origo;
	pp = *p;
	if (pp == 0) {
		if (noh == NULL)
			return (NULL);
		oh2 = *noh;
		*noh = NULL;
		memcpy(oh2->digest, digest, sizeof oh2->digest);
		*p = hcb_r_node(oh2);
		return (oh2);
	}

	while(hcb_is_y(pp)) {
		y = hcb_l_y(pp);
		CHECK_OBJ_NOTNULL(y, HCB_Y_MAGIC);
		assert(y->ptr < DIGEST_LEN);
		s = (digest[y->ptr] & y->bitmask) != 0;
		assert(s < 2);
		p = &y->leaf[s];
		pp = *p;
	}

	if (pp == 0) {
		/* We raced hcb_delete and got a NULL pointer */
		assert(noh == NULL);
		return (NULL);
	}

	assert(hcb_is_node(pp));

	/* We found a node, does it match ? */
	oh2 = hcb_l_node(pp);
	CHECK_OBJ_NOTNULL(oh2, OBJHEAD_MAGIC);
	if (!memcmp(oh2->digest, digest, DIGEST_LEN))
		return (oh2);

	if (noh == NULL)
		return (NULL);

	/* Insert */

	CAST_OBJ_NOTNULL(y2, wrk->nhashpriv, HCB_Y_MAGIC);
	wrk->nhashpriv = NULL;
	(void)hcb_crit_bit(digest, oh2, y2);
	s2 = (digest[y2->ptr] & y2->bitmask) != 0;
	assert(s2 < 2);
	oh2 = *noh;
	*noh = NULL;
	memcpy(oh2->digest, digest, sizeof oh2->digest);
	y2->leaf[s2] = hcb_r_node(oh2);
	s2 = 1-s2;

	p = &root->origo;
	assert(*p != 0);

	while(hcb_is_y(*p)) {
		y = hcb_l_y(*p);
		CHECK_OBJ_NOTNULL(y, HCB_Y_MAGIC);
		assert(y->critbit != y2->critbit);
		if (y->critbit > y2->critbit)
			break;
		assert(y->ptr < DIGEST_LEN);
		s = (digest[y->ptr] & y->bitmask) != 0;
		assert(s < 2);
		p = &y->leaf[s];
	}
	y2->leaf[s2] = *p;
	VWMB();
	*p = hcb_r_y(y2);
	return(oh2);
}

/*--------------------------------------------------------------------*/

static void
hcb_delete(struct hcb_root *r, struct objhead *oh)
{
	struct hcb_y *y;
	volatile uintptr_t *p;
	unsigned s;

	if (r->origo == hcb_r_node(oh)) {
		r->origo = 0;
		return;
	}
	p = &r->origo;
	assert(hcb_is_y(*p));

	y = NULL;
	while(1) {
		assert(hcb_is_y(*p));
		y = hcb_l_y(*p);
		assert(y->ptr < DIGEST_LEN);
		s = (oh->digest[y->ptr] & y->bitmask) != 0;
		assert(s < 2);
		if (y->leaf[s] == hcb_r_node(oh)) {
			*p = y->leaf[1 - s];
			VSTAILQ_INSERT_TAIL(&cool_y, y, list);
			return;
		}
		p = &y->leaf[s];
	}
}

/*--------------------------------------------------------------------*/

static void
dumptree(struct cli *cli, uintptr_t p, int indent)
{
	int i;
	const struct objhead *oh;
	const struct hcb_y *y;

	if (p == 0)
		return;
	if (hcb_is_node(p)) {
		oh = hcb_l_node(p);
		VCLI_Out(cli, "%*.*sN %d r%u <%02x%02x%02x...>\n",
		    indent, indent, "", indent / 2, oh->refcnt,
		    oh->digest[0], oh->digest[1], oh->digest[2]);
		return;
	}
	assert(hcb_is_y(p));
	y = hcb_l_y(p);
	VCLI_Out(cli, "%*.*sY c %u p %u b %02x i %d\n",
	    indent, indent, "",
	    y->critbit, y->ptr, y->bitmask, indent / 2);
	indent += 2;
	for (i = 0; i < 2; i++)
		dumptree(cli, y->leaf[i], indent);
}

static void
hcb_dump(struct cli *cli, const char * const *av, void *priv)
{

	(void)priv;
	(void)av;
	VCLI_Out(cli, "HCB dump:\n");
	dumptree(cli, hcb_root.origo, 0);
	VCLI_Out(cli, "Coollist:\n");
}

static struct cli_proto hcb_cmds[] = {
	{ "hcb.dump", "hcb.dump", "dump HCB tree\n", 0, 0, "d", hcb_dump },
	{ NULL }
};

/*--------------------------------------------------------------------*/

static void * __match_proto__(bgthread_t)
hcb_cleaner(struct worker *wrk, void *priv)
{
	struct hcb_y *y, *y2;
	struct objhead *oh, *oh2;

	(void)priv;
	while (1) {
		VSTAILQ_FOREACH_SAFE(y, &dead_y, list, y2) {
			VSTAILQ_REMOVE_HEAD(&dead_y, list);
			FREE_OBJ(y);
		}
		VTAILQ_FOREACH_SAFE(oh, &dead_h, hoh_list, oh2) {
			VTAILQ_REMOVE(&dead_h, oh, hoh_list);
			HSH_DeleteObjHead(&wrk->stats, oh);
		}
		Lck_Lock(&hcb_mtx);
		VSTAILQ_CONCAT(&dead_y, &cool_y);
		VTAILQ_CONCAT(&dead_h, &cool_h, hoh_list);
		Lck_Unlock(&hcb_mtx);
		WRK_SumStat(wrk);
		VTIM_sleep(cache_param->critbit_cooloff);
	}
	NEEDLESS_RETURN(NULL);
}

/*--------------------------------------------------------------------*/

static void __match_proto__(hash_start_f)
hcb_start(void)
{
	struct objhead *oh = NULL;
	pthread_t tp;

	(void)oh;
	CLI_AddFuncs(hcb_cmds);
	Lck_New(&hcb_mtx, lck_hcb);
	WRK_BgThread(&tp, "hcb-cleaner", hcb_cleaner, NULL);
	memset(&hcb_root, 0, sizeof hcb_root);
	hcb_build_bittbl();
}

static int __match_proto__(hash_deref_f)
hcb_deref(struct objhead *oh)
{
	int r;

	r = 1;
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	Lck_Lock(&oh->mtx);
	assert(oh->refcnt > 0);
	oh->refcnt--;
	if (oh->refcnt == 0) {
		Lck_Lock(&hcb_mtx);
		hcb_delete(&hcb_root, oh);
		VTAILQ_INSERT_TAIL(&cool_h, oh, hoh_list);
		Lck_Unlock(&hcb_mtx);
		assert(VTAILQ_EMPTY(&oh->objcs));
		AZ(oh->waitinglist);
	}
	Lck_Unlock(&oh->mtx);
#ifdef PHK
	fprintf(stderr, "hcb_defef %d %d <%s>\n", __LINE__, r, oh->hash);
#endif
	return (r);
}

static struct objhead * __match_proto__(hash_lookup_f)
hcb_lookup(struct worker *wrk, const void *digest, struct objhead **noh)
{
	struct objhead *oh;
	struct hcb_y *y;
	unsigned u;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AN(digest);
	if (noh != NULL) {
		CHECK_OBJ_NOTNULL(*noh, OBJHEAD_MAGIC);
		assert((*noh)->refcnt == 1);
	}

	/* First try in read-only mode without holding a lock */

	wrk->stats.hcb_nolock++;
	oh = hcb_insert(wrk, &hcb_root, digest, NULL);
	if (oh != NULL) {
		Lck_Lock(&oh->mtx);
		/*
		 * A refcount of zero indicates that the tree changed
		 * under us, so fall through and try with the lock held.
		 */
		u = oh->refcnt;
		if (u > 0) {
			oh->refcnt++;
			return (oh);
		}
		Lck_Unlock(&oh->mtx);
	}

	while (1) {
		/* No luck, try with lock held, so we can modify tree */
		CAST_OBJ_NOTNULL(y, wrk->nhashpriv, HCB_Y_MAGIC);
		Lck_Lock(&hcb_mtx);
		VSC_C_main->hcb_lock++;
		oh = hcb_insert(wrk, &hcb_root, digest, noh);
		Lck_Unlock(&hcb_mtx);

		if (oh == NULL)
			return (NULL);

		Lck_Lock(&oh->mtx);

		CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
		if (noh != NULL && *noh == NULL) {
			assert(oh->refcnt > 0);
			VSC_C_main->hcb_insert++;
			return (oh);
		}
		/*
		 * A refcount of zero indicates that the tree changed
		 * under us, so fall through and try with the lock held.
		 */
		u = oh->refcnt;
		if (u > 0) {
			oh->refcnt++;
			return (oh);
		}
		Lck_Unlock(&oh->mtx);
	}
}

static void __match_proto__(hash_prep_f)
hcb_prep(struct worker *wrk)
{
	struct hcb_y *y;

	if (wrk->nhashpriv == NULL) {
		ALLOC_OBJ(y, HCB_Y_MAGIC);
		AN(y);
		wrk->nhashpriv = y;
	}
}

const struct hash_slinger hcb_slinger = {
	.magic  =	SLINGER_MAGIC,
	.name   =	"critbit",
	.start  =	hcb_start,
	.lookup =	hcb_lookup,
	.prep =		hcb_prep,
	.deref  =	hcb_deref,
};
