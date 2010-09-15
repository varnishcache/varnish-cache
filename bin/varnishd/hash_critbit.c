/*-
 * Copyright (c) 2008-2009 Linpro AS
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

#undef PHK

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache.h"
#include "hash_slinger.h"
#include "cli_priv.h"
#include "vmb.h"

static struct lock hcb_mtx;

/**********************************************************************
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
	return hcb_bittbl[x ^ y];
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

/**********************************************************************
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

/**********************************************************************
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

/**********************************************************************
 * Find the "critical" bit that separates these two digests
 */

static unsigned
hcb_crit_bit(const struct objhead *oh1, const struct objhead *oh2,
    struct hcb_y *y)
{
	unsigned char u, r;

	CHECK_OBJ_NOTNULL(y, HCB_Y_MAGIC);
	for (u = 0; u < DIGEST_LEN && oh1->digest[u] == oh2->digest[u]; u++)
		;
	assert(u < DIGEST_LEN);
	r = hcb_bits(oh1->digest[u], oh2->digest[u]);
	y->ptr = u;
	y->bitmask = 0x80 >> r;
	y->critbit = u * 8 + r;
	return (y->critbit);
}

/*********************************************************************
 * Unless we have the lock, we need to be very careful about pointer
 * references into the tree, we cannot trust things to be the same
 * in two consequtive memory accesses.
 */

static struct objhead *
hcb_insert(struct worker *wrk, struct hcb_root *root, struct objhead *oh, int has_lock)
{
	volatile uintptr_t *p;
	uintptr_t pp;
	struct hcb_y *y, *y2;
	struct objhead *oh2;
	unsigned s, s2;

	p = &root->origo;
	pp = *p;
	if (pp == 0) {
		if (!has_lock)
			return (NULL);
		*p = hcb_r_node(oh);
		return (oh);
	}

	while(hcb_is_y(pp)) {
		y = hcb_l_y(pp);
		CHECK_OBJ_NOTNULL(y, HCB_Y_MAGIC);
		assert(y->ptr < DIGEST_LEN);
		s = (oh->digest[y->ptr] & y->bitmask) != 0;
		assert(s < 2);
		p = &y->leaf[s];
		pp = *p;
	}

	if (pp == 0) {
		/* We raced hcb_delete and got a NULL pointer */
		assert(!has_lock);
		return (NULL);
	}

	assert(hcb_is_node(pp));

	/* We found a node, does it match ? */
	oh2 = hcb_l_node(pp);
	CHECK_OBJ_NOTNULL(oh2, OBJHEAD_MAGIC);
	if (!memcmp(oh2->digest, oh->digest, DIGEST_LEN))
		return (oh2);

	if (!has_lock)
		return (NULL);

	/* Insert */

	CAST_OBJ_NOTNULL(y2, wrk->nhashpriv, HCB_Y_MAGIC);
	wrk->nhashpriv = NULL;
	(void)hcb_crit_bit(oh, oh2, y2);
	s2 = (oh->digest[y2->ptr] & y2->bitmask) != 0;
	assert(s2 < 2);
	y2->leaf[s2] = hcb_r_node(oh);
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
		s = (oh->digest[y->ptr] & y->bitmask) != 0;
		assert(s < 2);
		p = &y->leaf[s];
	}
	y2->leaf[s2] = *p;
	VWMB();
	*p = hcb_r_y(y2);
	return(oh);
}

/**********************************************************************/

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

/**********************************************************************/

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
		cli_out(cli, "%*.*sN %d r%u <%02x%02x%02x...>\n",
		    indent, indent, "", indent / 2, oh->refcnt,
		    oh->digest[0], oh->digest[1], oh->digest[2]);
		return;
	}
	assert(hcb_is_y(p));
	y = hcb_l_y(p);
	cli_out(cli, "%*.*sY c %u p %u b %02x i %d\n",
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
	cli_out(cli, "HCB dump:\n");
	dumptree(cli, hcb_root.origo, 0);
	cli_out(cli, "Coollist:\n");
}

static struct cli_proto hcb_cmds[] = {
	{ "hcb.dump", "hcb.dump", "dump HCB tree\n", 0, 0, "d", hcb_dump },
	{ NULL }
};

/**********************************************************************/

static void *
hcb_cleaner(void *priv)
{
	struct hcb_y *y, *y2;
	struct worker ww;
	struct objhead *oh, *oh2;

	memset(&ww, 0, sizeof ww);
	ww.magic = WORKER_MAGIC;

	THR_SetName("hcb_cleaner");
	(void)priv;
	while (1) {
		VSTAILQ_FOREACH_SAFE(y, &dead_y, list, y2) {
			VSTAILQ_REMOVE_HEAD(&dead_y, list);
			FREE_OBJ(y);
		}
		VTAILQ_FOREACH_SAFE(oh, &dead_h, hoh_list, oh2) {
			VTAILQ_REMOVE(&dead_h, oh, hoh_list);
			HSH_DeleteObjHead(&ww, oh);
		}
		Lck_Lock(&hcb_mtx);
		VSTAILQ_CONCAT(&dead_y, &cool_y);
		VTAILQ_CONCAT(&dead_h, &cool_h, hoh_list);
		Lck_Unlock(&hcb_mtx);
		WRK_SumStat(&ww);
		TIM_sleep(params->critbit_cooloff);
	}
	NEEDLESS_RETURN(NULL);
}

/**********************************************************************/

static void
hcb_start(void)
{
	struct objhead *oh = NULL;
	pthread_t tp;

	(void)oh;
	CLI_AddFuncs(hcb_cmds);
	Lck_New(&hcb_mtx, lck_hcb);
	AZ(pthread_create(&tp, NULL, hcb_cleaner, NULL));
	memset(&hcb_root, 0, sizeof hcb_root);
	hcb_build_bittbl();
}

static int
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
		assert(VTAILQ_EMPTY(&oh->waitinglist));
	}
	Lck_Unlock(&oh->mtx);
#ifdef PHK
	fprintf(stderr, "hcb_defef %d %d <%s>\n", __LINE__, r, oh->hash);
#endif
	return (r);
}

static struct objhead *
hcb_lookup(const struct sess *sp, struct objhead *noh)
{
	struct objhead *oh;
	struct hcb_y *y;
	unsigned u;
	unsigned with_lock;

	(void)sp;

	with_lock = 0;
	while (1) {
		if (with_lock) {
			if (sp->wrk->nhashpriv == NULL) {
				ALLOC_OBJ(y, HCB_Y_MAGIC);
				sp->wrk->nhashpriv = y;
			}
			AN(sp->wrk->nhashpriv);
			Lck_Lock(&hcb_mtx);
			VSC_main->hcb_lock++;
			assert(noh->refcnt == 1);
			oh = hcb_insert(sp->wrk, &hcb_root, noh, 1);
			Lck_Unlock(&hcb_mtx);
		} else {
			VSC_main->hcb_nolock++;
			oh = hcb_insert(sp->wrk, &hcb_root, noh, 0);
		}

		if (oh != NULL && oh == noh) {
			/* Assert that we only muck with the tree with a lock */
			assert(with_lock);
			VSC_main->hcb_insert++;
			assert(oh->refcnt > 0);
			return (oh);
		}

		if (oh == NULL) {
			assert(!with_lock);
			/* Try again, with lock */
			with_lock = 1;
			continue;
		}

		CHECK_OBJ_NOTNULL(noh, OBJHEAD_MAGIC);
		Lck_Lock(&oh->mtx);
		/*
		 * A refcount of zero indicates that the tree changed
		 * under us, so fall through and try with the lock held.
		 */
		u = oh->refcnt;
		if (u > 0) {
			oh->refcnt++;
		} else {
			assert(!with_lock);
			with_lock = 1;
		}
		Lck_Unlock(&oh->mtx);
		if (u > 0)
			return (oh);
	}
}


const struct hash_slinger hcb_slinger = {
	.magic  =       SLINGER_MAGIC,
	.name   =       "critbit",
	.start  =       hcb_start,
	.lookup =       hcb_lookup,
	.deref  =       hcb_deref,
};
